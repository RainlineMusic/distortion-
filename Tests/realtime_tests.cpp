#include "Core/RealtimeAnalyzer.h"
#include <cstdlib>
#include <iostream>
#include <new>

using namespace distortion;
namespace {
int failures=0;
thread_local bool forbidAllocation=false;
thread_local std::size_t callbackAllocations=0;
void require(bool ok,const char* message) { if (!ok) { ++failures; std::cerr<<"FAIL: "<<message<<'\n'; } }
}
void* operator new(std::size_t size) {
    if (forbidAllocation) ++callbackAllocations;
    if (void* p=std::malloc(size==0?1:size)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p,std::size_t) noexcept { ::operator delete(p); }
int main() {
    CaptureQueue queue; queue.prepare(64);
    require(!queue.hasPending(),"empty capture queue");
    float x[37]{}; for (int i=0;i<37;++i) x[i]=static_cast<float>(i);
    require(queue.push(x,x,x,x,37,11),"initial push");
    require(queue.hasPending(),"queued capture remains fresh during analysis");
    require(!queue.push(x,x,x,x,37,12),"bounded overflow");
    std::size_t at=0;
    require(queue.consume([&](const CaptureFrame& f) { require(f.epoch==11 && f.x0==static_cast<float>(at++),"queue payload"); })==37,"consume count");
    require(!queue.hasPending(),"consumed capture queue");
    for (int pass=0;pass<64;++pass) {
        require(queue.push(x,x,x,x,37,13),"wrapped push"); at=0;
        queue.consume([&](const CaptureFrame& f) { require(f.x0==static_cast<float>(at++),"wrapped payload"); });
    }
    // Concurrent ownership test exercises payload visibility across wraparound.
    queue.prepare(1024);
    std::atomic<bool> done{false};
    std::atomic<int> corrupt{0};
    std::thread consumer([&] {
        std::uint64_t last=0;
        while (!done.load() || last<10000) queue.consume([&](const CaptureFrame& f) {
            if (f.epoch!=last+1 || f.x0!=static_cast<float>(last+1)) ++corrupt;
            last=f.epoch;
        });
    });
    for (std::uint64_t k=1;k<=10000;++k) { float v=static_cast<float>(k); while (!queue.push(&v,&v,&v,&v,1,k)) std::this_thread::yield(); }
    done.store(true); consumer.join(); require(corrupt.load()==0,"SPSC concurrent integrity");

    RealtimeAnalyzer meter; meter.prepare(48000);
    Config cfg; const auto count=cfg.requiredSamples();
    std::vector<float> dry(count+2048),wet(dry.size());
    for (std::size_t i=0;i<dry.size();++i) { const double p=2*pi*997.3*i/48000; dry[i]=static_cast<float>(0.35*std::sin(p)); wet[i]=static_cast<float>(0.35*(std::sin(p)+0.05*std::sin(2*p))); }
    const int sizes[]{1,7,32,64,127,256,511,1024};
    std::size_t offset=0; int block=0;
    while (offset<dry.size()) {
        const auto n=std::min(static_cast<std::size_t>(sizes[block++%8]),dry.size()-offset);
        forbidAllocation=true;
        meter.push(dry.data()+offset,dry.data()+offset,wet.data()+offset,wet.data()+offset,n);
        forbidAllocation=false;
        offset+=n;
    }
    require(callbackAllocations==0,"capture performs no new/new[] allocations");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    auto result=meter.snapshot();
    while (!result.valid() && std::chrono::steady_clock::now()<deadline) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); result=meter.snapshot(); }
    require(result.valid() && std::abs(result.percent-5)<0.001,"worker and float capture harmonic calibration");
    meter.invalidate(); require(!meter.snapshot().valid(),"seek immediately invalidates old reading");
    meter.push<float>(nullptr,nullptr,wet.data(),wet.data(),256,false);
    require(meter.snapshot().status==Status::noReference,"missing sidechain");
    meter.stop();
    for (int i=0;i<4;++i) { meter.prepare(i%2==0?44100:96000); meter.stop(); }
    std::cout<<"Realtime failures="<<failures<<" callback allocations="<<callbackAllocations<<'\n';
    return failures==0?EXIT_SUCCESS:EXIT_FAILURE;
}
