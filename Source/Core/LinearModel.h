#pragma once
#include "Tone.h"
#include <functional>

namespace distortion {
// Finite, two-sided FIR represented as a causal filter after alignment.
// Solve only on training rows; validation is separated by a full filter length.
class LinearModel {
public:
    LinearModel(const Channels& input, std::size_t tapCount, std::size_t rowCount)
        : x(input), taps(tapCount), rows(rowCount), inputs(input.size()),
          fft(nextPowerOfTwo(rowCount+2*tapCount-2)), preFFT(nextPowerOfTwo(2*tapCount)),
          spectra(inputs, std::vector<Complex>(fft.n)),
          prePower(inputs, std::vector<double>(preFFT.n)), scratch(fft.n), temp(fft.n),
          preScratch(preFFT.n) {
        for (std::size_t c=0; c<inputs; ++c) {
            for (std::size_t i=0; i<x[c].size(); ++i) spectra[c][i]=x[c][i];
            fft.transform(spectra[c]);
            for (std::size_t i=0; i<fft.n; ++i) scratch[i]=std::norm(spectra[c][i]);
            fft.transform(scratch, true);
            std::fill(preScratch.begin(),preScratch.end(),Complex{});
            preScratch[0]=scratch[0].real();
            for (std::size_t i=1; i<taps; ++i) {
                const double v=scratch[i].real()*(0.5+0.5*std::cos(pi*i/taps));
                preScratch[i]=v; preScratch[preFFT.n-i]=v;
            }
            preFFT.transform(preScratch);
            const double floor=std::max(1.e-20, scratch[0].real()*1.e-7);
            for (std::size_t i=0; i<preFFT.n; ++i) prePower[c][i]=std::max(floor,preScratch[i].real());
            diagonalEnergy += scratch[0].real();
        }
        ridge=std::max(1.e-20,diagonalEnergy/inputs*1.e-5);
        if (inputs==2) {
            crossPower.resize(preFFT.n);
            for (std::size_t i=0;i<fft.n;++i) scratch[i]=std::conj(spectra[0][i])*spectra[1][i];
            fft.transform(scratch,true);
            std::fill(preScratch.begin(),preScratch.end(),Complex{});
            preScratch[0]=scratch[0].real();
            for (std::size_t i=1;i<taps;++i) {
                const double window=0.5+0.5*std::cos(pi*i/taps);
                preScratch[i]=scratch[i].real()*window;
                preScratch[preFFT.n-i]=scratch[fft.n-i].real()*window;
            }
            preFFT.transform(preScratch);
            for (std::size_t i=0;i<preFFT.n;++i) {
                const double bound=std::sqrt(prePower[0][i]*prePower[1][i])*0.999999;
                const double magnitude=std::abs(preScratch[i]);
                crossPower[i]=magnitude>bound?preScratch[i]*(bound/magnitude):preScratch[i];
            }
            secondPreScratch.resize(preFFT.n);
        }
    }

    struct Fit { std::vector<double> h; double convergence=1; int iterations=0; };
    std::vector<double> affineSeed(const std::vector<double>& target) const {
        const auto dimensions=inputs+1;
        std::vector<double> gram(dimensions*dimensions,0.0), values(dimensions), weights(dimensions,0.0);
        const auto offset=taps-1-taps/4;
        for (std::size_t i=0;i<rows;++i) {
            for (std::size_t c=0;c<inputs;++c) values[c]=x[c][i+offset];
            values.back()=1;
            for (std::size_t c=0;c<dimensions;++c) {
                weights[c]+=values[c]*target[i];
                for (std::size_t d=0;d<dimensions;++d) gram[c*dimensions+d]+=values[c]*values[d];
            }
        }
        for (std::size_t c=0;c<inputs;++c) gram[c*dimensions+c]+=std::max(1.e-20,gram[c*dimensions+c]*1.e-12);
        std::vector<double> seed(inputs*taps+1,0.0);
        if (choleskySolve(std::move(gram),weights,dimensions)) {
            for (std::size_t c=0;c<inputs;++c) seed[c*taps+taps/4]=weights[c];
            seed.back()=weights.back();
        }
        return seed;
    }
    Fit fit(const std::vector<double>& target, int limit) {
        const auto dimension=inputs*taps+1;
        std::vector<double> b(dimension), h(dimension,0.0), r, z, p, ap;
        transpose(target,b);
        const double scale=std::max(dot(b,b),1.e-30);
        auto seed=affineSeed(target);
        auto remainder=predict(seed);
        for (std::size_t i=0;i<rows;++i) remainder[i]=target[i]-remainder[i];
        transpose(remainder,b);
        r=b; precondition(r,z); p=z;
        double rz=dot(r,z);
        Fit result;
        result.convergence=std::sqrt(dot(r,r)/scale);
        if (result.convergence<1.e-6) { result.h=seed; return result; }
        // Learn the short, well-conditioned part first. Starting thousands of
        // unconstrained taps at zero was unreliable on highly coloured music.
        if (taps>128) {
            const auto shortTaps=std::max(std::size_t(128),taps/4);
            const auto start=taps-shortTaps-taps/4+shortTaps/4;
            Channels shortInput(inputs,std::vector<double>(rows+shortTaps-1));
            for (std::size_t c=0;c<inputs;++c)
                std::copy_n(x[c].begin()+static_cast<std::ptrdiff_t>(start),shortInput[c].size(),shortInput[c].begin());
            LinearModel shortModel(shortInput,shortTaps,rows);
            const auto learned=shortModel.fit(target,std::min(limit,24));
            std::fill(seed.begin(),seed.end(),0.0);
            for (std::size_t c=0;c<inputs;++c)
                std::copy_n(learned.h.begin()+static_cast<std::ptrdiff_t>(c*shortTaps),shortTaps,
                            seed.begin()+static_cast<std::ptrdiff_t>(c*taps+taps/4-shortTaps/4));
            seed.back()=learned.h.back();
            remainder=predict(seed);
            for (std::size_t i=0;i<rows;++i) remainder[i]=target[i]-remainder[i];
            transpose(remainder,b);r=b;precondition(r,z);p=z;rz=dot(r,z);
            result.convergence=std::sqrt(dot(r,r)/scale);
            if (result.convergence<1.e-6) { result.h=seed; return result; }
        }
        for (int iteration=0; iteration<limit; ++iteration) {
            normal(p,ap);
            const double divisor=dot(p,ap);
            if (!(divisor>0.0) || !std::isfinite(rz)) break;
            const double alpha=rz/divisor;
            for (std::size_t j=0; j<dimension; ++j) { h[j]+=alpha*p[j]; r[j]-=alpha*ap[j]; }
            result.iterations=iteration+1;
            result.convergence=std::sqrt(dot(r,r)/scale);
            if (result.convergence < 1.e-4) break;
            precondition(r,z);
            const double next=dot(r,z), beta=next/std::max(rz,1.e-300);
            for (std::size_t j=0; j<dimension; ++j) p[j]=z[j]+beta*p[j];
            rz=next;
        }
        // Regularize the correction, not the gain/matrix already demonstrated
        // by the training data. This keeps null/gain cases exact on music.
        for (std::size_t i=0;i<h.size();++i) h[i]+=seed[i];
        result.h=std::move(h);
        return result;
    }
    std::vector<double> predict(const std::vector<double>& h) {
        std::vector<double> predicted(rows);
        std::fill(scratch.begin(),scratch.end(),Complex{});
        for (std::size_t c=0; c<inputs; ++c) {
            std::fill(temp.begin(),temp.end(),Complex{});
            for (std::size_t k=0; k<taps; ++k) temp[k]=h[c*taps+k];
            fft.transform(temp);
            for (std::size_t k=0; k<fft.n; ++k) scratch[k]+=temp[k]*spectra[c][k];
        }
        fft.transform(scratch,true);
        for (std::size_t i=0; i<rows; ++i) predicted[i]=scratch[i+taps-1].real()+h.back();
        return predicted;
    }
private:
    void transpose(const std::vector<double>& v, std::vector<double>& out) {
        out.assign(inputs*taps+1,0.0);
        std::fill(temp.begin(),temp.end(),Complex{});
        for (std::size_t i=0; i<rows; ++i) temp[i+taps-1]=v[i];
        fft.transform(temp);
        for (std::size_t c=0; c<inputs; ++c) {
            for (std::size_t k=0; k<fft.n; ++k) scratch[k]=std::conj(spectra[c][k])*temp[k];
            fft.transform(scratch,true);
            for (std::size_t k=0; k<taps; ++k) out[c*taps+k]=scratch[k].real();
        }
        out.back()=std::accumulate(v.begin(),v.end(),0.0);
    }
    void normal(const std::vector<double>& h, std::vector<double>& out) {
        const auto predicted=predict(h);
        transpose(predicted,out);
        for (std::size_t k=0; k+1<h.size(); ++k) out[k]+=ridge*h[k];
    }
    void precondition(const std::vector<double>& r, std::vector<double>& z) {
        z.resize(r.size());
        if (inputs==2) {
            std::fill(preScratch.begin(),preScratch.end(),Complex{});
            std::fill(secondPreScratch.begin(),secondPreScratch.end(),Complex{});
            for (std::size_t k=0;k<taps;++k) { preScratch[k]=r[k]; secondPreScratch[k]=r[taps+k]; }
            preFFT.transform(preScratch); preFFT.transform(secondPreScratch);
            for (std::size_t k=0;k<preFFT.n;++k) {
                const double a=prePower[0][k]+ridge,d=prePower[1][k]+ridge;
                const auto b=crossPower[k], u=preScratch[k],v=secondPreScratch[k];
                const double det=std::max(a*d-std::norm(b),a*d*1.e-12);
                preScratch[k]=(d*u-b*v)/det;
                secondPreScratch[k]=(a*v-std::conj(b)*u)/det;
            }
            preFFT.transform(preScratch,true); preFFT.transform(secondPreScratch,true);
            for (std::size_t k=0;k<taps;++k) { z[k]=preScratch[k].real(); z[taps+k]=secondPreScratch[k].real(); }
            z.back()=r.back()/static_cast<double>(rows);
            return;
        }
        for (std::size_t c=0; c<inputs; ++c) {
            std::fill(preScratch.begin(),preScratch.end(),Complex{});
            for (std::size_t k=0; k<taps; ++k) preScratch[k]=r[c*taps+k];
            preFFT.transform(preScratch);
            for (std::size_t k=0; k<preFFT.n; ++k) preScratch[k]/=prePower[c][k]+ridge;
            preFFT.transform(preScratch,true);
            for (std::size_t k=0; k<taps; ++k) z[c*taps+k]=preScratch[k].real();
        }
        z.back()=r.back()/static_cast<double>(rows);
    }
    Channels x;
    std::size_t taps, rows, inputs;
    FFT fft, preFFT;
    std::vector<std::vector<Complex>> spectra;
    std::vector<std::vector<double>> prePower;
    std::vector<Complex> scratch, temp, preScratch;
    std::vector<Complex> crossPower,secondPreScratch;
    double diagonalEnergy=0, ridge=0;
};
}
