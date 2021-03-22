//
//  localize.cpp
//  svc
//
//  Created by Asterisk on 3/19/21.
//

#include "Localize.hpp"

Localize::Localize(std::string filePath):filePath_(filePath), handler_(NULL){}
                                                   
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

int Localize::write(const unsigned char *buffer, int size) {
    if (buffer == NULL || size <= 0) {
        return -1;
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

void Localize::flush() {
    if (handler_) {
        fflush(handler_);
    }
}

void Localize::close() {
    if (handler_) {
        fclose(handler_);
    }
    
    handler_ = NULL;
}
