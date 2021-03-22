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
    Localize(std::string filePath);
    ~Localize();
    
    int open();
    int write(const unsigned char *buffer, int size);
    void flush();
    void close();
private:
    FILE  *handler_;
    std::string filePath_;
};

#endif /* Localize_hpp */
