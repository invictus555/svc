//
//  SVCDecoder.cpp
//  svc
//
//  Created by Asterisk on 3/19/21.
//

#include "SVCDecoder.hpp"

SVCDecoder::SVCDecoder(int maxSize, std::string &&extraInfo): svcH264DataQueue_(std::make_shared<SyncQueue<SVCH264Data>>(maxSize)), svcDecoder_(NULL), decoderThread_(NULL), decoderInitialized_(false), extraInfo_(extraInfo){}

SVCDecoder::~SVCDecoder() {}

int SVCDecoder::initSVCDecoder() {
    SDecodingParam decParam;
    auto ret = WelsCreateDecoder(&svcDecoder_);
    if (cmResultSuccess != ret) {
        return -1;
    }
    
    memset(&decParam, 0, sizeof(SDecodingParam));
    decParam.uiTargetDqLayer = UCHAR_MAX;
    decParam.eEcActiveIdc = ERROR_CON_FRAME_COPY_CROSS_IDR;
    decParam.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;
    
    ret = svcDecoder_->Initialize(&decParam);
    if (ret != 0) {
        return -2;
    }
    
    decoderInitialized_ = true;
    return 0;
}

int SVCDecoder::start(NotifyUserCB notifyUser) {
    if (!decoderInitialized_) {
        return -1;
    }
    
    decoderThread_ =
    std::make_shared<std::thread>([this] (NotifyUserCB notifyUser) {
        auto status = 0;
        SBufferInfo dstInfo;
        SVCH264Data svcH264Data;
        unsigned char *pDstBuf = NULL;
        
        while (true) {
            memset(&svcH264Data, 0, sizeof(SVCH264Data));
            svcH264DataQueue_->front(svcH264Data);
            if (svcH264Data.compressedDataLen <= 0 || svcH264Data.compressedData == NULL) { // time to go out
                break;
            }
            
            memset(&dstInfo, 0, sizeof(SBufferInfo));
            auto inputBuffer = svcH264Data.compressedData;
            auto inputBufferLen = svcH264Data.compressedDataLen;
            dstInfo.uiInBsTimeStamp = svcH264Data.timestamp;
            status = svcDecoder_->DecodeFrame2(inputBuffer, inputBufferLen, &pDstBuf, &dstInfo);
            if (notifyUser) {
                notifyUser(false, status, &dstInfo, pDstBuf, extraInfo_);
            }
            
            delete[] inputBuffer;
        }
        
        if (notifyUser) {
            notifyUser(true, 0, NULL, NULL, extraInfo_);
        }
        
        if (svcDecoder_) {
            svcDecoder_->Uninitialize();
            WelsDestroyDecoder(svcDecoder_);
        }
    }, notifyUser);
    
    return 0;
}

void SVCDecoder::put(SVCH264Data &&svcH264Data) {
    if (!svcH264DataQueue_) {
        return;
    }
    
    svcH264DataQueue_->put(std::forward<SVCH264Data>(svcH264Data));
}

void SVCDecoder::stop() {
    if (decoderThread_ && decoderThread_->joinable()) {
        decoderThread_->join();
    }
}

void SVCDecoder::interrupt() {
    if (svcH264DataQueue_) {
        svcH264DataQueue_->interrupt();
    }
}


