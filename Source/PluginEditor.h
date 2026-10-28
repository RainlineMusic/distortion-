#pragma once
#include "PluginProcessor.h"

class DistortionAudioProcessorEditor final : public juce::AudioProcessorEditor,private juce::Timer {
public:
    explicit DistortionAudioProcessorEditor(DistortionAudioProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    static juce::String formatValue(const distortion::Result&);
    // Same rendering path for integration captures and the live editor.
    void displayMeasurement(const distortion::Result&);
private:
    void timerCallback() override;
    DistortionAudioProcessor& processor;
    juce::Label title,value;
    juce::Image chassis;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistortionAudioProcessorEditor)
};
