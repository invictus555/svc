//
//  localize.cpp
//  svc
//
//  Created by Asterisk on 3/19/21.
//
#include <unistd.h>
#include <sys/stat.h>
#include "Localize.hpp"

Localize::Localize(std::string &filePath):filePath_(filePath), handler_(NULL){}
            
Localize::Localize(std::string &dir, std::string &fileName): handler_(nullptr) {
    auto tempDir = dir;
    if (access(tempDir.c_str(), F_OK) == -1) {
        mkdir(tempDir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    }
    
    filePath_ = tempDir.append("/").append(fileName);
}

Localize::~Localize(){
    if (handler_) {
        fclose(handler_);
    }
    
    handler_ = NULL;
}

int Localize::open() {
    if (filePath_.empty()) {
        return -1;
    }
    
    handler_ = fopen(filePath_.c_str(), "w+");
    return 0;
}

int Localize::write(unsigned char **ppDst, int stride, int width, int height) {
    auto written = 0;
    auto yBuf = *ppDst, uBuf = *(ppDst + 1), vBuf = *(ppDst + 2);
    auto ret = write(yBuf, stride, width, height);
    if (ret < 0) {
        return ret;
    }
    
    written += ret;
    ret = write(uBuf, stride >> 1, width >> 1, height >> 1);
    if (ret < 0) {
        return ret;
    }
    
    written += ret;
    write(vBuf, stride >> 1, width >> 1, height >> 1);
    if (ret < 0) {
        return ret;
    }
    
    written += ret;
    return written;
}

int Localize::write(const unsigned char *buffer, int size) {
    if (buffer == NULL || size <= 0) {
        return -1;
    }
    
    if (!handler_) {
        return -2;
    }
    
    auto written = 0;
    do {
        auto ret = fwrite(buffer + written, sizeof(unsigned char), size - written, handler_);
        if (ret <= 0) {
            break;
        }
        
        written += ret;
    } while (written < size);
        
    return written;
}

int Localize::write(const unsigned char *buf, int stride, int width, int height) {
    auto written = 0, bufferOffset = 0;
    if (stride != width) {
        for (auto i = 0; i < height; i++) {
            auto ret = fwrite(buf + bufferOffset, sizeof(unsigned char), width, handler_);
            if (ret < 0) {
                break;
            }
            
            written += width;
            bufferOffset += stride;
        }
    } else {
        written = write(buf, stride * height);
    }
    
    return written;
}

Localize &Localize::flush() {
    if (handler_) {
        fflush(handler_);
    }
    
    return *this;
}

void Localize::close() {
    if (handler_) {
        fclose(handler_);
    }
    
    handler_ = NULL;
}
