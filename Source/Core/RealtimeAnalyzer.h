#pragma once
#include "LiveMeter.h"
#include "ReadingContinuity.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace distortion {
struct CaptureFrame { float x0=0, x1=0, y0=0, y1=0; std::uint64_t epoch=0; };
// One producer (host audio thread), one consumer (analysis worker). Indices
// publish ownership of the non-atomic payload; no seqlock on plain data.
class CaptureQueue {
public:
    void prepare(std::size_t capacity) {
        storage.resize(nextPowerOfTwo(capacity)); mask=storage.size()-1;
        write.store(0); read.store(0);
    }
    template<class Sample>
    bool push(const Sample* x0,const Sample* x1,const Sample* y0,const Sample* y1,
              std::size_t count,std::uint64_t epoch) noexcept {
        const auto w=write.load(std::memory_order_relaxed), r=read.load(std::memory_order_acquire);
        if (storage.empty() || count>storage.size()-(w-r)) return false;
        for (std::size_t i=0;i<count;++i)
            storage[(w+i)&mask]={static_cast<float>(x0[i]),static_cast<float>(x1[i]),
                                static_cast<float>(y0[i]),static_cast<float>(y1[i]),epoch};
        write.store(w+count,std::memory_order_release);
        return true;
    }
    template<class Consumer>
    std::size_t consume(Consumer&& consumeFrame) {
        const auto r=read.load(std::memory_order_relaxed), w=write.load(std::memory_order_acquire);
        for (auto i=r;i<w;++i) consumeFrame(storage[i&mask]);
        read.store(w,std::memory_order_release);
        return w-r;
    }
    bool hasPending() const noexcept {
        return read.load(std::memory_order_relaxed)!=write.load(std::memory_order_acquire);
    }
private:
    std::vector<CaptureFrame> storage;
    std::size_t mask=0;
    alignas(64) std::atomic<std::size_t> write{0};
    alignas(64) std::atomic<std::size_t> read{0};
};

class RealtimeAnalyzer {
public:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,"64-bit atomics required");
    static_assert(std::atomic<double>::is_always_lock_free,"Lock-free result atomics required");
    ~RealtimeAnalyzer() { stop(); }
    RealtimeAnalyzer()=default;
    RealtimeAnalyzer(const RealtimeAnalyzer&)=delete;
    RealtimeAnalyzer& operator=(const RealtimeAnalyzer&)=delete;
    // Host lifecycle only, with processBlock stopped.
    void prepare(double sampleRate) {
        stop();
        config=Config(sampleRate);
        meterQueue.prepare(static_cast<std::size_t>(sampleRate*4));
        queue.prepare(std::max(config.requiredSamples()*2,static_cast<std::size_t>(sampleRate*16)));
        epoch.fetch_add(1,std::memory_order_acq_rel);
        fallback.store(static_cast<int>(Status::measuring));
        active=false; quietReference=quietOutput=0; running.store(true);
        worker=std::thread([this] { run(); });
        meterWorker=std::thread([this] { runMeter(); });
    }
    void stop() {
        running.store(false,std::memory_order_release);
        if (worker.joinable()) worker.join();
        if (meterWorker.joinable()) meterWorker.join();
        fallback.store(static_cast<int>(Status::measuring));
        epoch.fetch_add(1,std::memory_order_acq_rel);
    }
    void invalidate(Status reason=Status::measuring) noexcept {
        fallback.store(static_cast<int>(reason),std::memory_order_relaxed);
        epoch.fetch_add(1,std::memory_order_release); active=false;
    }
    template<class Sample>
    void push(const Sample* x0,const Sample* x1,const Sample* y0,const Sample* y1,
              std::size_t count,bool connected=true) noexcept {
        if (!running.load(std::memory_order_relaxed) || count==0) return;
        if (!connected || x0==nullptr || x1==nullptr) { invalidate(Status::noReference); return; }
        double ex=0,ey=0;
        for (std::size_t i=0;i<count;++i) {
            const double a=x0[i],b=x1[i],c=y0[i],d=y1[i];
            if (!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(c)||!std::isfinite(d)) { invalidate(Status::invalid); return; }
            ex+=a*a+b*b; ey+=c*c+d*d;
        }
        // Preserve the actual zero samples in short musical gaps. One quiet
        // host block must not restart the entire 1.65-second learning window.
        const auto quietLimit=static_cast<std::size_t>(config.sampleRate*0.25);
        quietReference=ex/count<2.e-14?std::min(quietLimit,quietReference+count):0;
        quietOutput=ey/count<2.e-14?std::min(quietLimit,quietOutput+count):0;
        if (quietReference>=quietLimit || quietOutput>=quietLimit) {
            const auto reason=quietReference>=quietLimit?Status::noReference:Status::silence;
            if (active || fallback.load(std::memory_order_relaxed)!=static_cast<int>(reason)) invalidate(reason);
            return;
        }
        if (!active) { invalidate(Status::measuring); active=true; }
        const auto tag=epoch.load(std::memory_order_relaxed);
        if (!meterQueue.push(x0,x1,y0,y1,count,tag)) invalidate(Status::overload);
        if (!queue.push(x0,x1,y0,y1,count,tag)) invalidate(Status::overload);
    }
    MeterView meterSnapshot() const { std::lock_guard<std::mutex> lock(meterMutex); return view; }
    bool tryMeterSnapshot(MeterView& destination) const {
        std::unique_lock<std::mutex> lock(meterMutex,std::try_to_lock);
        if(!lock.owns_lock())return false; destination=view;return true;
    }
    void resetIntegrated() noexcept { resetMeter.fetch_add(1); }
    Result snapshot() const noexcept {
        Result result;
        result.status=static_cast<Status>(fallback.load(std::memory_order_relaxed));
        const auto current=epoch.load(std::memory_order_acquire);
        for (int attempt=0;attempt<3;++attempt) {
            const auto first=publication.load();
            if (first&1) continue;
            Result candidate;
            candidate.status=static_cast<Status>(status.load());
            candidate.method=static_cast<Method>(method.load());
            candidate.percent=percent.load();
            candidate.frequencyHz=frequency.load();
            candidate.delaySamples=delay.load();
            candidate.harmonics=harmonics.load();
            const auto tagged=publishedEpoch.load();
            const auto last=publication.load();
            if (first==last && !(last&1) && tagged==current && current==epoch.load(std::memory_order_acquire))
                return candidate;
        }
        return result;
    }
private:
    // Sequentially consistent payload and version atomics keep the UI snapshot
    // ordered on ARM as well as x86. Only the worker and UI use these fields.
    void publish(const Result& result,std::uint64_t tag) noexcept {
        publication.fetch_add(1);
        status.store(static_cast<int>(result.status));
        method.store(static_cast<int>(result.method));
        percent.store(result.percent);
        frequency.store(result.frequencyHz);
        delay.store(result.delaySamples);
        harmonics.store(result.harmonics);
        publishedEpoch.store(tag);
        publication.fetch_add(1);
    }
    void run() {
        const auto needed=config.requiredSamples();
        std::vector<CaptureFrame> history(needed);
        std::size_t writeAt=0,filled=0,sinceAnalysis=0;
        std::uint64_t tag=0;
        auto lastCapture=std::chrono::steady_clock::now();
        bool timedOut=false;
        Analyzer analyzer(config);
        ReadingContinuity continuity;
        auto nextFit=std::chrono::steady_clock::now();
        while (running.load(std::memory_order_acquire)) {
            const auto consumed=queue.consume([&](const CaptureFrame& frame) {
                if (frame.epoch!=epoch.load(std::memory_order_acquire)) return;
                if (frame.epoch!=tag) { tag=frame.epoch; writeAt=filled=sinceAnalysis=0; }
                history[writeAt]=frame; writeAt=(writeAt+1)%needed;
                filled=std::min(filled+1,needed); ++sinceAnalysis;
            });
            if (consumed>0) { lastCapture=std::chrono::steady_clock::now(); timedOut=false; }
            if (std::chrono::steady_clock::now()>=nextFit && filled==needed && sinceAnalysis>=static_cast<std::size_t>(config.sampleRate*0.5)
                && tag==epoch.load(std::memory_order_acquire)) {
                Channels x(2,std::vector<double>(needed)),y=x;
                for (std::size_t i=0;i<needed;++i) {
                    const auto& frame=history[(writeAt+i)%needed];
                    x[0][i]=frame.x0; x[1][i]=frame.x1; y[0][i]=frame.y0; y[1][i]=frame.y1;
                }
                Result result;
                try {
                    PredictionModel learned; result=analyzer.analyse(x,y,&learned);
                    if(result.valid() && learned.valid() && tag==epoch.load()) {
                        std::lock_guard<std::mutex> lock(modelMutex); liveModel=std::move(learned); modelEpoch=tag;
                    }
                }
                catch (...) { result.status=Status::invalid; }
                if (tag==epoch.load(std::memory_order_acquire) && running.load(std::memory_order_acquire))
                    publish(continuity.update(result,tag),tag);
                sinceAnalysis=0;
                nextFit=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            }
            // Hosts may stop calling processBlock entirely at transport stop.
            if (!timedOut && !queue.hasPending()
                && std::chrono::steady_clock::now()-lastCapture>std::chrono::milliseconds(1600)) {
                Result stale; publish(stale,tag); filled=0; sinceAnalysis=0; timedOut=true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    void runMeter() {
        const auto hop=static_cast<std::size_t>(config.sampleRate*.1);
        const auto margin=static_cast<std::size_t>(config.maxDelay)+2*config.taps;
        std::deque<CaptureFrame> history;
        std::uint64_t tag=0,reset=resetMeter.load();
        EnergyMeter energies(static_cast<std::size_t>(config.sampleRate*3));
        auto last=std::chrono::steady_clock::now();
        while(running.load()) {
            meterQueue.consume([&](const CaptureFrame& f) {
                if(f.epoch!=epoch.load())return;
                if(tag!=f.epoch) {tag=f.epoch;history.clear();energies.resetShort();}
                history.push_back(f);last=std::chrono::steady_clock::now();
            });
            if(reset!=resetMeter.load()) {reset=resetMeter.load();energies.resetIntegrated();}
            while(history.size()>=2*margin+hop) {
                PredictionModel model;
                {std::lock_guard<std::mutex> lock(modelMutex);if(modelEpoch==tag)model=liveModel;}
                Channels y(2,std::vector<double>(hop)),e=y;
                for(std::size_t i=0;i<hop;++i) {
                    const auto& f=history[margin+i];y[0][i]=f.y0;y[1][i]=f.y1;
                }
                MeterView next;
                if(model.valid()) {
                    Channels input(2,std::vector<double>(hop+model.taps-1));
                    const auto first=static_cast<std::ptrdiff_t>(margin)-model.delay+static_cast<std::ptrdiff_t>(model.taps/4)-static_cast<std::ptrdiff_t>(model.taps-1);
                    for(std::size_t i=0;i<input[0].size();++i) {const auto& f=history[static_cast<std::size_t>(first)+i];input[0][i]=f.x0;input[1][i]=f.x1;}
                    const auto predicted=predictLive(input,model,hop);
                    e=compensateEnvelope(predicted,y,config.sampleRate);
                    double means[2]={0,0};
                    for(std::size_t c=0;c<2;++c) means[c]=std::accumulate(y[c].begin(),y[c].end(),0.)/hop;
                    for(std::size_t i=0;i<hop;++i) {
                        double err=0,sig=0;
                        for(std::size_t c=0;c<2;++c) {err+=e[c][i]*e[c][i];sig+=(y[c][i]-means[c])*(y[c][i]-means[c]);}
                        energies.add(err,sig);
                    }
                    
                }
                energies.fill(next,config.sampleRate);
                if(!model.valid())next.shortValid=false;
                {std::lock_guard<std::mutex> lock(meterMutex);view=next;}
                history.erase(history.begin(),history.begin()+static_cast<std::ptrdiff_t>(hop));
            }
            if(tag!=epoch.load() || std::chrono::steady_clock::now()-last>std::chrono::milliseconds(1600)) {
                MeterView paused;energies.fill(paused,config.sampleRate);paused.shortValid=false;
                std::lock_guard<std::mutex> lock(meterMutex);view=paused;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    mutable std::mutex meterMutex;
    std::mutex modelMutex;
    MeterView view;
    PredictionModel liveModel;
    std::uint64_t modelEpoch=0;
    std::atomic<std::uint64_t> resetMeter{0};
    CaptureQueue meterQueue;
    std::thread meterWorker;
    Config config;
    CaptureQueue queue;
    std::thread worker;
    bool active=false; // audio-thread ownership; lifecycle writes only when stopped
    std::size_t quietReference=0,quietOutput=0;
    std::atomic<bool> running{false};
    std::atomic<std::uint64_t> epoch{1}, publishedEpoch{0}, publication{0};
    std::atomic<int> fallback{0},status{0},method{0},delay{0},harmonics{0};
    std::atomic<double> percent{0},frequency{0};
};
}
