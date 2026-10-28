#include "Core/Analyzer.h"
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace distortion;
namespace {
int failures=0, cases=0;
void check(bool condition, const std::string& message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
void report(const std::string& name, const Result& r, double expected, double tolerance) {
    ++cases;
    std::cout << std::left << std::setw(31) << name << " " << statusName(r.status)
              << " result=" << std::setprecision(9) << r.percent << "% expected=" << expected
              << "% delay=" << r.delaySamples << " train=" << r.trainingPercent
              << " grad=" << r.convergence << " iterations=" << r.iterations << '\n';
    check(r.valid(),name+": valid result");
    check(std::abs(r.percent-expected)<tolerance,name+": percentage tolerance");
}
Channels noise(std::size_t n,int channels=1,unsigned seed=717) {
    std::mt19937 rng(seed); std::normal_distribution<double> d(0.0,0.15);
    Channels x(static_cast<std::size_t>(channels),std::vector<double>(n));
    for (auto& c:x) for (auto& s:c) s=d(rng);
    return x;
}
Channels tones(std::size_t n,double fs,double f,double h2=0,double h3=0,double gain=1.0) {
    Channels y(1,std::vector<double>(n));
    for (std::size_t i=0;i<n;++i) {
        const double a=2*pi*f*i/fs;
        y[0][i]=0.35*gain*(std::sin(a)+h2*std::sin(2*a+0.34)+h3*std::cos(3*a-0.19));
    }
    return y;
}
double at(const std::vector<double>& c,std::ptrdiff_t i) {
    return i>=0 && i<static_cast<std::ptrdiff_t>(c.size()) ? c[static_cast<std::size_t>(i)] : 0.0;
}
void toneTests() {
    for (double fs:{44100.0,48000.0,96000.0,192000.0}) {
        const Config cfg(fs); Analyzer a(cfg); const auto n=cfg.requiredSamples();
        auto x=tones(n,fs,997.3);
        for (double percent:{0.0,0.1,1.0,5.0,10.0}) {
            auto y=tones(n,fs,997.3,percent/100.0,0.0,0.251188643);
            report("harmonic "+std::to_string(int(fs))+" / "+std::to_string(percent),a.analyse(x,y),percent,0.00005);
        }
        auto y=tones(n,fs,997.3,0.03,0.04,-1.995262315);
        report("H2=3% H3=4% "+std::to_string(int(fs)),a.analyse(x,y),5.0,0.00005);
    }
    Config cfg; Analyzer a(cfg); const auto n=cfg.requiredSamples();
    for (double f:{31.17,80.6,3217.87,7000.9}) {
        auto x=tones(n,cfg.sampleRate,f), y=tones(n,cfg.sampleRate,f,0.01);
        report("non-bin sine "+std::to_string(f),a.analyse(x,y),1.0,0.002);
    }
    auto x=tones(n,48000,997.3); double last=0;
    for (double threshold:{0.32,0.25,0.15}) {
        auto y=x; for (auto& v:y[0]) v=std::clamp(v,-threshold,threshold);
        const auto r=a.analyse(x,y);
        check(r.valid() && r.percent>last,"hard clipping increases THD"); last=r.percent;
        std::cout<<"hard clip threshold="<<threshold<<" THD="<<r.percent<<"%\n";
    }
}
void linearTests() {
    Config cfg; Analyzer a(cfg); const auto n=cfg.requiredSamples();
    auto x=noise(n), y=x;
    report("identity",a.analyse(x,y),0.0,0.002);
    for (double gain:{0.251188643,0.501187234,1.995262315,3.981071706,-1.0}) {
        y=x; for (auto& v:y[0]) v=v*gain+0.01;
        report("gain / polarity / DC "+std::to_string(gain),a.analyse(x,y),0.0,0.002);
    }
    for (int delay:{1,173,5000,-817}) {
        y=x;
        for (std::size_t i=0;i<n;++i) y[0][i]=0.42*at(x[0],static_cast<std::ptrdiff_t>(i)-delay);
        const auto r=a.analyse(x,y); report("delay "+std::to_string(delay),r,0.0,0.002);
        check(r.delaySamples==delay,"integer latency detected");
    }
    y=x;
    for (std::size_t i=0;i<n;++i)
        y[0][i]=0.8*x[0][i]+0.15*at(x[0],static_cast<std::ptrdiff_t>(i)-1)-0.2*at(x[0],static_cast<std::ptrdiff_t>(i)-37)+0.05*at(x[0],static_cast<std::ptrdiff_t>(i)-150);
    report("FIR EQ",a.analyse(x,y),0.0,0.002);
    y=x;
    for (std::size_t i=0;i<n;++i)
        y[0][i]=-0.65*x[0][i]+at(x[0],static_cast<std::ptrdiff_t>(i)-1)+0.65*at(y[0],static_cast<std::ptrdiff_t>(i)-1);
    report("IIR all-pass",a.analyse(x,y),0.0,0.002);
    y=x;
    for (std::size_t i=0;i<n;++i) y[0][i]=0.01*x[0][i]+0.99*at(y[0],static_cast<std::ptrdiff_t>(i)-1);
    report("IIR low-pass pole .99",a.analyse(x,y),0.0,0.08);
    // Independent finite windowed-sinc fractional delay, with non-unity gain.
    std::vector<double> kernel(65);
    for (int k=0;k<65;++k) {
        const double t=k-32.37;
        kernel[static_cast<std::size_t>(k)]=std::sin(pi*t)/(pi*t)*(0.5-0.5*std::cos(2*pi*k/64));
    }
    y=x;
    for (std::size_t i=0;i<n;++i) {
        y[0][i]=0;
        for (std::size_t k=0;k<kernel.size();++k) y[0][i]+=0.75*kernel[k]*at(x[0],static_cast<std::ptrdiff_t>(i)-static_cast<std::ptrdiff_t>(k));
    }
    report("fractional delay 32.37",a.analyse(x,y),0.0,0.002);
    auto stereo=noise(n,2); auto out=stereo;
    for (std::size_t i=0;i<n;++i) {
        stereo[1][i]=0.7*stereo[0][i]+0.3*stereo[1][i];
        out[0][i]=0.8*stereo[0][i]+0.2*stereo[1][i];
        out[1][i]=-0.3*stereo[0][i]+0.7*stereo[1][i];
    }
    report("correlated stereo M/S matrix",a.analyse(stereo,out),0.0,0.003);
    stereo[1]=stereo[0]; out=stereo;
    for (auto& v:out[1]) v*=-0.25;
    report("dual-mono / rank reduction",a.analyse(stereo,out),0.0,0.003);
    // Coloured, non-periodic excitation is harder than a white-noise null test.
    for (std::size_t i=1;i<n;++i) x[0][i]=0.05*x[0][i]+0.95*x[0][i-1];
    y=x;
    for (std::size_t i=1;i<n;++i) y[0][i]=0.7*x[0][i]+0.3*x[0][i-1];
    report("coloured reference + EQ",a.analyse(x,y),0.0,0.02);
}
void residualAndGuardTests() {
    Config cfg; Analyzer a(cfg); const auto n=cfg.requiredSamples();
    auto x=noise(n), independent=noise(n,1,914), y=x;
    for (double ratio:{0.01,0.05,0.15}) {
        for (std::size_t i=0;i<n;++i) y[0][i]=x[0][i]+ratio*independent[0][i];
        report("uncorrelated residual "+std::to_string(ratio),a.analyse(x,y),100*ratio/std::sqrt(1+ratio*ratio),ratio*100*0.09);
    }
    for (double ratio:{0.01,0.05,0.15}) {
        for (std::size_t i=0;i<n;++i) {
            const double z=x[0][i]/0.15;
            y[0][i]=x[0][i]+ratio*0.15*(z*z*z-3*z)/std::sqrt(6.0);
        }
        report("Hermite cubic "+std::to_string(ratio),a.analyse(x,y),100*ratio/std::sqrt(1+ratio*ratio),ratio*100*0.16);
    }
    // A new distorted attack in held-out material is the quantity to measure,
    // not a reason to discard a correctly fitted linear reference model.
    y=x;
    const auto start=n-static_cast<std::size_t>(cfg.maxDelay)-cfg.taps-cfg.validation;
    for (std::size_t i=start;i<start+cfg.validation;++i) y[0][i]=std::clamp(y[0][i],-.12,.12);
    const auto attack=a.analyse(x,y);
    check(attack.valid() && attack.percent>1,"new nonlinear event must not blank the readout");
    auto zero=x; for (auto& v:zero[0]) v=0;
    check(a.analyse(zero,x).status==Status::noReference,"missing reference has no percentage");
    check(a.analyse(x,zero).status==Status::silence,"silence has no percentage");
    check(a.analyse(x,independent).status==Status::unrelated,"unrelated audio rejected");
    y=x; y[0][13]=std::numeric_limits<double>::quiet_NaN();
    check(a.analyse(x,y).status==Status::invalid,"NaN rejected");
    y=x; y[0].pop_back(); check(a.analyse(x,y).status==Status::invalid,"mismatched lengths rejected");
    auto note=tones(n,48000,400,0.1,0.05); y=note;
    report("narrow-band exact null",a.analyse(note,y),0.0,0.0001);
    auto periodic=noise(n),clipped=periodic;
    for (std::size_t i=0;i<n;++i) {
        periodic[0][i]=periodic[0][i%1024];
        clipped[0][i]=std::tanh(3*periodic[0][i]);
    }
    check(a.analyse(periodic,clipped).status==Status::insufficient,"short periodic reference cannot teach away nonlinear output");
    Channels shortInput(1,std::vector<double>(32,0.1));
    check(!a.analyse(shortInput,shortInput).valid(),"short window rejected");
}
void musicalRegression() {
    Config cfg; Analyzer analyzer(cfg); const auto n=cfg.requiredSamples();
    std::mt19937 rng(44);std::normal_distribution<double> random(0,1);
    for (double noiseLevel:{.03,.6}) {
        Channels x(2,std::vector<double>(n));double low=0;
        for (std::size_t i=0;i<n;++i) {
            const double t=i/48000.0;low=.98*low+.02*random(rng);
            x[0][i]=.3*std::sin(2*pi*110*t)*(.6+.4*std::sin(7*t))+.12*std::sin(2*pi*329.63*t)+noiseLevel*low;
            x[1][i]=.9*x[0][i]+.02*std::sin(2*pi*440*t);
        }
        auto y=x;for(auto& channel:y)for(auto& v:channel)v*=.8;
        report("musical coloured gain",analyzer.analyse(x,y),0.0,0.0001);
        if (noiseLevel>.1) {
            y=x;for(auto& channel:y){double lowpass=0;for(auto& v:channel){lowpass=.8*lowpass+.2*v;v=lowpass;}}
            report("musical coloured EQ",analyzer.analyse(x,y),0.0,0.08);
            y=x;for(auto& channel:y)for(auto& v:channel)v=std::tanh(3*v)/3;
            const auto nonlinear=analyzer.analyse(x,y);
            check(nonlinear.valid() && nonlinear.percent>.1,"musical coloured nonlinear input remains measurable");
        }
    }
}
}
int main() {
    const auto start=std::chrono::steady_clock::now();
    toneTests(); linearTests(); residualAndGuardTests(); musicalRegression();
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"Compared percentage cases="<<cases<<" failures="<<failures<<" elapsed="<<seconds<<"s\n";
    return failures==0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
