//
//  main.cpp
//  SVC
//
//  Created by Asterisk on 3/15/21.
//

#include <iostream>
#include "SVCProj.hpp"

int main(int argc, const char * argv[])
{
    auto queueMaxSize = 50;
    auto spatialNum = std::min(4, MAX_SPATIAL_LAYER_NUM);
    auto temporalNum = std::min(4, MAX_TEMPORAL_LAYER_NUM);
    std::initializer_list<SpatialData> spatialData = {
        {640,   360,    600 * 1024},    // 360p 600Kb
        {854,   480,    1000 * 1024},   // 480p 1000Kb
        {1280,  720,    2000 * 1024},   // 720p 2000Kb
        {1920,  1080,   4500 * 1204}    // 1080p 4500Kb
    };
    
    std::string url = "/Users/shengchao/Projects/svcProj/football.mp4";
    std::string dumpDir = "/Users/shengchao/Projects/svcProj/dump/";
    std::shared_ptr<SVCProj> svcProj = std::make_shared<SVCProj>(temporalNum, spatialNum, spatialData);
    svcProj->start(url, dumpDir, queueMaxSize, AV_LOG_DEBUG);
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    svcProj->interrupt()->stop();
    return 0;
}
