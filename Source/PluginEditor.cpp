#include "PluginEditor.h"
#include <BinaryData.h>
namespace {
juce::Font inter(float height,bool medium=false) {
    static auto regular=juce::Typeface::createSystemTypefaceFor(BinaryData::InterRegular_ttf,BinaryData::InterRegular_ttfSize);
    static auto strong=juce::Typeface::createSystemTypefaceFor(BinaryData::InterMedium_ttf,BinaryData::InterMedium_ttfSize);
    return juce::Font(juce::FontOptions(medium?strong:regular).withHeight(height));
}
const juce::Colour ink(0xffadb9c7), blue(0xffa9cbee);
juce::String percent(double x,bool valid) {
    if(!valid)return juce::String::fromUTF8("\xe2\x80\x94");
    if(x>=10000)return ">9999%";
    return juce::String(x<.005?0:x,x<100?2:1)+"%";
}
}
DistortionAudioProcessorEditor::DistortionAudioProcessorEditor(DistortionAudioProcessor& owner):AudioProcessorEditor(owner),processor(owner) {
    setOpaque(true);setWantsKeyboardFocus(false);setResizable(false,false);
    title.setText("Distortion",juce::dontSendNotification);title.setFont(inter(25,true));title.setColour(juce::Label::textColourId,ink);
    for(auto* label:{&value,&integrated}) {label->setFont(inter(43));label->setColour(juce::Label::textColourId,blue);label->setBorderSize(juce::BorderSize<int>(0));addAndMakeVisible(label);}
    value.setTitle("Short distortion, rolling three seconds");integrated.setTitle("Integrated distortion since reset");
    reset.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff27333e));reset.setColour(juce::TextButton::textColourOffId,ink);
    reset.setTitle("Reset integrated measurement");reset.onClick=[this]{processor.resetIntegrated();};
    addAndMakeVisible(title);addAndMakeVisible(reset);setSize(560,236);startTimerHz(10);
    displayMeasurement(processor.measurement());distortion::MeterView next;if(processor.tryMeters(next))displayMeters(next);
}
juce::String DistortionAudioProcessorEditor::formatValue(const distortion::Result& r) {return percent(r.percent,r.valid());}
void DistortionAudioProcessorEditor::displayMeasurement(const distortion::Result& r) {latest=r;}
void DistortionAudioProcessorEditor::displayMeters(const distortion::MeterView& v) {
    if(juce::String(meters.seconds,1)!=juce::String(v.seconds,1))repaint(292,176,200,18);
    meters=v;value.setText(percent(v.shortPercent,v.shortValid),juce::dontSendNotification);
    integrated.setText(percent(v.integratedPercent,v.integratedValid),juce::dontSendNotification);
    value.setDescription("Residual RMS relative to output RMS. Rolling 3 seconds; updates every 100 ms after model learning. Compensates a 20 ms gain trajectory. Includes noise, model error and uncompensated dynamics.");
    integrated.setDescription("Ratio of summed residual and output energies since reset. Warm-up and unavailable data excluded.");
}
void DistortionAudioProcessorEditor::timerCallback() {displayMeasurement(processor.measurement());distortion::MeterView next;if(processor.tryMeters(next))displayMeters(next);}
void DistortionAudioProcessorEditor::paint(juce::Graphics& g) {
    g.drawImageAt(chassis,0,0);g.setColour(ink);g.setFont(inter(12,true));
    g.drawText("Short",32,83,180,18,juce::Justification::left);
    g.drawText("Integrated",292,83,200,18,juce::Justification::left);
    g.setFont(inter(11));g.drawText("3 s / 100 ms",32,176,180,18,juce::Justification::left);
    g.drawText(juce::String(meters.seconds,1)+" s measured",292,176,180,18,juce::Justification::left);
    g.drawText("Music estimate",312,29,216,18,juce::Justification::right);
}
void DistortionAudioProcessorEditor::resized() {
    chassis=juce::Image(juce::Image::RGB,getWidth(),getHeight(),false);juce::Graphics g(chassis);
    g.fillAll(juce::Colour(0xff151d25));g.setColour(juce::Colour(0xff14191f));
    g.fillRoundedRectangle(juce::Rectangle<float>(18,70,524,138),2);
    g.setColour(ink.withAlpha(.16f));g.drawVerticalLine(272,86,188);
    title.setBounds(25,21,270,36);value.setBounds(32,112,218,55);integrated.setBounds(292,112,238,55);
    reset.setBounds(464,209,78,24);
}
