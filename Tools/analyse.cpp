#include "Core/Analyzer.h"
#include "Wav.h"
#include <iomanip>
#include <iostream>

int main(int argc,char** argv) {
    if (argc==2 && std::string(argv[1])=="--help") {
        std::cout<<"DistortionAnalyze PRE.wav POST.wav [--output result.json]\n"
                 <<"No resampling or normalization. Identical rates and frame counts required.\n"
                 <<"Music: held-out residual RMS/output RMS. Pure reference sine: harmonic THD.\n";
        return 0;
    }
    if (argc!=3 && !(argc==5 && std::string(argv[3])=="--output")) {
        std::cerr<<"Usage: DistortionAnalyze PRE.wav POST.wav [--output result.json]\n"; return 2;
    }
    try {
        const auto pre=distortion::readWav(argv[1]),post=distortion::readWav(argv[2]);
        if (pre.sampleRate!=post.sampleRate) throw std::runtime_error("Sample rates differ; explicit resampling is required before analysis");
        if (pre.channels[0].size()!=post.channels[0].size()) throw std::runtime_error("Frame counts differ; preserve a shared timeline when exporting");
        const distortion::Config config(pre.sampleRate); distortion::Analyzer analyzer(config);
        const auto frames=pre.channels[0].size(),needed=config.requiredSamples();
        if (frames<needed) throw std::runtime_error("WAV too short: needs at least "+std::to_string(needed)+" frames");
        std::ofstream file;
        if (argc==5) { file.open(argv[4]); if (!file) throw std::runtime_error("Cannot create output JSON"); }
        std::ostream& out=argc==5?file:std::cout;
        out<<std::setprecision(12)<<"{\n  \"schema\":1,\n  \"sampleRate\":"<<pre.sampleRate
           <<",\n  \"frames\":"<<frames<<",\n  \"firTaps\":"<<config.taps
           <<",\n  \"maxDelaySamples\":"<<config.maxDelay<<",\n  \"measurements\":[\n";
        bool first=true;
        const auto hop=static_cast<std::size_t>(pre.sampleRate);
        for (std::size_t start=0;start+needed<=frames;start+=hop) {
            distortion::Channels x(pre.channels.size()),y(post.channels.size());
            for (std::size_t c=0;c<x.size();++c) x[c].assign(pre.channels[c].begin()+static_cast<std::ptrdiff_t>(start),pre.channels[c].begin()+static_cast<std::ptrdiff_t>(start+needed));
            for (std::size_t c=0;c<y.size();++c) y[c].assign(post.channels[c].begin()+static_cast<std::ptrdiff_t>(start),post.channels[c].begin()+static_cast<std::ptrdiff_t>(start+needed));
            const auto r=analyzer.analyse(x,y);
            if (!first) out<<",\n";
            first=false;
            out<<"    {\"windowStartFrame\":"<<start<<",\"windowFrames\":"<<needed
               <<",\"status\":\""<<distortion::statusName(r.status)<<"\",\"method\":\""
               <<(r.method==distortion::Method::harmonicTHD?"harmonic_thd_f":"held_out_residual_output")<<"\",\"percent\":";
            if (r.valid()) out<<r.percent; else out<<"null";
            out<<",\"delaySamples\":"<<r.delaySamples<<",\"frequencyHz\":"<<r.frequencyHz
               <<",\"harmonics\":"<<r.harmonics<<",\"trainingPercent\":"<<r.trainingPercent
               <<",\"errorRms\":"<<r.errorRms<<",\"outputRms\":"<<r.outputRms
               <<",\"predictionCorrelationSquared\":"<<r.predictionCorrelationSquared<<",\"normalGradient\":"<<r.convergence
               <<",\"iterations\":"<<r.iterations<<"}";
            if (frames-needed-start<hop) break;
        }
        out<<"\n  ]\n}\n";
        if (!out) throw std::runtime_error("JSON output write failed");
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 2; }
}
