#include "Core/RealtimeAnalyzer.h"
#include "Core/TransportTracker.h"
#include <cstdlib>
#include <iostream>
#include <new>
#include <random>

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
    Channels source(2,std::vector<double>(4800)), changed=source;
    std::mt19937 randomSource(42);std::normal_distribution<double> normal(0,.15);
    for(auto& channel:source)for(auto& sample:channel)sample=normal(randomSource);
    auto ratio=[&](const Channels& y){auto e=compensateEnvelope(source,y,48000);double a=0,b=0;for(int c=0;c<2;++c){a+=energy(e[c]);b+=energy(y[c]);}return 100*std::sqrt(a/b);};
    for(std::size_t c=0;c<2;++c)for(std::size_t i=0;i<4800;++i)changed[c][i]=source[c][i]*(i<1371?.5:2.);
    require(ratio(changed)<1.e-8,"instant level jump produces no residual");
    for(std::size_t c=0;c<2;++c)for(std::size_t i=0;i<4800;++i)changed[c][i]=source[c][i]*(.2+double(i)/4800);
    require(ratio(changed)<1.e-8,"gain ramp produces no residual");
    for(std::size_t c=0;c<2;++c)for(std::size_t i=0;i<4800;++i)changed[c][i]=std::tanh(source[c][i]*4)/4;
    require(ratio(changed)>5,"saturation survives envelope compensation");
    PredictionModel convolution;convolution.taps=32;convolution.kernels=Channels(2,std::vector<double>(65));
    Channels convolutionInput(2,std::vector<double>(95));
    for(auto& channel:convolutionInput)for(auto& sample:channel)sample=normal(randomSource);
    for(int pass=0;pass<3;++pass) {
        for(auto& kernel:convolution.kernels) {std::fill(kernel.begin(),kernel.end(),0.);for(std::size_t k=0;k<(pass==0?2:64);++k)kernel[k]=normal(randomSource);kernel.back()=.03;}
        const auto calculated=predictLive(convolutionInput,convolution,64);
        double maximumError=0;
        for(std::size_t c=0;c<2;++c)for(std::size_t i=0;i<64;++i) {
            double expected=convolution.kernels[c].back();
            for(std::size_t input=0;input<2;++input)for(std::size_t k=0;k<32;++k) expected+=convolution.kernels[c][input*32+k]*convolutionInput[input][i+31-k];
            maximumError=std::max(maximumError,std::abs(expected-calculated[c][i]));
        }
        require(maximumError<1.e-10,"sparse, FFT and changed-kernel convolution agree with direct reference");
    }
    TransportTracker transport;
    for (int i=0;i<200;++i) require(!transport.update(i*1024+(i%3)-1,true,1024),"48k / 1024 rounded position is continuous");
    require(transport.update(0,true,1024),"real backward seek invalidates");
    transport.reset();
    for (int i=0;i<200;++i) require(!transport.update((i/2)*2048,true,1024),"repeated host timestamps are continuous");
    require(transport.update(400000,true,1024),"real forward seek invalidates");
    require(transport.update(400000,false,1024),"transport stop invalidates");
    ReadingContinuity continuity; Result good; good.status=Status::valid; good.percent=5;
    const auto now=ReadingContinuity::Clock::now();
    require(continuity.update(good,1,now).valid(),"valid reading accepted");
    require(continuity.update(Result{},1,now+std::chrono::seconds(1)).valid(),"brief refit does not blank the number");
    require(!continuity.update(Result{},1,now+std::chrono::seconds(3)).valid(),"held reading expires; never stays stale indefinitely");
    continuity.update(good,1,now);
    require(!continuity.update(Result{},2,now).valid(),"seek never reuses an old-epoch result");
    continuity.update(good,2,now); Result missing; missing.status=Status::noReference;
    require(!continuity.update(missing,2,now).valid(),"missing reference is never held");
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

    EnergyMeter stats(30); MeterView statsView;
    for(int i=0;i<30;++i)stats.add(.001,1);
    stats.fill(statsView,10);
    require(statsView.shortValid && std::abs(statsView.shortPercent-3.16227766)<1.e-6,"minus 30 dB equals 3.162 percent");
    for(int i=0;i<30;++i)stats.add(.04,4);
    stats.fill(statsView,10);
    require(std::abs(statsView.shortPercent-10)<1.e-8,"Short uses only last 3 seconds");
    require(std::abs(statsView.integratedPercent-100*std::sqrt(.041/5))<1.e-8,"Integrated weights energies rather than percentages");
    stats.resetIntegrated();stats.fill(statsView,10);
    require(!statsView.integratedValid && statsView.shortValid,"Integrated reset preserves Short");
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
    // Intermittent 21 ms gaps used to reset every window before it could fill.
    // Feed the exact reported host configuration, including a quiet block every
    // 683 ms. Use broadband material so this checks acquisition, not sine mode.
    meter.prepare(48000);
    std::mt19937 rng(188); std::uniform_real_distribution<float> random(-.3f,.3f);
    std::vector<float> pre(1024),post(1024);
    int visible=0;
    for (int blockIndex=0;blockIndex<360;++blockIndex) {
        for (std::size_t i=0;i<pre.size();++i) { pre[i]=blockIndex%32==0?0.0f:random(rng); post[i]=pre[i]*.7f; }
        forbidAllocation=true; meter.push(pre.data(),pre.data(),post.data(),post.data(),pre.size()); forbidAllocation=false;
        std::this_thread::sleep_for(std::chrono::milliseconds(22));
        if (blockIndex>100 && meter.snapshot().valid()) ++visible;
    }
    const auto gapDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (!meter.snapshot().valid() && std::chrono::steady_clock::now()<gapDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    require(meter.snapshot().valid(),"48k / 1024 acquires despite recurring short gaps");
    const auto live=meter.meterSnapshot();
    require(live.shortValid && live.shortPercent<0.001,"3 second live null/gain meter");
    require(live.integratedValid && live.seconds>3,"integrated counts measured samples");
    std::fill(pre.begin(),pre.end(),0.0f);
    meter.push(pre.data(),pre.data(),pre.data(),pre.data(),pre.size());
    require(meter.snapshot().valid(),"one 1024-sample quiet block does not erase the number");
    for (int i=0;i<12;++i) meter.push(pre.data(),pre.data(),pre.data(),pre.data(),pre.size());
    require(!meter.snapshot().valid(),"sustained silence clears the reading");
    meter.push<float>(nullptr,nullptr,post.data(),post.data(),1024,false);
    require(meter.snapshot().status==Status::noReference,"explicit sidechain disconnect still clears immediately");
    require(callbackAllocations==0,"quiet-gap path has no callback allocations");
    meter.stop();
    for (int i=0;i<4;++i) { meter.prepare(i%2==0?44100:96000); meter.stop(); }
    std::cout<<"Realtime failures="<<failures<<" callback allocations="<<callbackAllocations<<" visible polls during gap test="<<visible<<'\n';
    return failures==0?EXIT_SUCCESS:EXIT_FAILURE;
}
