//
//  SVCProj.hpp
//  svc
//
//  Created by Asterisk on 3/18/21.
//

#ifndef SVCProj_hpp
#define SVCProj_hpp

#include <stdio.h>
#include <iostream>
#include <vector>
#include "SVCDecoder.hpp"
#include "SVCEncoder.hpp"
#include "H264Decoder.hpp"

// ffmpeg headers
extern "C"
{
    #include "libavutil/aes.h"
    #include "libavutil/dict.h"
    #include "libswscale/swscale.h"
    #include "libavutil/imgutils.h"
    #include "libavcodec/avcodec.h"
    #include "libavformat/avformat.h"
}

using SpatialDataVec = std::vector<SpatialData>;
using SVCDecoderShr = std::shared_ptr<SVCDecoder>;
using SVCEncoderShr = std::shared_ptr<SVCEncoder>;
using ReadThreadShr = std::shared_ptr<std::thread>;
using H264DecoderShr = std::shared_ptr<H264Decoder>;
using SVCDecoderShrVec = std::vector<SVCDecoderShr>;

class SVCProj {
public:
    SVCProj(int temporalNum, int spatialNum, std::initializer_list<SpatialData> spatialList);
    
    ~SVCProj();
    
    /* url: input media like local mp4 and so on
     * dumpDir: where to store dump data
     * maxSize: the max size of aync queue
     * logLevel: reference to ffmpeg
     * RETURN: successful if 0, otherwise failed.
     */
    void start(std::string &url, std::string &dumpDir, int maxSize, int logLevel);
    
    SVCProj *interrupt();
    
    void stop();

private:
    void correctSpatialData();
        
    int openInputSourceMedia(std::string &url, int logLevel);
        
    SSourcePicture && createSSourcePicture(AVFrame *frame);
    
    void initH264Decoder();
    
    void initSVCH264Encoder(int width, int height);
    
    void initSVCH264Decoders();
        
    std::string createExtraInfo(int temporalId, int spatialId, SpatialData &data);

private:
    int svcSpatialNum_;                     // svc Spatial number
    int svcTemporalNum_;                    // svc Temporal number
    int syncQueueMaxSize_;                  // sync queue max size
    std::string dumpDataDir_;               // where dump date to store
    AVStream *h264Stream_;                  // h264 stream
    AVFormatContext *fmtCtx_;               // input media for read
    std::atomic_bool stop_;                 // to control read thread
    std::atomic_bool started_;              // redundant protection
    ReadThreadShr readThread_;              // read thread instance
    H264DecoderShr h264Decoder_;            // h264 decoder context
    SpatialDataVec spatialSettings_;        // to store all svc spatial data setting
    SVCEncoderShr svcH264Encoder_;          // svc encoder  context
    SVCDecoderShrVec svcH264Decoders_;      // all decoder about svc decoding
};

#endif /* SVCProj_hpp */
