#include "PluginProcessor.h"
#include "PluginEditor.h"

DistortionAudioProcessor::DistortionAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true)
                                     .withInput("Reference (Pre)",juce::AudioChannelSet::stereo(),true)
                                     .withOutput("Output",juce::AudioChannelSet::stereo(),true)) {}

void DistortionAudioProcessor::prepareToPlay(double sampleRate,int maximumBlockSize) {
    juce::ignoreUnused(maximumBlockSize);
    transport.reset();
    setLatencySamples(0);
    analyzer.prepare(sampleRate);
}
void DistortionAudioProcessor::releaseResources() { analyzer.stop(); }

bool DistortionAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const {
    const auto main=layout.getMainOutputChannelSet();
    if (main!=juce::AudioChannelSet::mono() && main!=juce::AudioChannelSet::stereo()) return false;
    if (layout.getMainInputChannelSet()!=main || layout.inputBuses.size()!=2 || layout.outputBuses.size()!=1) return false;
    const auto reference=layout.inputBuses[1];
    return reference.isDisabled() || reference==juce::AudioChannelSet::mono() || reference==juce::AudioChannelSet::stereo();
}
void DistortionAudioProcessor::trackTransport(int samples) {
    if (auto* head=getPlayHead()) if (const auto position=head->getPosition()) {
        const bool playing=position->getIsPlaying();
        if (const auto at=position->getTimeInSamples()) {
            if (transport.update(*at,playing,samples)) analyzer.invalidate();
        }
    }
}
template<class Sample>
void DistortionAudioProcessor::capture(juce::AudioBuffer<Sample>& buffer) {
    // Main input and output layouts match. JUCE supplies them in the same
    // channels: leave those samples, and the auxiliary input, completely alone.
    const auto main=getBusBuffer(buffer,false,0);
    trackTransport(buffer.getNumSamples());
    const bool connected=getBusCount(true)>1 && getBus(true,1)->isEnabled();
    if (!connected || getChannelCountOfBus(true,1)==0) { analyzer.invalidate(distortion::Status::noReference); return; }
    const auto reference=getBusBuffer(buffer,true,1);
    if (main.getNumChannels()==0 || reference.getNumChannels()==0) { analyzer.invalidate(); return; }
    const auto* x0=reference.getReadPointer(0);
    const auto* x1=reference.getReadPointer(reference.getNumChannels()>1?1:0);
    const auto* y0=main.getReadPointer(0);
    const auto* y1=main.getReadPointer(main.getNumChannels()>1?1:0);
    analyzer.push(x0,x1,y0,y1,static_cast<std::size_t>(buffer.getNumSamples()),connected);
}
void DistortionAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) { capture(buffer); }
void DistortionAudioProcessor::processBlock(juce::AudioBuffer<double>& buffer,juce::MidiBuffer&) { capture(buffer); }
void DistortionAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&) { analyzer.invalidate(); }
void DistortionAudioProcessor::processBlockBypassed(juce::AudioBuffer<double>&,juce::MidiBuffer&) { analyzer.invalidate(); }

void DistortionAudioProcessor::getStateInformation(juce::MemoryBlock& destination) {
    juce::XmlElement state("Distortion"); state.setAttribute("version",1);
    copyXmlToBinary(state,destination);
}
void DistortionAudioProcessor::setStateInformation(const void* data,int size) {
    // Version one has no parameters or preferences. A host may restore state
    // while processing: do not touch worker-owned or audio-owned storage here.
    juce::ignoreUnused(data,size);
}
juce::AudioProcessorEditor* DistortionAudioProcessor::createEditor() { return new DistortionAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DistortionAudioProcessor(); }
