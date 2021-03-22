//
//  SVCProj.cpp
//  svc
//
//  Created by Asterisk on 3/18/21.
//

#include "SVCProj.hpp"
#include "Localize.hpp"

SVCProj::SVCProj(int temporalNum, int spatialNum, std::initializer_list<SpatialData> spatialList): svcTemporalNum_(temporalNum), svcSpatialNum_(spatialNum), stop_(false), fmtCtx_(NULL), readThread_(NULL), svcSpatialDecoders_(SVCDecoderShrVec(MAX_SPATIAL_LAYER_NUM, NULL)), h264Decoder_(NULL), started_(false), svcTemporalEncoder_(NULL), svcSpatialEncoder_(NULL), syncQueueMaxSize_(50), svcTemporalDecoders_(SVCDecoderShrVec(MAX_TEMPORAL_LAYER_NUM, NULL)) {
    
    svcTemporalNum_ = std::max(std::min(svcTemporalNum_, MAX_TEMPORAL_LAYER_NUM), 1);
    svcSpatialNum_ = std::max(std::min(static_cast<int>(spatialList.size()), std::min(svcSpatialNum_, MAX_SPATIAL_LAYER_NUM)), 1);
    
    for (auto it = spatialList.begin(); it != spatialList.end(); it++) {
        spatialSettings_.push_back(*it);
    }
}

SVCProj::~SVCProj()
{
    if (fmtCtx_) {
        avformat_close_input(&fmtCtx_);
    }
}

void SVCProj::start(std::string &url, std::string &dumpDir, int maxSize, int logLevel)
{
    if (started_) {
        av_log(NULL, AV_LOG_WARNING, "warning: SVCProj has started......\n");
        return;
    }
    
    started_ = true;
    if (maxSize > 0) {
        syncQueueMaxSize_ = maxSize;
    }
    
    auto ret = openInputSourceMedia(url, logLevel);
    if (ret) {
        av_log(NULL, AV_LOG_ERROR, "call open_input_url failed, ret = %d\n", ret);
        return ;
    }
    
    auto picWidth = h264Stream_->codecpar->width;
    auto picHeight = h264Stream_->codecpar->height;
    av_log(NULL, AV_LOG_DEBUG, "OpenInput: media_width = %d, media_height = %d\n", picWidth, picHeight);
    
    dumpDataDir_ = dumpDir;
    correctSpatialData();    // correct some data like spatials
    
    av_log(NULL, AV_LOG_DEBUG, "OpenInput: svcTemporalNum = %d, svcSpatialNum = %d\n", svcTemporalNum_, svcSpatialNum_);
    // 1. init one h264 decoder
    initH264Decoder();
    
    // 2. init one spatial svc encoder
    initSVCSpatialEncoder(picWidth, picHeight);
    
    // 3. create a temporal svc encoder
    initSVCTemporalEncoder(picWidth, picHeight);
    
    // 4. init several svc spatial decoders
    initSVCSpatialDecoders();
    
    // 5. init several svc temporal decoders
    initSVCTemporalDecoders();
    
    // read packet from input media file
    readThread_ = std::make_shared<std::thread>([this]{
        while (!stop_) {
            AVPacket pkt;
            av_init_packet(&pkt);
            auto read_ret = av_read_frame(fmtCtx_, &pkt);
            if (read_ret < 0 ) {
                av_packet_unref(&pkt);
                av_log(NULL, AV_LOG_ERROR, "readThread: Failed to read frame, ret = %d\n", read_ret);
                break;
            }
                    
            if (fmtCtx_->streams[pkt.stream_index] != h264Stream_) { // only video to decode
                av_packet_unref(&pkt);
                continue;
            }
            
            // send packet to H264 decoder queue
            if (!h264Decoder_) {
                break;
            }
            
            h264Decoder_->put(std::forward<AVPacket>(pkt));
        }
    });
}

SVCProj* SVCProj::interrupt() {
    stop_ = true;
    return this;
}

void SVCProj::stop() {
    if (!started_) {
        av_log(NULL, AV_LOG_WARNING, "warning: SVCProj has not started.......");
        return;
    }
    
    started_ = false;
    
    if (readThread_) {
        readThread_->join();
    }
    
    if (h264Decoder_) { // stop h264 decoder
        AVPacket nullPkt = {.data = NULL, .size = 0};
        h264Decoder_->put(std::forward<AVPacket>(nullPkt));
        h264Decoder_->stop();
    }
    
    if (svcSpatialEncoder_) {  // stop svc encoder
        svcSpatialEncoder_->stop();
    }
    
    if (svcTemporalEncoder_) {
        svcTemporalEncoder_->stop();
    }
    
    for (auto it = svcSpatialDecoders_.begin(); it != svcSpatialDecoders_.end(); it++) {    // stop all svc decoders
        if (*it == NULL) {
            continue;
        }
        
        (*it)->stop();
    }
    
    for (auto it = svcTemporalDecoders_.begin(); it != svcTemporalDecoders_.end(); it++) {    // stop all svc decoders
        if (*it == NULL) {
            continue;
        }
        
        (*it)->stop();
    }
}

int SVCProj::openInputSourceMedia(std::string &url, int logLevel)
{
    if (url.empty()) {
        av_log(NULL, AV_LOG_ERROR, "%s is invalid\n", url.c_str());
        return -1;
    }
    
    avformat_network_init();
    av_log_set_level(logLevel);
    auto ret = avformat_open_input(&fmtCtx_, url.c_str(), NULL, NULL);
    if (ret) {
        av_log(NULL, AV_LOG_ERROR, "Couldn't open input stream, ret = %d\n", ret);
        return ret;
    }
    
    
    ret = avformat_find_stream_info(fmtCtx_, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Couldn't find stream information");
        return ret;
    }
    
    for (auto i = 0; i < fmtCtx_->nb_streams; i++) {
        if (fmtCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            h264Stream_ = fmtCtx_->streams[i];
            break;
        }
    }
    
    if (!h264Stream_) {
        av_log(NULL, AV_LOG_ERROR, "this file has NO video stream\n");
        return -2;
    }
    
    av_dump_format(fmtCtx_, 0, url.c_str(), 0);

    return 0;
}

void SVCProj::correctSpatialData() {
    auto originWidth = h264Stream_->codecpar->width;
    auto originHeight = h264Stream_->codecpar->height;
    while (spatialSettings_.size() > svcSpatialNum_) {
        spatialSettings_.pop_back();
    }
    
    auto largest = spatialSettings_.at(svcSpatialNum_ - 1);
    if (largest.width != originWidth || largest.height != originHeight) {
        largest.width = originWidth;
        largest.height = originHeight;
        spatialSettings_.at(svcSpatialNum_ - 1) = largest;
    }
}

SSourcePicture &&SVCProj::createSSourcePicture(AVFrame *frame) {
    SSourcePicture sourcePic;
    memset(&sourcePic, 0, sizeof(SSourcePicture));
    sourcePic.iPicWidth = frame->width;
    sourcePic.iPicHeight = frame->height;
    sourcePic.iColorFormat = videoFormatI420;
    sourcePic.iStride[0] = sourcePic.iPicWidth;
    sourcePic.iStride[1] = sourcePic.iPicWidth >> 1;
    sourcePic.iStride[2] = sourcePic.iPicWidth >> 1;
    
    auto y_size = frame->width * frame->height;
    sourcePic.pData[0] = new unsigned char[y_size];
    memcpy(sourcePic.pData[0], frame->data[0], y_size);
    
    auto u_size = frame->width * frame->height >> 2;
    sourcePic.pData[1] = new unsigned char[u_size];
    memcpy(sourcePic.pData[1], frame->data[1], u_size);
    
    auto v_size = frame->width * frame->height >> 2;
    sourcePic.pData[2] = new unsigned char[v_size];
    memcpy(sourcePic.pData[2], frame->data[2], v_size);
    
    AVRational dst_timebase = (AVRational){1, 1000};
    AVRational src_timebase = h264Stream_->time_base;
    sourcePic.uiTimeStamp = av_rescale_q_rnd(frame->pts, src_timebase, dst_timebase, AV_ROUND_DOWN);
    return std::move(sourcePic);
}

void SVCProj::initH264Decoder() {
    h264Decoder_ = std::make_shared<H264Decoder>(syncQueueMaxSize_);
    auto status = h264Decoder_->initH264Decoder(h264Stream_);
    av_log(NULL, AV_LOG_DEBUG, "initH264Decoder: status = %d\n", status);
    h264Decoder_->start([this](bool eof, int status, AVFrame* frame) {
        if (eof) {
            SSourcePicture nullSourcePic;
            av_log(NULL, AV_LOG_DEBUG, "H264Decoder: send a terminal signal to SVC spatial encoder\n");
            memset(&nullSourcePic, 0, sizeof(SSourcePicture));
            svcSpatialEncoder_->put(std::move(nullSourcePic));
            
            av_log(NULL, AV_LOG_DEBUG, "H264Decoder: send a terminal signal to SVC temporal encoder\n");
            memset(&nullSourcePic, 0, sizeof(SSourcePicture));
            svcTemporalEncoder_->put(std::move(nullSourcePic));
            return;
        }
        
        if (status < 0) {
            av_log(NULL, AV_LOG_ERROR, "H264Decoder: frame is not available\n");
            return;
        }
        
        // send I420 picture to SVC Spatial encoder
        SSourcePicture spatialPic = createSSourcePicture(frame);
        svcSpatialEncoder_->put(std::move(spatialPic));
        
        // send I420 picture to SVC Temporal encoder
        SSourcePicture temporalPic = createSSourcePicture(frame);
        svcTemporalEncoder_->put(std::move(temporalPic));
    });
}

void SVCProj::initSVCSpatialEncoder(int width, int height) {
    svcSpatialEncoder_ = std::make_shared<SVCEncoder>(syncQueueMaxSize_);
    auto status = svcSpatialEncoder_->initSVCEncoder(width, height, svcSpatialNum_, spatialSettings_);
    av_log(NULL, AV_LOG_DEBUG, "initSVCSpatialEncoder: status = %d\n", status);
    svcSpatialEncoder_->start([this](bool eof, int status, SFrameBSInfo *pEncodedInfo) {
        if (eof) {  // EOF
            av_log(NULL, AV_LOG_DEBUG, "SVCSpatialEncoder: send a terminaate signal to all SVC Temporal decoders\n");
            for (auto it = svcSpatialDecoders_.begin(); it != svcSpatialDecoders_.end(); it++) {
                if(*it == NULL) {
                    continue;
                }
                
                SVCH264Data nullData = { .compressedDataLen = 0, .compressedData = NULL};
                (*it)->put(std::move(nullData));
            }
            return ;
        }
        
        // not EOF
        if (pEncodedInfo->eFrameType == videoFrameTypeInvalid || pEncodedInfo->eFrameType == videoFrameTypeSkip) {
            av_log(NULL, AV_LOG_ERROR, "SVCSpatialEncoder: frame type is invalid, frameType = %d\n", pEncodedInfo->eFrameType);
            return;
        }
        
        //  dispatch SVC H264 compressed data to corresponding SVC Spatial decoder correctly
        for (auto idx = 0; idx < pEncodedInfo->iLayerNum; idx++) {
            auto totalSize = 0, bufferPtrOffset = 0;
            auto layerInfo = pEncodedInfo->sLayerInfo[idx];
            auto curSpatialId = layerInfo.uiSpatialId;
            for (auto i = 0; i <= idx; i++) {   // accumulate size all needed
                auto tmp = pEncodedInfo->sLayerInfo[i];
                for (auto nalIdx = 0; nalIdx < tmp.iNalCount; nalIdx++) {
                    totalSize += tmp.pNalLengthInByte[nalIdx];
                }
            }
            
            auto svcDecoder = svcSpatialDecoders_.at(curSpatialId);
            if (svcDecoder == NULL) {
                av_log(NULL, AV_LOG_ERROR, "SVCSpatialEncoder: Fatal Something Wrong\n");
                return ;
            }
            
            // dispatch elements layer and enhancement layer
            // 低分辨率是基本层，增强层是建立在基本层之上的差值。故:高分辨率需要将基本层与对应的增强层组合
            auto pBuf = new unsigned char[totalSize];
            for (auto j = 0; j <= idx; j++) {
                auto totalSizeOfCurrentLayerInfo = 0;
                auto tmp = pEncodedInfo->sLayerInfo[j];
                for (auto nalIdx = 0; nalIdx < tmp.iNalCount; nalIdx++) {
                    totalSizeOfCurrentLayerInfo += tmp.pNalLengthInByte[nalIdx];
                }
                
                memcpy(pBuf + bufferPtrOffset, tmp.pBsBuf, totalSizeOfCurrentLayerInfo);
                bufferPtrOffset += totalSizeOfCurrentLayerInfo;
            }
            
            SVCH264Data data = { .compressedDataLen = totalSize, .compressedData = pBuf, .timestamp = pEncodedInfo->uiTimeStamp};
            svcDecoder->put(std::move(data));
        }
    });
}

void SVCProj::initSVCTemporalEncoder(int width, int height) {
    svcTemporalEncoder_ = std::make_shared<SVCEncoder>(syncQueueMaxSize_);
    auto status = svcTemporalEncoder_->initSVCEncoder(width, height, spatialSettings_.back().bitrate, svcTemporalNum_);
    av_log(NULL, AV_LOG_DEBUG, "initSVCTemporalEncoder: status = %d\n", status);
    svcTemporalEncoder_->start([this](bool eof, int status, SFrameBSInfo *pEncodedInfo){
        if (eof) {  // EOF
            av_log(NULL, AV_LOG_DEBUG, "SVCTemporalEncoder: send a terminaate signal to all SVC Temporal decoders\n");
            for (auto it = svcTemporalDecoders_.begin(); it != svcTemporalDecoders_.end(); it++) {
                if(*it == NULL) {
                    continue;
                }
                
                SVCH264Data nullData = { .compressedDataLen = 0, .compressedData = NULL};
                (*it)->put(std::move(nullData));
            }
            return ;
        }
        
        // not EOF
        if (pEncodedInfo->eFrameType == videoFrameTypeInvalid || pEncodedInfo->eFrameType == videoFrameTypeSkip) {
            av_log(NULL, AV_LOG_ERROR, "SVCTemporalEncoder: frame type is invalid, frameType = %d\n", pEncodedInfo->eFrameType);
            return;
        }
        
        //  dispatch SVC H264 compressed data to corresponding SVC Temporal decoder correctly
        for (auto idx = 0; idx < pEncodedInfo->iLayerNum; idx++) {
            auto layerInfo = pEncodedInfo->sLayerInfo[idx];
            auto curTemporalId = layerInfo.uiTemporalId;
            auto totalSizeOfCurrentLayerInfo = 0;
            for (auto nalIdx = 0; nalIdx < layerInfo.iNalCount; nalIdx++) {
                totalSizeOfCurrentLayerInfo += layerInfo.pNalLengthInByte[nalIdx];
            }
            
            /*
             iTemporalLayerNum 的值为 1 时，使用 uiGopSize = 1 的配置，即每一帧为一组，每一组的 uiTemporalId 值为 0
             iTemporalLayerNum 的值为 2 时，使用 uiGopSize = 2 的配置，即每两帧为一组，每一组中对应的uiTemporalId 为 [0, 1]
             iTemporalLayerNum 的值为 3 时，使用 uiGopSize = 4 的配置，即每四帧为一组，每一组中对应的uiTemporalId 为 [0, 2, 1, 2]
             iTemporalLayerNum 的值为 4 时，使用 uiGopSize = 8 的配置，即每八帧为一组，每一组中对应的uiTemporalId 为 [0, 3, 2, 3, 1, 3, 2, 3]
             */
            
            for (auto i = 0; i < svcTemporalNum_ - curTemporalId; i++) {
                auto svcDecoder = svcTemporalDecoders_.at(svcTemporalNum_ - i - 1);
                if (svcDecoder == NULL) {
                    av_log(NULL, AV_LOG_ERROR, "SVCTemporalEncoder: Fatal Something Wrong\n");
                    continue;
                }
                
                auto pBuf = new unsigned char[totalSizeOfCurrentLayerInfo];
                memcpy(pBuf, layerInfo.pBsBuf, totalSizeOfCurrentLayerInfo);
                SVCH264Data data = { .compressedDataLen = totalSizeOfCurrentLayerInfo, .compressedData = pBuf, .timestamp = pEncodedInfo->uiTimeStamp};
                svcDecoder->put(std::move(data));
            }
        }
    });
}

void SVCProj::initSVCSpatialDecoders() {
    for (auto index = 0; index < svcSpatialNum_; index++) {
        auto item = spatialSettings_.at(index);
        std::string extraInfo = "SVC_S";
        extraInfo.append(std::to_string(index)).append("_").append(std::to_string(item.width)).append("x").append(std::to_string(item.height));
        auto svcDecoder = std::make_shared<SVCDecoder>(syncQueueMaxSize_, std::move(extraInfo));
        auto status = svcDecoder->initSVCDecoder();
        av_log(NULL, AV_LOG_DEBUG, "initSVCSpatialDecoders: status = %d\n", status);
        svcSpatialDecoders_.at(index) = svcDecoder;
        svcDecoder->start([this](bool eof, int status, SBufferInfo *pDecodedInfo, uchar *pDst, std::string &extra){
            if (eof) {
                av_log(NULL, NULL, "SVCSpatialDecoder[%s]:time to Game Over\n", extra.c_str());
                return;
            }
            
            if (status || pDecodedInfo->iBufferStatus != 1) {
                av_log(NULL, AV_LOG_DEBUG, "SVCSpatialDecoder[%s]: Unavailable, status = %d, bufferStatus = %d\n", extra.c_str(), status, pDecodedInfo->iBufferStatus);
                return;
            }
            
            // can print some info about decoded yuv
            auto inTimestamp = pDecodedInfo->uiInBsTimeStamp;
            auto outTimestamp = pDecodedInfo->uiOutYuvTimeStamp;
            auto width = pDecodedInfo->UsrData.sSystemBuffer.iWidth;
            auto height = pDecodedInfo->UsrData.sSystemBuffer.iHeight;
            av_log(NULL, AV_LOG_DEBUG, "SVCSpatialDecoder[%s]: outTimestamp = %lld, inTimestamp = %lld, width = %d, height = %d\n",
                   extra.c_str(), outTimestamp, inTimestamp, width, height);
        });
    }
}

void SVCProj::initSVCTemporalDecoders() {
    for (auto index = 0; index < svcTemporalNum_; index++) {
        std::string extraInfo = "SVC_T";
        auto svcDecoder = std::make_shared<SVCDecoder>(syncQueueMaxSize_, std::move(extraInfo.append(std::to_string(index))));
        auto status = svcDecoder->initSVCDecoder();
        av_log(NULL, AV_LOG_DEBUG, "initSVCTemporalDecoders: status = %d\n", status);
        svcTemporalDecoders_.at(index) = svcDecoder;
        // send decoded SVC info to user
        svcDecoder->start([this](bool eof, int status, SBufferInfo *pDecodedInfo, uchar *pDst, std::string &extra){
            if (eof) {
                av_log(NULL, NULL, "SVCTemporalDecoder[%s]:time to Game Over\n", extra.c_str());
                return;
            }
            
            if (status || pDecodedInfo->iBufferStatus != 1) {
                av_log(NULL, AV_LOG_DEBUG, "SVCTemporalDecoder[%s]: Unavailable, status = %d, bufferStatus = %d\n", extra.c_str(), status, pDecodedInfo->iBufferStatus);
                return;
            }
            
            // can print some info about decoded yuv
            auto inTimestamp = pDecodedInfo->uiInBsTimeStamp;
            auto outTimestamp = pDecodedInfo->uiOutYuvTimeStamp;
            auto width = pDecodedInfo->UsrData.sSystemBuffer.iWidth;
            auto height = pDecodedInfo->UsrData.sSystemBuffer.iHeight;
            av_log(NULL, AV_LOG_DEBUG, "SVCTemporalDecoder[%s]: outTimestamp = %lld, inTimestamp = %lld, width = %d, height = %d\n",
                   extra.c_str(), outTimestamp, inTimestamp, width, height);
        });
    }
}
