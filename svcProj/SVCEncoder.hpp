//
//  SVCEncoder.hpp
//  svc
//
//  Created by Asterisk on 3/20/21.
//

#ifndef SVCEncoder_hpp
#define SVCEncoder_hpp

#include <stdio.h>
#include <thread>
#include <vector>
#include <memory>
#include <iostream>
#include "SyncQueue.hpp"
#include "svc/codec_api.h"

struct SpatialData {
    int width;
    int height;
    int bitrate;
};

using SpatialData = struct SpatialData;
using EncoderThread = std::shared_ptr<std::thread>;
using PictureQueue = std::shared_ptr<SyncQueue<SSourcePicture>>;
using NotifySVCDecoderCB= std::function<void (bool eof, int status, SFrameBSInfo *pEncodedInfo)>;

class SVCEncoder {
public:
    SVCEncoder(int maxSize);
    
    ~SVCEncoder();
    
    int initSVCEncoder(int width, int height, int spatialNum, std::vector<SpatialData> &spatials);
    
    int initSVCEncoder(int width, int height, int bitrate, int temporalNum);

    int start(NotifySVCDecoderCB notifySVCDecoder);
    
    void put(SSourcePicture && sourcePic);
    
    void interrupt();
    
    void stop();
            
private:
    bool encoderInitialized_;
    
    ISVCEncoder *svcEncoder_;
    
    PictureQueue pictureQueue_;

    EncoderThread encoderThread_;
};
#endif /* SVCEncoder_hpp */
