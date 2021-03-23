//
//  SVCDecoder.hpp
//  svc
//
//  Created by Asterisk on 3/19/21.
//

#ifndef SVCDecoder_hpp
#define SVCDecoder_hpp

#include <list>
#include <thread>
#include <atomic>
#include <memory>
#include <stdio.h>
#include <stdarg.h>
#include <iostream>

#include "Localize.hpp"
#include "SyncQueue.hpp"
#include "svc/codec_api.h"

struct SVCH264Data {
    long long timestamp;
    int compressedDataLen;
    unsigned char * compressedData;
};

class SVCDecoder;
using uchar = unsigned char;
using SVCH264Data = struct SVCH264Data;
using LocalizeShr = std::shared_ptr<Localize>;
using DecoderThread = std::shared_ptr<std::thread>;
using SVCH264DataQueue = std::shared_ptr<SyncQueue<SVCH264Data>>;
using NotifyUserCB = std::function<void (bool eof, int status, SBufferInfo *pDecodedInfo, uchar **ppDst, SVCDecoder *thiz)>;

class SVCDecoder {
 
public:
    SVCDecoder(int maxSize, std::string &dumpDir, std::string &&extraInfo);
    
    ~SVCDecoder();
    
    int initSVCDecoder();
    
    int start(NotifyUserCB callback);
        
    void put(SVCH264Data &&svcH264Data);
    
    void interrupt();
    
    void stop();
            
    LocalizeShr &dumpSvcHandler();
    
    LocalizeShr &dumpYuvHandler();
    
    const std::string &tag() ;
private:
    std::string tag_;

    LocalizeShr dumpSvcHandler_;
    
    LocalizeShr dumpYuvHandler_;
    
    bool decoderInitialized_;
    
    ISVCDecoder *svcDecoder_;                                       // for decode H264 data
        
    DecoderThread decoderThread_;

    SVCH264DataQueue svcH264DataQueue_;                                 //svc h264 date queue
};
#endif /* SVCDecoder_hpp */
