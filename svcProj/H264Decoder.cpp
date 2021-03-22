//
//  H264Decoder.cpp
//  svc
//
//  Created by Asterisk on 3/20/21.
//

#include "H264Decoder.hpp"

H264Decoder::H264Decoder(int maxSize): h264PacketQueue_(std::make_shared<SyncQueue<AVPacket>>(maxSize)), h264Decoder_(NULL), decoderInitialized_(false){}

H264Decoder::~H264Decoder(){}

int H264Decoder::initH264Decoder(AVStream *stream) {
    if (!stream) {
        av_log(NULL, AV_LOG_ERROR, "please call open_input_url firstly OR this file has NO video stream\n");
        return -1;
    }
    
    auto decCodec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decCodec) {
        av_log(NULL, AV_LOG_ERROR, "can not find %d decoder codec\n", stream->codecpar->codec_id);
        return -2;
    }
    
    h264Decoder_ = avcodec_alloc_context3(decCodec);
    if (!h264Decoder_) {
        av_log(NULL, AV_LOG_ERROR, "can not alloc decoder context\n");
        return -3;
    }
    
    auto ret = avcodec_parameters_to_context(h264Decoder_, stream->codecpar);
    h264Decoder_->thread_count = 4;
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "failed to copy parameters into context\n");
        return -4;
    }
    
    ret = avcodec_open2(h264Decoder_, decCodec, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not open video codec\n");
        return ret;
    }
    
    decoderInitialized_ = true;
    return 0;
}

int H264Decoder::start(NotifySVCEncoderCB notifySVCEncoder) {
    if (!decoderInitialized_) {
        return -1;
    }
    
    h264DecoderThread_ =
    std::make_shared<std::thread>([this] (NotifySVCEncoderCB notifySVCEncoder) {
        AVPacket pkt;
        AVFrame *outFrame = av_frame_alloc();
        while (true) {
            av_init_packet(&pkt);
            h264PacketQueue_->front(pkt);
            if (pkt.data == NULL || pkt.size == 0) {
                break;
            }
            
            auto status = avcodec_send_packet(h264Decoder_, &pkt);
            status = avcodec_receive_frame(h264Decoder_, outFrame);
            if (notifySVCEncoder) {
                notifySVCEncoder(false, status, outFrame);
            }
            
            av_packet_unref(&pkt);
        }
        
        if (notifySVCEncoder) {
            notifySVCEncoder(true, 0, NULL);      // terminal flag
        }
        
        av_frame_free(&outFrame);
        if (h264Decoder_) {
            if (avcodec_is_open(h264Decoder_)) {
                avcodec_close(h264Decoder_);
            }
            
            avcodec_free_context(&h264Decoder_);
        }
    }, notifySVCEncoder);
    
    return 0;
}

void H264Decoder::put(AVPacket &&pkt) {
    if (!h264PacketQueue_) {
        return;
    }
    
    h264PacketQueue_->put(std::forward<AVPacket>(pkt));
}

void H264Decoder::stop() {
    if (h264DecoderThread_ && h264DecoderThread_->joinable()) {
        h264DecoderThread_->join();
    }
}

void H264Decoder::interrupt() {
    if (h264PacketQueue_) {
        h264PacketQueue_->interrupt();
    }
}
