//
//  SVCProj.cpp
//  svc
//
//  Created by Asterisk on 3/18/21.
//

#include "SVCProj.hpp"
#include "Localize.hpp"

SVCProj::SVCProj(int temporalNum, int spatialNum, std::initializer_list<SpatialData> spatialList): svcTemporalNum_(temporalNum), svcSpatialNum_(spatialNum), stop_(false), fmtCtx_(NULL), readThread_(NULL), svcH264Decoders_(SVCDecoderShrVec(MAX_SPATIAL_LAYER_NUM * MAX_TEMPORAL_LAYER_NUM, NULL)), h264Decoder_(NULL), started_(false), svcH264Encoder_(NULL), syncQueueMaxSize_(50) {
    
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
    
    // 2. init one svc encoder
    initSVCH264Encoder(picWidth, picHeight);
    
    // 4. init several svc spatial decoders
    initSVCH264Decoders();
        
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
    
    if (svcH264Encoder_) {  // stop svc encoder
        svcH264Encoder_->stop();
    }
    
    for (auto it = svcH264Decoders_.begin(); it != svcH264Decoders_.end(); it++) {    // stop all svc decoders
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
            svcH264Encoder_->put(std::move(nullSourcePic));
            return;
        }
        
        if (status < 0) {
            av_log(NULL, AV_LOG_ERROR, "H264Decoder: frame is not available\n");
            return;
        }
        
        // send I420 picture to SVC Spatial encoder
        SSourcePicture spatialPic = createSSourcePicture(frame);
        svcH264Encoder_->put(std::move(spatialPic));
    });
}

void SVCProj::initSVCH264Encoder(int width, int height) {
    svcH264Encoder_ = std::make_shared<SVCEncoder>(syncQueueMaxSize_);
    auto status = svcH264Encoder_->initSVCEncoder(width, height, svcTemporalNum_, svcSpatialNum_, spatialSettings_);
    av_log(NULL, AV_LOG_DEBUG, "initSVCH264Encoder: status = %d\n", status);
    svcH264Encoder_->start([this](bool eof, int status, SFrameBSInfo *pEncodedInfo) {
        if (eof) {  // EOF
            av_log(NULL, AV_LOG_DEBUG, "SVCH264Encoder: send a terminaate signal to all SVC Temporal decoders\n");
            for (auto it = svcH264Decoders_.begin(); it != svcH264Decoders_.end(); it++) {
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
            av_log(NULL, AV_LOG_ERROR, "SVCH264Encoder: frame type is invalid, frameType = %d\n", pEncodedInfo->eFrameType);
            return;
        }
        
        //  dispatch SVC H264 compressed data to corresponding SVC decoder correctly
        /* 1. spatial rules
         * 低分辨率是基本层，增强层是建立在基本层之上的差值。故:高分辨率需要将基本层与对应的增强层组合
         * 例如：完整的1080p的NAL = NAL_360p(基本层) + NAL_480p(增强层) + NAL_720p(增强层) + NAL_1080p(增强层)
         * 2. temporal rules
         * iTemporalLayerNum 的值为 1 时，使用 uiGopSize = 1 的配置，即每一帧为一组，每一组的uiTemporalId 值为 0
         * iTemporalLayerNum 的值为 2 时，使用 uiGopSize = 2 的配置，即每两帧为一组，每一组中对应的uiTemporalId 为 [0, 1]
         * iTemporalLayerNum 的值为 3 时，使用 uiGopSize = 4 的配置，即每四帧为一组，每一组中对应的uiTemporalId 为 [0, 2, 1, 2]
         * iTemporalLayerNum 的值为 4 时，使用 uiGopSize = 8 的配置，即每八帧为一组，每一组中对应的uiTemporalId 为 [0, 3, 2, 3, 1, 3, 2, 3]
         */
        
        // 每一次只有一个temporalId但是可能有多个spatialId, temporal层面的NAL不需要拼接， Spatial层面的NAL需要拼接。
        auto totalLayerNum = pEncodedInfo->iLayerNum;
        for (auto curLayerIndex = 0; curLayerIndex < totalLayerNum; curLayerIndex++) {
            auto curLayerInfo = pEncodedInfo->sLayerInfo[curLayerIndex];
            auto curSpatialId = curLayerInfo.uiSpatialId;
            auto curTemporalId = curLayerInfo.uiTemporalId;
            av_log(NULL, AV_LOG_DEBUG, "svcH264Encoder: temporal_id = %d, spatial_id = %d\n", curTemporalId, curSpatialId);

            auto totalSize = 0; // 计算出某空域NAL所需要的完整数据大小，因为spatial NAL是按照低分辨率到高分辨率出场的, 累加即可。
            for (auto i = 0; i <= curLayerIndex; i++) {
                auto tmp = pEncodedInfo->sLayerInfo[i];
                for (auto nalIdx = 0; nalIdx < tmp.iNalCount; nalIdx++) {
                    totalSize += tmp.pNalLengthInByte[nalIdx];
                }
            }
            
            /* T0 需要 temporalId = {0}的NAL,
             * T1 需要 temporalId = {0, 1}的NAL,
             * T2 需要 temporalId = {0, 1, 2}的NAL,
             * T3 需要 temporalId = {0, 1, 2, 3}的NAL
             * 即 NAL的temporal = 0时，需要向 T0，T1，T2，T3 发送。
             * NAL的temporal = 1时，需要向T1，T2，T3 发送。
             * 以此类推....
             */
            for (auto temporalId = 0; temporalId < svcTemporalNum_ - curTemporalId; temporalId++) {
                // 根据上面的提示， 给需要temporalId=x的decoder发送完整的NAL，必须得到目标decoder。
                auto decoderAt = (svcTemporalNum_ - temporalId - 1) * MAX_SPATIAL_LAYER_NUM + curSpatialId;
                auto svcDecoder = svcH264Decoders_.at(decoderAt);
                if (svcDecoder == NULL) {
                    av_log(NULL, AV_LOG_ERROR, "SVCH264Encoder: Fatal Something Wrong\n");
                    continue;
                }
                
                // 组合spatial NAL(多个NAL组合成一个一个完整的NAL)
                auto bufferPtrOffset = 0;
                auto pBuf = new unsigned char[totalSize];
                for (auto j = 0; j <= curLayerIndex; j++) {
                    auto totalSizeOfCurrentLayerInfo = 0;
                    auto tmp = pEncodedInfo->sLayerInfo[j];
                    for (auto nalIdx = 0; nalIdx < tmp.iNalCount; nalIdx++) {
                        totalSizeOfCurrentLayerInfo += tmp.pNalLengthInByte[nalIdx];
                    }
                    
                    memcpy(pBuf + bufferPtrOffset, tmp.pBsBuf, totalSizeOfCurrentLayerInfo);
                    bufferPtrOffset += totalSizeOfCurrentLayerInfo;
                }
                
                // dispatch NAL
                SVCH264Data data = { .compressedDataLen = totalSize, .compressedData = pBuf, .timestamp = pEncodedInfo->uiTimeStamp};
                svcDecoder->dumpSvcHandler()->write(pBuf, totalSize);
                svcDecoder->put(std::move(data));
            }
        }
    });
}

void SVCProj::initSVCH264Decoders() {
    for (auto i = 0; i < svcTemporalNum_; i++) {
        for (auto j = 0; j < svcSpatialNum_; j++) {
            auto item = spatialSettings_.at(j);
            std::string uniqueTag = "SVC_T";
            uniqueTag.append(std::to_string(i)).append("_").append(std::to_string(item.width)).append("x").append(std::to_string(item.height));
            auto svcDecoder = std::make_shared<SVCDecoder>(syncQueueMaxSize_, dumpDataDir_, std::move(uniqueTag));
            auto status = svcDecoder->initSVCDecoder();
            av_log(NULL, AV_LOG_DEBUG, "initSVCH264Decoders: status = %d\n", status);
            svcH264Decoders_.at(i * MAX_SPATIAL_LAYER_NUM + j) = svcDecoder;
            svcDecoder->start([this](bool eof, int status, SBufferInfo *pDecodedInfo, uchar **ppDst, SVCDecoder *thiz){
                if (eof) {
                    av_log(NULL, NULL, "SVCH264Decoder[%s]: time to Game Over, Bye...\n", thiz->tag().c_str());
                    return;
                }
                
                if (status || pDecodedInfo->iBufferStatus != 1) {
                    av_log(NULL, AV_LOG_DEBUG, "SVCH264Decoder[%s]: Decoded Data is Unavailable, status: %d, bufferStatus: %d\n",
                           thiz->tag().c_str(), status, pDecodedInfo->iBufferStatus);
                    return;
                }
                
                // can print some info about decoded yuv
                auto inTimestamp = pDecodedInfo->uiInBsTimeStamp;
                auto outTimestamp = pDecodedInfo->uiOutYuvTimeStamp;
                auto width = pDecodedInfo->UsrData.sSystemBuffer.iWidth;
                auto height = pDecodedInfo->UsrData.sSystemBuffer.iHeight;
                auto strideY = pDecodedInfo->UsrData.sSystemBuffer.iStride[0];
                auto strideUV = pDecodedInfo->UsrData.sSystemBuffer.iStride[1];

                av_log(NULL, AV_LOG_DEBUG, "SVCH264Decoder[%s]: outTimestamp: %lld, inTimestamp: %lld, width: %d, height: %d, stideY: %d, strideUV: %d\n",
                       thiz->tag().c_str(), outTimestamp, inTimestamp, width, height, strideY, strideUV);
                thiz->dumpYuvHandler()->write(ppDst, strideY, width, height);
            });
        }
    }
}
