#pragma once
#include <JuceHeader.h>
#include "Core/RealtimeAnalyzer.h"
#include "Core/TransportTracker.h"

class DistortionAudioProcessor final : public juce::AudioProcessor {
public:
    DistortionAudioProcessor();
    ~DistortionAudioProcessor() override = default;
    void prepareToPlay(double sampleRate,int maximumBlockSize) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<double>&,juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    distortion::Result measurement() const noexcept { return analyzer.snapshot(); }
    distortion::MeterView meters() const { return analyzer.meterSnapshot(); }
    bool tryMeters(distortion::MeterView& v) const { return analyzer.tryMeterSnapshot(v); }
    void resetIntegrated() { analyzer.resetIntegrated(); }
private:
    template<class Sample> void capture(juce::AudioBuffer<Sample>&);
    void trackTransport(int samples);
    distortion::RealtimeAnalyzer analyzer;
    distortion::TransportTracker transport;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistortionAudioProcessor)
};
