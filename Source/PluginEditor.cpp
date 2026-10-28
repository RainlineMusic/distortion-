#include "PluginEditor.h"
#include <BinaryData.h>

namespace {
juce::Font inter(float height,bool medium=false) {
    static auto regular=juce::Typeface::createSystemTypefaceFor(BinaryData::InterRegular_ttf,BinaryData::InterRegular_ttfSize);
    static auto strong=juce::Typeface::createSystemTypefaceFor(BinaryData::InterMedium_ttf,BinaryData::InterMedium_ttfSize);
    return juce::Font(juce::FontOptions(medium?strong:regular).withHeight(height));
}
}
DistortionAudioProcessorEditor::DistortionAudioProcessorEditor(DistortionAudioProcessor& owner)
    : AudioProcessorEditor(owner),processor(owner) {
    setOpaque(true); setWantsKeyboardFocus(false);
    setResizable(false,false);
    title.setText("Distortion",juce::dontSendNotification);
    title.setFont(inter(23.0f,true)); title.setJustificationType(juce::Justification::centred);
    title.setColour(juce::Label::textColourId,juce::Colour(0xffadb9c7));
    title.setBorderSize(juce::BorderSize<int>(0));
    value.setFont(inter(62.0f)); value.setJustificationType(juce::Justification::centred);
    value.setColour(juce::Label::textColourId,juce::Colour(0xffa9cbee));
    value.setBorderSize(juce::BorderSize<int>(0)); value.setMinimumHorizontalScale(0.6f);
    value.setTitle("Distortion measurement");
    addAndMakeVisible(title); addAndMakeVisible(value);
    setSize(414,218);
    displayMeasurement(processor.measurement());
    startTimerHz(5);
}
juce::String DistortionAudioProcessorEditor::formatValue(const distortion::Result& result) {
    if (!result.valid()) return juce::String::fromUTF8("\xe2\x80\x94");
    if (result.percent<0.005) return "0.00%";
    if (result.percent>=10000.0) return ">9999%";
    return juce::String(result.percent,result.percent<100.0?2:1)+"%";
}
void DistortionAudioProcessorEditor::displayMeasurement(const distortion::Result& result) {
    const auto text=formatValue(result);
    if (value.getText()!=text) value.setText(text,juce::dontSendNotification);
    juce::String explanation;
    if (result.valid()) {
        explanation=result.method==distortion::Method::harmonicTHD
            ? "Harmonic THD relative to the output fundamental. Pure reference sine."
            : "Estimated residual RMS relative to output after linear compensation. Includes noise and changing dynamics.";
    } else {
        switch (result.status) {
            case distortion::Status::noReference: explanation="Send the signal before processing to the external sidechain."; break;
            case distortion::Status::silence: explanation="Play audio to measure."; break;
            case distortion::Status::insufficient: explanation="Use a pure test sine or broader musical material."; break;
            case distortion::Status::unrelated: explanation="Check the reference routing or allow the signals to synchronize."; break;
            case distortion::Status::overload: explanation="Analysis capture overflow. Play in real time, or use the WAV analyzer for a fast offline render."; break;
            case distortion::Status::invalid: explanation="Measurement unavailable: invalid samples or unsupported input."; break;
            case distortion::Status::measuring:
            case distortion::Status::valid: explanation="Measuring. Play a continuous section for a few seconds."; break;
        }
    }
    value.setDescription(explanation);
}
void DistortionAudioProcessorEditor::timerCallback() { displayMeasurement(processor.measurement()); }
void DistortionAudioProcessorEditor::paint(juce::Graphics& g) { g.drawImageAt(chassis,0,0); }
void DistortionAudioProcessorEditor::resized() {
    chassis=juce::Image(juce::Image::RGB,getWidth(),getHeight(),false);
    juce::Graphics g(chassis);
    const auto bounds=getLocalBounds().toFloat();
    // Materials and palette inherit Clip Pocket Solid Dark.
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1c2732),0,0,juce::Colour(0xff151d25),bounds.getWidth(),bounds.getHeight(),true));
    g.fillAll();
    const auto glass=bounds.reduced(18.0f);
    g.setColour(juce::Colour(0xff14191f)); g.fillRoundedRectangle(glass,1.5f);
    g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.6f),0,glass.getY(),juce::Colours::transparentBlack,0,glass.getY()+14,false));
    g.fillRect(glass.withHeight(14));
    g.setColour(juce::Colour(0xffd9e3ee).withAlpha(0.13f));
    g.drawHorizontalLine(juce::roundToInt(glass.getBottom()),glass.getX(),glass.getRight());
    title.setBounds(36,43,getWidth()-72,32);
    value.setBounds(32,87,getWidth()-64,81);
}
