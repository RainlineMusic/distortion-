#pragma once
#include "FFT.h"
#include "Types.h"
#include <limits>
#include <numeric>

namespace distortion {
inline double energy(const std::vector<double>& a) {
    return std::inner_product(a.begin(), a.end(), a.begin(), 0.0);
}
inline double dot(const std::vector<double>& a, const std::vector<double>& b) {
    return std::inner_product(a.begin(), a.end(), b.begin(), 0.0);
}
// Positive-definite dense solve, also used by the small harmonic regression.
inline bool choleskySolve(std::vector<double> matrix, std::vector<double>& rhs, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j <= i; ++j) {
            double s = matrix[i*n+j];
            for (std::size_t k = 0; k < j; ++k) s -= matrix[i*n+k] * matrix[j*n+k];
            if (i == j) {
                if (!(s > 1.e-16) || !std::isfinite(s)) return false;
                matrix[i*n+j] = std::sqrt(s);
            } else matrix[i*n+j] = s / matrix[j*n+j];
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < i; ++k) rhs[i] -= matrix[i*n+k] * rhs[k];
        rhs[i] /= matrix[i*n+i];
    }
    for (std::size_t i = n; i-- > 0;) {
        for (std::size_t k = i+1; k < n; ++k) rhs[i] -= matrix[k*n+i] * rhs[k];
        rhs[i] /= matrix[i*n+i];
    }
    return true;
}

inline std::vector<double> harmonicFit(const std::vector<double>& data, double omega, int harmonics) {
    const auto count = data.size();
    const std::size_t dimensions = static_cast<std::size_t>(2 * harmonics + 1);
    std::vector<double> gram(dimensions * dimensions, 0.0), rhs(dimensions, 0.0);
    std::vector<double> cs(static_cast<std::size_t>(2*harmonics+1)), sn(cs.size());
    cs[0] = static_cast<double>(count);
    for (int m = 1; m <= 2*harmonics; ++m) {
        const double angle = m * omega;
        const double amp = std::sin(0.5 * count * angle) / std::sin(0.5 * angle);
        cs[static_cast<std::size_t>(m)] = amp * std::cos(0.5 * (count-1) * angle);
        sn[static_cast<std::size_t>(m)] = amp * std::sin(0.5 * (count-1) * angle);
    }
    const auto sumC = [&](int m) { return cs[static_cast<std::size_t>(std::abs(m))]; };
    const auto sumS = [&](int m) { return (m < 0 ? -1.0 : 1.0) * sn[static_cast<std::size_t>(std::abs(m))]; };
    gram[0] = static_cast<double>(count);
    rhs[0] = std::accumulate(data.begin(), data.end(), 0.0);
    for (int h = 1; h <= harmonics; ++h) {
        const auto c = static_cast<std::size_t>(2*h-1), s = c+1;
        gram[c] = gram[c*dimensions] = sumC(h);
        gram[s] = gram[s*dimensions] = sumS(h);
        double cr = 1.0, si = 0.0;
        const double cw = std::cos(h*omega), sw = std::sin(h*omega);
        for (std::size_t i = 0; i < count; ++i) {
            rhs[c] += data[i]*cr;
            rhs[s] += data[i]*si;
            const double next = cr*cw - si*sw;
            si = si*cw + cr*sw; cr = next;
        }
        for (int k = 1; k <= harmonics; ++k) {
            const auto kc = static_cast<std::size_t>(2*k-1), ks = kc+1;
            gram[c*dimensions+kc] = 0.5*(sumC(h-k)+sumC(h+k));
            gram[s*dimensions+ks] = 0.5*(sumC(h-k)-sumC(h+k));
            gram[s*dimensions+kc] = 0.5*(sumS(h+k)+sumS(h-k));
            gram[c*dimensions+ks] = 0.5*(sumS(k+h)+sumS(k-h));
        }
    }
    if (!choleskySolve(std::move(gram), rhs, dimensions)) return {};
    return rhs;
}

// Fit the sinusoid recurrence including a DC term. Verify the result by a
// separate full-window sine fit: a pitched musical note is not a test sine.
inline double pureToneFrequency(const std::vector<double>& x, double sampleRate) {
    const double total = energy(x);
    if (total < static_cast<double>(x.size()) * 1.e-14 || x.size() < 128) return 0.0;
    // Symmetric recurrence avoids estimating two almost collinear predictors
    // at low frequencies / high sample rates. Long-double sums matter here.
    long double sx=0, sz=0, sxx=0, sxz=0;
    for (std::size_t i = 1; i+1 < x.size(); ++i) {
        const long double v=x[i], z=static_cast<long double>(x[i-1])+x[i+1];
        sx+=v; sz+=z; sxx+=v*v; sxz+=v*z;
    }
    const long double samples=x.size()-2;
    const long double denominator=samples*sxx-sx*sx;
    if (!(denominator>0)) return 0.0;
    const long double cosine=(samples*sxz-sx*sz)/(2*denominator);
    if (std::abs(cosine)>=1) return 0.0;
    const double omega=static_cast<double>(std::acos(cosine)), frequency=omega*sampleRate/(2*pi);
    if (frequency < 20.0 || frequency > std::min(18000.0, sampleRate*0.4)
        || frequency*x.size()/sampleRate < 8.0) return 0.0;
    const auto fit = harmonicFit(x, omega, 1);
    if (fit.empty()) return 0.0;
    double err=0, cr=1, si=0;
    const double cw=std::cos(omega), sw=std::sin(omega);
    for (double v : x) {
        const double d=v-fit[0]-fit[1]*cr-fit[2]*si;
        err += d*d;
        const double next=cr*cw-si*sw; si=si*cw+cr*sw; cr=next;
    }
    return err/total < 1.e-10 ? frequency : 0.0;
}

inline Result analyseTone(const Channels& x, const Channels& y, const Config& config) {
    Result result;
    result.status = Status::insufficient;
    std::size_t loudest=0;
    for (std::size_t c=1; c<x.size(); ++c) if (energy(x[c])>energy(x[loudest])) loudest=c;
    const double f=pureToneFrequency(x[loudest], config.sampleRate);
    if (!(f > 0.0)) return result;
    for (const auto& channel : x) if (energy(channel) > 1.e-6*energy(x[loudest])) {
        const double other=pureToneFrequency(channel, config.sampleRate);
        if (std::abs(other-f)>f*1.e-6) return result;
    }
    const int nh=std::min(64, static_cast<int>(std::floor(std::min(20000.0, config.sampleRate*0.499)/f)));
    if (nh < 2) return result;
    double fundamental=0, harmonic=0, output=0;
    for (const auto& channel : y) {
        const auto fit=harmonicFit(channel, 2*pi*f/config.sampleRate, nh);
        if (fit.empty()) return result;
        fundamental += fit[1]*fit[1]+fit[2]*fit[2];
        for (std::size_t j=3; j<fit.size(); ++j) harmonic += fit[j]*fit[j];
        const double dc=fit[0];
        for (double v : channel) output += (v-dc)*(v-dc);
    }
    if (fundamental < 1.e-14) return result;
    const double explained=(fundamental+harmonic)*0.5*x[0].size()/std::max(output,1.e-30);
    if (explained < 0.8) return result;
    result.method=Method::harmonicTHD;
    result.status=Status::valid;
    result.frequencyHz=f;
    result.harmonics=nh;
    result.percent=100.0*std::sqrt(harmonic/fundamental);
    result.errorRms=std::sqrt(0.5*harmonic/y.size());
    result.outputRms=std::sqrt(output/(y.size()*y[0].size()));
    return result;
}
}
