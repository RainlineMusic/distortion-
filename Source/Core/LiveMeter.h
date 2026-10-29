#pragma once
#include "Analyzer.h"
#include <deque>
#include <mutex>

namespace distortion {
struct MeterView {
    double shortPercent=0,integratedPercent=0,seconds=0;
    bool shortValid=false,integratedValid=false;
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
// Piecewise affine gain trajectories remove level and smooth envelope changes.
// 20 ms is an explicit modelling assumption, not a universal THD separator.
inline Channels compensateEnvelope(const Channels& predicted,const Channels& output,double fs) {
    Channels residual=output;
    const auto length=output[0].size(), step=std::max(std::size_t(8),static_cast<std::size_t>(fs*.02));
    for(std::size_t c=0;c<output.size();++c) for(std::size_t first=0;first<length;first+=step) {
        const auto end=std::min(length,first+step);double aa=0,ab=0,bb=0,ay=0,by=0;
        for(auto i=first;i<end;++i) {
            const double t=2.*double(i-first)/double(end-first)-1.,p=predicted[c][i],v=p*t;
            aa+=p*p;ab+=p*v;bb+=v*v;ay+=p*output[c][i];by+=v*output[c][i];
        }
        const double determinant=aa*bb-ab*ab;
        const double gain=determinant>1.e-25?(ay*bb-by*ab)/determinant:ay/std::max(aa,1.e-30);
        const double slope=determinant>1.e-25?(by*aa-ay*ab)/determinant:0;
        // An exact two-level gain jump is identifiable without discarding a
        // whole block or waiting for the slow linear-model learner.
        std::size_t split=first;double jump=0,previous=0;bool have=false;
        for(auto i=first;i<end;++i) if(predicted[c][i]*predicted[c][i]>aa/double(end-first)*.01) {
            const double ratio=output[c][i]/predicted[c][i];
            if(have && std::abs(ratio-previous)>jump) {jump=std::abs(ratio-previous);split=i;}
            previous=ratio;have=true;
        }
        bool exactStep=false;double gains[2]={0,0};
        if(split>first && split<end && jump>.01) {
            double e=0,signal=0;
            for(int side=0;side<2;++side) {
                const auto lo=side==0?first:split,hi=side==0?split:end;double xy=0,xx=0;
                for(auto i=lo;i<hi;++i){xy+=predicted[c][i]*output[c][i];xx+=predicted[c][i]*predicted[c][i];}
                gains[side]=xy/std::max(xx,1.e-30);
                for(auto i=lo;i<hi;++i){const double d=output[c][i]-gains[side]*predicted[c][i];e+=d*d;signal+=output[c][i]*output[c][i];}
            }
            exactStep=signal>1.e-20 && e<signal*1.e-12;
        }
        for(auto i=first;i<end;++i) {
            const double t=2.*double(i-first)/double(end-first)-1.;
            if(exactStep) {residual[c][i]=output[c][i]-gains[i<split?0:1]*predicted[c][i];continue;}
            residual[c][i]=output[c][i]-(gain+slope*t)*predicted[c][i];
        }
    }
    return residual;
}
}
