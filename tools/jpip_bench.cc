// Warm-cache index opening and a viewport/playback/zoom response sequence.
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/resource.h>

#include "server/channel_engine.h"
#include "jpip/request/request.h"

using Clock = std::chrono::steady_clock;
static double elapsed(Clock::time_point start) {
    return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
}
static uint64_t peak_rss() {
    rusage usage = {};
    if (getrusage(RUSAGE_SELF,&usage) != 0) std::exit(1);
#ifdef __APPLE__
    return usage.ru_maxrss;
#else
    return static_cast<uint64_t>(usage.ru_maxrss)*1024;
#endif
}
int main(int argc,char **argv) {
    if (argc<4 || argc>5) {
        std::cerr << "usage: jpip_bench open|response image-directory target [open-iterations]\n";
        return 2;
    }
    const std::string mode=argv[1];
    std::string directory=argv[2];
    if (directory.empty()) return 2;
    if (directory.back()!='/') directory+='/';
    if (mode=="open") {
        int iterations=argc==5 ? std::atoi(argv[4]) : 10;
        if(iterations<=0)return 2;
        size_t streams=0;
        Clock::time_point start=Clock::now();
        for(int i=0;i<iterations;++i) {
            server::FileManager files;
            if(!files.Init(directory) || files.OpenImage(argv[3])!=server::FileManager::OpenResult::OPENED)return 1;
            streams=files.GetImage()->GetNumCodestreams();
        }
        double time=elapsed(start)/iterations;
        std::cout << "{\"open_ms\":" << time << ",\"streams\":" << streams
                  << ",\"peak_rss_bytes\":" << peak_rss() << "}\n";
        return 0;
    }
    if(mode!="response" || argc!=4)return 2;
    server::ChannelEngine engine(64000);
    if(!engine.Init(directory))return 1;
    Clock::time_point start=Clock::now();
    if(engine.Open(argv[3])!=server::FileManager::OpenResult::OPENED)return 1;
    double opened=elapsed(start);
    std::cout << "{\"open_ms\":" << opened << ",\"phases\":[";
    for(int phase=0;phase<6;++phase) {
        int stream=phase==5?0:phase;
        int width=phase==5?4096:1024;
        jpip::Request request;
        std::string error;
        if(!request.ParseTarget("/jpip?stream="+std::to_string(stream)+"&fsiz="+
              std::to_string(width)+","+std::to_string(width)+",closest",&error))return 1;
        uint64_t bytes=0,hash=14695981039346656037ULL;
        start=Clock::now();
        if(!engine.Begin(request,false,&error)){std::cerr<<error<<'\n';return 1;}
        for(;;) {
            char buffer[64000];int length=0;
            auto result=engine.Generate(buffer,sizeof buffer,&length);
            if(result==server::ChannelEngine::GenerateResult::FAILED){std::cerr<<engine.GetError()<<'\n';return 1;}
            bytes+=length;
            for(int i=0;i<length;++i)hash=(hash^static_cast<unsigned char>(buffer[i]))*1099511628211ULL;
            if(result==server::ChannelEngine::GenerateResult::COMPLETE)break;
        }
        double time=elapsed(start);
        if(phase)std::cout<<',';
        std::cout << "{\"phase\":" << phase << ",\"ms\":" << time
                  << ",\"bytes\":" << bytes << ",\"fnv64\":\"" << hash << "\"}";
    }
    std::cout << "],\"peak_rss_bytes\":" << peak_rss() << "}\n";
}
