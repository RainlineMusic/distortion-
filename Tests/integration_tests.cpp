#include "PluginEditor.h"
#include <cstring>
#include <iostream>
#include <random>

namespace {
int failures=0;
void require(bool ok,const char* message) { if (!ok) { ++failures; std::cerr<<"FAIL: "<<message<<'\n'; } }
template<class Sample>
void passthrough(DistortionAudioProcessor& p,int channels,int samples) {
    juce::AudioBuffer<Sample> buffer(channels,samples);
    std::mt19937 rng(191); std::uniform_real_distribution<double> values(-1.3,1.3);
    for (int c=0;c<channels;++c) for (int i=0;i<samples;++i) buffer.setSample(c,i,static_cast<Sample>(values(rng)));
    juce::AudioBuffer<Sample> before; before.makeCopyOf(buffer);
    juce::MidiBuffer midi;
    p.processBlock(buffer,midi);
    for (int c=0;c<channels;++c) require(std::memcmp(buffer.getReadPointer(c),before.getReadPointer(c),sizeof(Sample)*static_cast<std::size_t>(samples))==0,"bit-exact audio and sidechain passthrough");
    p.processBlockBypassed(buffer,midi);
    for (int c=0;c<channels;++c) require(std::memcmp(buffer.getReadPointer(c),before.getReadPointer(c),sizeof(Sample)*static_cast<std::size_t>(samples))==0,"host bypass leaves all samples unchanged");
}
void save(DistortionAudioProcessorEditor& editor,const juce::File& file) {
    const auto image=editor.createComponentSnapshot(editor.getLocalBounds(),true,2.0f);
    auto stream=file.createOutputStream(); require(stream!=nullptr,"capture output stream");
    if (stream) { stream->setPosition(0);stream->truncate(); }
    if (stream) require(juce::PNGImageFormat().writeImageToStream(image,*stream),"PNG encoding");
}
}
int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    DistortionAudioProcessor processor;
    for (int mainCount:{1,2}) for (int referenceCount:{0,1,2}) {
        auto layout=processor.getBusesLayout();
        layout.inputBuses.set(0,juce::AudioChannelSet::canonicalChannelSet(mainCount));
        layout.outputBuses.set(0,juce::AudioChannelSet::canonicalChannelSet(mainCount));
        layout.inputBuses.set(1,juce::AudioChannelSet::canonicalChannelSet(referenceCount));
        require(processor.setBusesLayout(layout),"mono/stereo sidechain layout");
        for (double fs:{44100.0,48000.0,96000.0,192000.0}) {
            processor.setRateAndBufferSizeDetails(fs,512);
            processor.prepareToPlay(fs,512);
            for (int block:{1,7,32,64,127,512,2048}) {
                passthrough<float>(processor,mainCount+referenceCount,block);
                passthrough<double>(processor,mainCount+referenceCount,block);
            }
            processor.releaseResources();
        }
    }
    auto layout=processor.getBusesLayout();
    layout.inputBuses.set(0,juce::AudioChannelSet::stereo());
    layout.inputBuses.set(1,juce::AudioChannelSet::stereo());
    layout.outputBuses.set(0,juce::AudioChannelSet::stereo());
    require(processor.setBusesLayout(layout),"stereo calibration layout");
    processor.setRateAndBufferSizeDetails(48000,256); processor.prepareToPlay(48000,256);
    const distortion::Config config;
    const auto total=config.requiredSamples()+1024;
    juce::AudioBuffer<float> buffer(4,256); juce::MidiBuffer midi;
    for (std::size_t base=0;base<total;base+=256) {
        const auto frames=static_cast<int>(std::min(std::size_t(256),total-base));
        buffer.setSize(4,frames,false,false,true);
        for (int i=0;i<frames;++i) {
            const double phase=2*distortion::pi*997.3*(base+static_cast<std::size_t>(i))/48000;
            const float dry=static_cast<float>(0.35*std::sin(phase));
            const float wet=static_cast<float>(0.175*(std::sin(phase)+0.03*std::sin(2*phase)+0.04*std::cos(3*phase)));
            buffer.setSample(0,i,wet); buffer.setSample(1,i,wet);
            buffer.setSample(2,i,dry); buffer.setSample(3,i,dry);
        }
        processor.processBlock(buffer,midi);
    }
    auto reading=processor.measurement();
    const auto until=juce::Time::getMillisecondCounter()+5000;
    while (!reading.valid() && juce::Time::getMillisecondCounter()<until) { juce::Thread::sleep(20); reading=processor.measurement(); }
    require(reading.valid() && reading.method==distortion::Method::harmonicTHD && std::abs(reading.percent-5.0)<0.001,"processor sidechain 3% H2 + 4% H3 = 5% THD");
    require(processor.getLatencySamples()==0,"no signal latency");
    require(processor.getParameters().isEmpty(),"no bypass or settings parameter");
    juce::MemoryBlock state; processor.getStateInformation(state);
    require(state.getSize()>0,"state serialization"); processor.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
    processor.releaseResources();
    {
        DistortionAudioProcessorEditor editor(processor);
        require(editor.getNumChildComponents()==4,"title, two readouts and reset");
        const auto folder=juce::File(argc>1?argv[1]:"ui-captures");
        require(folder.createDirectory().wasOk(),"capture folder");
        editor.displayMeasurement(reading); save(editor,folder.getChildFile("thd-5-percent.png"));
        distortion::Result residual; residual.status=distortion::Status::valid; residual.percent=2.5;
        editor.displayMeasurement(residual);
        distortion::MeterView demo; demo.shortValid=demo.integratedValid=true;demo.shortPercent=2.5;demo.integratedPercent=1.84;demo.seconds=93.7;
        editor.displayMeters(demo);save(editor,folder.getChildFile("residual-example.png"));
        editor.displayMeters(distortion::MeterView{});
        residual.status=distortion::Status::noReference;
        editor.displayMeasurement(residual); save(editor,folder.getChildFile("no-reference.png"));
        residual.status=distortion::Status::measuring;
        editor.displayMeasurement(residual); save(editor,folder.getChildFile("measuring.png"));
        residual.status=distortion::Status::valid; residual.percent=125;
        require(DistortionAudioProcessorEditor::formatValue(residual)=="125.0%","not clamped to 100");
        residual.percent=std::numeric_limits<double>::quiet_NaN();
        require(!DistortionAudioProcessorEditor::formatValue(residual).contains("%"),"no nonfinite readout");
    }
    std::cout<<"Processor calibration="<<reading.percent<<"% integration failures="<<failures<<'\n';
    return failures==0?0:1;
}
