//
//  H264Decoder.hpp
//  svc
//
//  Created by Asterisk on 3/20/21.
//

#ifndef H264Decoder_hpp
#define H264Decoder_hpp

#include <stdio.h>
#include <thread>
#include <iostream>
#include <functional>

#include "SyncQueue.hpp"
#include "svc/codec_api.h"
extern "C"
{
    #include "libavcodec/avcodec.h"
    #include "libavformat/avformat.h"
}

using H264DecoderThread = std::shared_ptr<std::thread>;
using H264PacketQueue = std::shared_ptr<SyncQueue<AVPacket>>;
using NotifySVCEncoderCB= std::function<void (bool eof, int status, AVFrame *decodedFrame)>;

class H264Decoder {
public:
    H264Decoder(int MaxSize);
    
    ~H264Decoder();
    
    int initH264Decoder(AVStream *stream);
    
    int start(NotifySVCEncoderCB callback);
    
    void put(AVPacket &&pkt);
    
    void interrupt();
    
    void stop();
    
private:
    bool decoderInitialized_;
    
    AVCodecContext *h264Decoder_;
    
    H264PacketQueue h264PacketQueue_;

    H264DecoderThread h264DecoderThread_;
};
#endif /* H264Decoder_hpp */
