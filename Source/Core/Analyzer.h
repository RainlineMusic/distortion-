#pragma once
#include "LinearModel.h"
#include <limits>

namespace distortion {
class Analyzer {
public:
    explicit Analyzer(Config configuration = Config{}) : config(configuration) {}
    const Config config;

    Result analyse(const Channels& reference, const Channels& processed) const {
        Result result;
        if (reference.empty() || processed.empty() || reference.size()>2 || processed.size()>2) {
            result.status=Status::invalid; return result;
        }
        const auto n=reference[0].size();
        for (const auto* group : {&reference,&processed}) for (const auto& c : *group) {
            if (c.size()!=n) { result.status=Status::invalid; return result; }
            for (double v : c) if (!std::isfinite(v)) { result.status=Status::invalid; return result; }
        }
        if (n<config.requiredSamples()) return result;
        double inputEnergy=0, outputEnergy=0;
        for (const auto& c : reference) inputEnergy+=energy(c);
        for (const auto& c : processed) outputEnergy+=energy(c);
        if (inputEnergy/(n*reference.size()) < 1.e-14) { result.status=Status::noReference; return result; }
        if (outputEnergy/(n*processed.size()) < 1.e-14) { result.status=Status::silence; return result; }
        // Ignore old latency padding when identifying a test tone.
        Channels toneX(reference.size()), toneY(processed.size());
        const auto toneLength=std::min(n,config.train);
        for (std::size_t c=0; c<reference.size(); ++c) toneX[c].assign(reference[c].end()-static_cast<std::ptrdiff_t>(toneLength),reference[c].end());
        for (std::size_t c=0; c<processed.size(); ++c) toneY[c].assign(processed[c].end()-static_cast<std::ptrdiff_t>(toneLength),processed[c].end());
        auto tone=analyseTone(toneX,toneY,config);
        if (tone.valid()) return tone;

        Channels inputs=reference;
        for (auto& c : inputs) {
            const double mean=std::accumulate(c.begin(),c.end(),0.0)/n;
            for (auto& v : c) v-=mean;
        }
        // A real covariance eigendecomposition handles mono, correlated stereo,
        // channel swaps and arbitrary fixed stereo gain matrices without a
        // singular two-input normal equation.
        if (inputs.size()==2) {
            const double a=energy(inputs[0]), b=dot(inputs[0],inputs[1]), d=energy(inputs[1]);
            const double theta=0.5*std::atan2(2*b,a-d), co=std::cos(theta), si=std::sin(theta);
            for (std::size_t i=0; i<n; ++i) {
                const double l=inputs[0][i], r=inputs[1][i];
                inputs[0][i]=co*l+si*r; inputs[1][i]=-si*l+co*r;
            }
            if (energy(inputs[1])<energy(inputs[0])*1.e-8) inputs.resize(1);
        }
        result.inputRank=static_cast<int>(inputs.size());
        const auto alignment=align(inputs,processed);
        result.delaySamples=alignment.first; result.correlation=alignment.second;
        if (alignment.second<0.08) { result.status=Status::unrelated; return result; }
        if (!broadbandEnough(inputs)) { result.status=Status::insufficient; return result; }

        const auto validStart=n-static_cast<std::size_t>(config.maxDelay)-config.taps-config.validation;
        const auto trainStart=validStart-config.taps-config.train;
        const int pre=static_cast<int>(config.taps/4);
        const auto makeInput=[&](std::size_t start, std::size_t rows) {
            Channels slice(inputs.size(),std::vector<double>(rows+config.taps-1));
            const auto first=static_cast<std::ptrdiff_t>(start)-alignment.first+pre-static_cast<std::ptrdiff_t>(config.taps-1);
            for (std::size_t c=0; c<inputs.size(); ++c)
                std::copy_n(inputs[c].begin()+first,slice[c].size(),slice[c].begin());
            return slice;
        };
        auto trainingInput=makeInput(trainStart,config.train);
        auto validationInput=makeInput(validStart,config.validation);
        LinearModel model(trainingInput,config.taps,config.train);
        LinearModel heldOut(validationInput,config.taps,config.validation);
        double error=0, signal=0, trainingError=0, trainingSignal=0;
        double predictedEnergy=0, cross=0;
        for (const auto& channel : processed) {
            std::vector<double> target(channel.begin()+static_cast<std::ptrdiff_t>(trainStart),channel.begin()+static_cast<std::ptrdiff_t>(trainStart+config.train));
            const auto fit=model.fit(target,config.maxIterations);
            result.iterations=std::max(result.iterations,fit.iterations);
            result.convergence=std::max(result.convergence,fit.convergence);
            if (fit.h.empty() || !std::isfinite(fit.convergence)) { result.status=Status::invalid; return result; }
            const auto trainPrediction=model.predict(fit.h);
            const auto prediction=heldOut.predict(fit.h);
            const double trainMean=std::accumulate(target.begin(),target.end(),0.0)/target.size();
            for (std::size_t i=0; i<target.size(); ++i) {
                const double d=target[i]-trainPrediction[i];
                trainingError+=d*d; trainingSignal+=(target[i]-trainMean)*(target[i]-trainMean);
            }
            for (std::size_t i=0; i<config.validation; ++i) {
                const double v=channel[validStart+i]-trainMean, p=prediction[i]-trainMean;
                error+=(v-p)*(v-p); signal+=v*v;
                predictedEnergy+=p*p; cross+=v*p;
            }
        }
        if (signal/config.validation/processed.size()<1.e-14) { result.status=Status::silence; return result; }
        result.trainingPercent=100*std::sqrt(trainingError/std::max(trainingSignal,1.e-30));
        result.percent=100*std::sqrt(error/signal);
        result.errorRms=std::sqrt(error/(config.validation*processed.size()));
        result.outputRms=std::sqrt(signal/(config.validation*processed.size()));
        result.predictionCorrelationSquared=cross*cross/std::max(signal*predictedEnergy,1.e-30);
        // Confidence checks do not declare the residue nonlinear: they only
        // reject an unrelated pair, a stalled fit or an unstable prediction.
        if (result.predictionCorrelationSquared<0.15) result.status=Status::unrelated;
        else if (result.convergence>2.e-4 || result.percent>4*result.trainingPercent+0.5)
            result.status=Status::measuring;
        else result.status=Status::valid;
        return result;
    }
private:
    std::pair<int,double> align(const Channels& x, const Channels& y) const {
        const auto n=x[0].size();
        FFT fft(nextPowerOfTwo(2*n));
        std::vector<std::vector<Complex>> xx(x.size(),std::vector<Complex>(fft.n));
        std::vector<double> ex(x.size());
        for (std::size_t c=0; c<x.size(); ++c) {
            ex[c]=energy(x[c]);
            for (std::size_t i=0; i<n; ++i) xx[c][i]=x[c][i];
            fft.transform(xx[c]);
        }
        std::vector<Complex> yy(fft.n), cross(fft.n);
        int bestLag=0; double best=0;
        for (const auto& output : y) {
            const double mean=std::accumulate(output.begin(),output.end(),0.0)/n;
            double ey=0;
            std::fill(yy.begin(),yy.end(),Complex{});
            for (std::size_t i=0; i<n; ++i) { yy[i]=output[i]-mean; ey+=std::norm(yy[i]); }
            fft.transform(yy);
            for (std::size_t c=0; c<x.size(); ++c) {
                const double normalizer=std::sqrt(std::max(1.e-30,ex[c]*ey));
                for (std::size_t i=0; i<fft.n; ++i) cross[i]=std::conj(xx[c][i])*yy[i];
                fft.transform(cross,true);
                for (int lag=-config.maxDelay; lag<=config.maxDelay; ++lag) {
                    const auto at=lag<0 ? fft.n-static_cast<std::size_t>(-lag) : static_cast<std::size_t>(lag);
                    const double score=std::abs(cross[at].real())/normalizer;
                    if (score>best) { best=score; bestLag=lag; }
                }
            }
        }
        return {bestLag,best};
    }
    bool broadbandEnough(const Channels& x) const {
        // A short, repeated broadband waveform is still not a persistently
        // exciting music reference: a FIR can memorize its nonlinear output.
        // Pure sines were handled by the explicitly harmonic method above.
        {
            const auto n=x[0].size();
            FFT full(nextPowerOfTwo(2*n));
            std::vector<Complex> ac(full.n);
            std::vector<double> prefix(n+1);
            for (const auto& channel:x) {
                std::fill(ac.begin(),ac.end(),Complex{});
                prefix[0]=0;
                for (std::size_t i=0;i<n;++i) { ac[i]=channel[i]; prefix[i+1]=prefix[i]+channel[i]*channel[i]; }
                full.transform(ac);
                for (auto& v:ac) v=std::norm(v);
                full.transform(ac,true);
                for (std::size_t lag=16;lag<=2*config.taps && lag<n/4;++lag) {
                    const double denominator=std::sqrt(std::max(1.e-30,prefix[n-lag]*(prefix[n]-prefix[lag])));
                    if (std::abs(ac[lag].real())/denominator>0.9999) return false;
                }
            }
        }
        constexpr std::size_t bins=4096;
        FFT fft(bins);
        std::vector<double> power(bins/2,0.0);
        std::vector<Complex> block(bins);
        for (const auto& channel : x) for (int frame=0; frame<4; ++frame) {
            const auto offset=channel.size()-bins-static_cast<std::size_t>(frame)*bins;
            for (std::size_t i=0; i<bins; ++i) block[i]=channel[offset+i]*(0.5-0.5*std::cos(2*pi*i/(bins-1)));
            fft.transform(block);
            for (std::size_t i=1; i<bins/2; ++i) power[i]+=std::norm(block[i]);
        }
        const double maximum=*std::max_element(power.begin(),power.end());
        const auto active=std::count_if(power.begin()+1,power.end(),[&](double p) { return p>maximum*1.e-5; });
        return active>=48;
    }
};
}
