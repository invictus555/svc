//
//  SVCEncoder.cpp
//  svc
//
//  Created by Asterisk on 3/20/21.
//

#include "SVCEncoder.hpp"

SVCEncoder::SVCEncoder(int maxSize): pictureQueue_(std::make_shared<SyncQueue<SSourcePicture>>(maxSize)), svcEncoder_(NULL), encoderInitialized_(false), encoderThread_(NULL){}

SVCEncoder::~SVCEncoder(){}

int SVCEncoder::initSVCEncoder(int width, int height, int temporalNum, int spatialNum, std::vector<SpatialData> &spatials) {
    TagEncParamExt encParam;
    auto ret = WelsCreateSVCEncoder(&svcEncoder_);
    if (cmResultSuccess != ret) {
        return -1;
    }
    
    memset(&encParam, 0, sizeof(SEncParamExt));
    svcEncoder_->GetDefaultParams(&encParam);
    encParam.iUsageType = CAMERA_VIDEO_REAL_TIME;
    encParam.fMaxFrameRate = 25;
    encParam.iPicWidth = width;
    encParam.iPicHeight = height;
    encParam.iRCMode = RC_QUALITY_MODE;
    encParam.bEnableDenoise = false;
    encParam.bEnableBackgroundDetection = true;
    encParam.bEnableAdaptiveQuant = false;
    encParam.bEnableFrameSkip = false;  //true
    encParam.bEnableLongTermReference = false;
    encParam.uiIntraPeriod = 50;
    encParam.eSpsPpsIdStrategy = CONSTANT_ID;
    encParam.bPrefixNalAddingCtrl = false;
    encParam.iSpatialLayerNum = spatialNum;
    encParam.iTemporalLayerNum = temporalNum;

    for (auto i = spatialNum - 1; i >= 0; i--) {
        auto item = spatials.at(i);
        encParam.iTargetBitrate += item.bitrate;
        encParam.sSpatialLayers[i].iVideoWidth = item.width;
        encParam.sSpatialLayers[i].iVideoHeight = item.height;
        encParam.sSpatialLayers[i].fFrameRate = 25;
        encParam.sSpatialLayers[i].iSpatialBitrate = item.bitrate;
        encParam.sSpatialLayers[i].iMaxSpatialBitrate = item.bitrate * 3 >> 1;
    }
    
    ret = svcEncoder_->InitializeExt(&encParam);
    if (ret != 0) {
        return ret;
    }
    
    encoderInitialized_ = true;
    return ret;
}

int SVCEncoder::start(NotifySVCDecoderCB notifySVCDecoder) {
    if(!encoderInitialized_) {
        return -1;
    }
    
    encoderThread_ =
    std::make_shared<std::thread>([this] (NotifySVCDecoderCB notifySVCDecoder) {
        SFrameBSInfo encodedInfo;
        SSourcePicture i420Picture;
        while (true) {
            memset(&i420Picture, 0, sizeof(SSourcePicture));
            pictureQueue_->front(i420Picture);
            if (i420Picture.iPicWidth <= 0 || i420Picture.iPicHeight <= 0) {   // time to break
                break;
            }
            
            if (i420Picture.pData[0] == NULL || i420Picture.pData[1] == NULL || i420Picture.pData[2] == NULL) {
                break;
            }
            
            memset(&encodedInfo, 0, sizeof(SFrameBSInfo));
            auto status = svcEncoder_->EncodeFrame(&i420Picture, &encodedInfo);
            if (notifySVCDecoder) {
                notifySVCDecoder(false, status, &encodedInfo);
            }
            
            delete [] i420Picture.pData[0];
            delete [] i420Picture.pData[1];
            delete [] i420Picture.pData[2];
        }
        
        if (notifySVCDecoder) { // send a terminal signal
            notifySVCDecoder(true, 0, NULL);
        }
        
        if (svcEncoder_) {
            svcEncoder_->Uninitialize();
            WelsDestroySVCEncoder(svcEncoder_);
        }
        
    }, notifySVCDecoder);

    return 0;
}

void SVCEncoder::put(SSourcePicture &&sourcePic) {
    if (!pictureQueue_) {
        return;
    }
    
    pictureQueue_->put(std::forward<SSourcePicture>(sourcePic));
}

void SVCEncoder::stop() {
    if (encoderThread_ && encoderThread_->joinable()) {
        encoderThread_->join();
    }
}

void SVCEncoder::interrupt() {
    if (pictureQueue_) {
        pictureQueue_->interrupt();
    }
}
