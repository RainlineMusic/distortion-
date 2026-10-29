#pragma once
#include "Analyzer.h"
#include <deque>
#include <mutex>

namespace distortion {
constexpr std::size_t graphBands=192;
struct MeterView {
    std::array<float,graphBands> before{},after{},residual{};
    double shortPercent=0,integratedPercent=0,seconds=0;
    bool spectrumValid=false,residualValid=false,shortValid=false,integratedValid=false;
    MeterView() { before.fill(-120); after.fill(-120); residual.fill(-120); }
};
// Worker-only energy accounting; every sample enters Integrated exactly once.
class EnergyMeter {
public:
    explicit EnergyMeter(std::size_t windowLength):length(windowLength) {}
    void reset() { samples.clear(); error=signal=totalError=totalSignal=0; count=0; }
    void resetShort() {samples.clear();error=signal=0;}
    void resetIntegrated() {totalError=totalSignal=0;count=0;}
    void add(double e,double s) {
        samples.push_back({e,s}); error+=e;signal+=s;totalError+=e;totalSignal+=s;++count;
        if(samples.size()>length) { error-=samples.front().first;signal-=samples.front().second;samples.pop_front(); }
    }
    void fill(MeterView& v,double fs) const {
        v.shortValid=samples.size()==length && signal>1.e-20;
        v.integratedValid=count>0 && totalSignal>1.e-20;
        v.shortPercent=100*std::sqrt(std::max(0.0,error)/std::max(1.e-30,signal));
        v.integratedPercent=100*std::sqrt(totalError/std::max(1.e-30,totalSignal));v.seconds=count/fs;
    }
private:
    std::size_t length,count=0;
    std::deque<std::pair<double,double>> samples;
    double error=0,signal=0,totalError=0,totalSignal=0;
};
// FFT convolution on the background meter worker, never on the audio thread.
inline Channels predictLive(const Channels& x,const PredictionModel& model,std::size_t rows) {
    FFT fft(nextPowerOfTwo(rows+2*model.taps-2));
    std::vector<std::vector<Complex>> spectra(2,std::vector<Complex>(fft.n));
    for(std::size_t c=0;c<2;++c) { for(std::size_t i=0;i<x[c].size();++i) spectra[c][i]=x[c][i];fft.transform(spectra[c]); }
    Channels out(model.kernels.size(),std::vector<double>(rows));
    for(std::size_t c=0;c<out.size();++c) {
        std::vector<Complex> sum(fft.n),h(fft.n);
        for(std::size_t input=0;input<2;++input) {
            std::fill(h.begin(),h.end(),Complex{});
            for(std::size_t k=0;k<model.taps;++k) h[k]=model.kernels[c][input*model.taps+k];
            fft.transform(h);for(std::size_t k=0;k<fft.n;++k)sum[k]+=h[k]*spectra[input][k];
        }
        fft.transform(sum,true);
        for(std::size_t i=0;i<rows;++i)out[c][i]=sum[i+model.taps-1].real()+model.kernels[c].back();
    }
    return out;
}
inline std::array<float,graphBands> spectrum(const Channels& x,double fs) {
    const auto length=x[0].size();FFT fft(nextPowerOfTwo(length));
    std::vector<double> power(fft.n/2+1);std::vector<Complex> bins(fft.n);
    double windowPower=0;
    for(std::size_t c=0;c<x.size();++c) {
        std::fill(bins.begin(),bins.end(),Complex{});
        for(std::size_t i=0;i<length;++i) {const double w=.5-.5*std::cos(2*pi*i/(length-1));bins[i]=x[c][i]*w;if(c==0)windowPower+=w*w;}
        fft.transform(bins);for(std::size_t i=1;i<power.size();++i)power[i]+=2*std::norm(bins[i])/(fft.n*windowPower*x.size());
    }
    std::array<float,graphBands> result{};
    for(std::size_t b=0;b<graphBands;++b) {
        const double frequency=20*std::pow(1000.0,double(b)/(graphBands-1));
        auto lo=std::max(std::size_t(1),std::size_t(frequency/std::pow(2.,1./12)*fft.n/fs));
        auto hi=std::min(power.size()-1,std::size_t(frequency*std::pow(2.,1./12)*fft.n/fs));
        double energy=0;for(auto i=lo;i<=hi && i<power.size();++i)energy+=power[i];
        result[b]=float(std::max(-120.,10*std::log10(std::max(energy,1.e-12))));
    }
    return result;
}
}
