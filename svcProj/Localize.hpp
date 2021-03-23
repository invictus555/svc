//
//  localize.hpp
//  svc
//
//  Created by Asterisk on 3/19/21.
//

#ifndef Localize_hpp
#define Localize_hpp

#include <stdio.h>
#include <iostream>

using namespace std;

class Localize
{
public:
    Localize(std::string &filePath);
    
    Localize(std::string &dir, std::string &fileName);
    
    ~Localize();
    
    int open();
    
    int write(const unsigned char *buffer, int size);
    
    int write(unsigned char **ppDst, int stride, int width, int height);
    
    int write(const unsigned char *buf, int stride, int width, int height);

    Localize &flush();
    
    void close();
    
    
private:
    FILE  *handler_;
    std::string filePath_;
};

#endif /* Localize_hpp */
