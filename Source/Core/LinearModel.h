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
        ridge=std::max(1.e-20,diagonalEnergy/inputs*1.e-10);
    }

    struct Fit { std::vector<double> h; double convergence=1; int iterations=0; };
    Fit fit(const std::vector<double>& target, int limit) {
        const auto dimension=inputs*taps+1;
        std::vector<double> b(dimension), h(dimension,0.0), r, z, p, ap;
        transpose(target,b);
        r=b; precondition(r,z); p=z;
        double rz=dot(r,z), scale=std::max(dot(b,b),1.e-30);
        Fit result;
        for (int iteration=0; iteration<limit; ++iteration) {
            normal(p,ap);
            const double divisor=dot(p,ap);
            if (!(divisor>0.0) || !std::isfinite(rz)) break;
            const double alpha=rz/divisor;
            for (std::size_t j=0; j<dimension; ++j) { h[j]+=alpha*p[j]; r[j]-=alpha*ap[j]; }
            result.iterations=iteration+1;
            result.convergence=std::sqrt(dot(r,r)/scale);
            if (result.convergence < 2.e-8) break;
            precondition(r,z);
            const double next=dot(r,z), beta=next/std::max(rz,1.e-300);
            for (std::size_t j=0; j<dimension; ++j) p[j]=z[j]+beta*p[j];
            rz=next;
        }
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
    double diagonalEnergy=0, ridge=0;
};
}
