#include "PluginEditor.h"
#include <BinaryData.h>
namespace {
juce::Font inter(float height,bool medium=false) {
    static auto regular=juce::Typeface::createSystemTypefaceFor(BinaryData::InterRegular_ttf,BinaryData::InterRegular_ttfSize);
    static auto strong=juce::Typeface::createSystemTypefaceFor(BinaryData::InterMedium_ttf,BinaryData::InterMedium_ttfSize);
    return juce::Font(juce::FontOptions(medium?strong:regular).withHeight(height));
}
const juce::Colour ink(0xffadb9c7), blue(0xffa9cbee), amber(0xffe5b887), green(0xff8eafa7);
juce::String percent(double x,bool valid) {
    if(!valid)return juce::String::fromUTF8("\xe2\x80\x94");
    if(x>=10000)return ">9999%";
    return juce::String(x<.005?0:x,x<100?2:1)+"%";
}
void curve(juce::Graphics& g,const std::array<float,distortion::graphBands>& data,juce::Rectangle<float> box,juce::Colour colour) {
    juce::Path path;
    for(std::size_t i=0;i<data.size();++i) {
        const float x=box.getX()+box.getWidth()*float(i)/float(data.size()-1);
        const float y=box.getBottom()-box.getHeight()*juce::jlimit(0.f,1.f,(data[i]+96.f)/96.f);
        if(i==0)path.startNewSubPath(x,y);else path.lineTo(x,y);
    }
    g.setColour(colour);g.strokePath(path,juce::PathStrokeType(1.65f,juce::PathStrokeType::curved));
}
void grid(juce::Graphics& g,juce::Rectangle<float> b,bool labels) {
    g.setFont(inter(11));
    for(int db=0;db>=-96;db-=24) {
        const float y=b.getY()-float(db)/96*b.getHeight();
        g.setColour(ink.withAlpha(.12f));g.drawHorizontalLine(juce::roundToInt(y),b.getX(),b.getRight());
        g.setColour(ink);g.drawText(juce::String(db),int(b.getX()-36),int(y-7),28,14,juce::Justification::right);
    }
    for(int f:{20,50,100,200,500,1000,2000,5000,10000,20000}) {
        const float x=b.getX()+float(std::log(double(f)/20)/std::log(1000.))*b.getWidth();
        g.setColour(ink.withAlpha(.10f));g.drawVerticalLine(juce::roundToInt(x),b.getY(),b.getBottom());
        if(labels) {g.setColour(ink);g.drawText(f>=1000?juce::String(f/1000)+"k":juce::String(f),int(x-18),int(b.getBottom()+7),36,16,juce::Justification::centred);}
    }
}
}
DistortionAudioProcessorEditor::DistortionAudioProcessorEditor(DistortionAudioProcessor& owner):AudioProcessorEditor(owner),processor(owner) {
    setOpaque(true);setWantsKeyboardFocus(false);setResizable(false,false);
    title.setText("Distortion",juce::dontSendNotification);title.setFont(inter(25,true));title.setColour(juce::Label::textColourId,ink);
    for(auto* label:{&value,&integrated}) {label->setFont(inter(43));label->setColour(juce::Label::textColourId,blue);label->setBorderSize(juce::BorderSize<int>(0));addAndMakeVisible(label);}
    value.setTitle("Short distortion, rolling three seconds");integrated.setTitle("Integrated distortion since reset");
    reset.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff27333e));reset.setColour(juce::TextButton::textColourOffId,ink);
    reset.setTitle("Reset integrated measurement");reset.onClick=[this]{processor.resetIntegrated();};
    addAndMakeVisible(title);addAndMakeVisible(reset);setSize(800,648);startTimerHz(10);
    displayMeasurement(processor.measurement());displayMeters(processor.meters());
}
juce::String DistortionAudioProcessorEditor::formatValue(const distortion::Result& r) {return percent(r.percent,r.valid());}
void DistortionAudioProcessorEditor::displayMeasurement(const distortion::Result& r) {latest=r;repaint();}
void DistortionAudioProcessorEditor::displayMeters(const distortion::MeterView& v) {
    meters=v;value.setText(percent(v.shortPercent,v.shortValid),juce::dontSendNotification);
    integrated.setText(percent(v.integratedPercent,v.integratedValid),juce::dontSendNotification);
    value.setDescription("Residual RMS relative to output RMS. Rolling 3 seconds; updates every 100 ms after model learning. Includes noise and changing dynamics.");
    integrated.setDescription("Ratio of summed residual and output energies since reset. Warm-up and unavailable data excluded.");repaint();
}
void DistortionAudioProcessorEditor::timerCallback() {displayMeasurement(processor.measurement());displayMeters(processor.meters());}
void DistortionAudioProcessorEditor::paint(juce::Graphics& g) {
    g.drawImageAt(chassis,0,0);
    g.setFont(inter(12,true));g.setColour(ink);g.drawText("SPECTRUM",34,78,160,18,juce::Justification::left);
    auto legend=[&](juce::String text,int x,juce::Colour c){g.setColour(c);g.drawLine(float(x),87,float(x+18),87,1.7f);g.setColour(ink);g.drawText(text,x+26,78,65,18,juce::Justification::left);};
    legend("Before",568,green);legend("After",674,blue);
    const juce::Rectangle<float> top(72,115,686,196),bottom(72,381,686,107);
    grid(g,top,true);grid(g,bottom,true);
    g.setFont(inter(12,true));g.setColour(ink);g.drawText("ADDED RESIDUAL",34,346,210,18,juce::Justification::left);
    g.setFont(inter(11));g.drawText("dBFS / 1/6 octave",572,346,186,18,juce::Justification::right);
    if(meters.spectrumValid) {curve(g,meters.before,top,green);curve(g,meters.after,top,blue);}
    if(meters.residualValid)curve(g,meters.residual,bottom,amber);
    if(!meters.spectrumValid) {g.setColour(ink);g.setFont(inter(14));g.drawText("Play audio. Send the unprocessed signal to the sidechain.",top,juce::Justification::centred);}
    else if(!meters.residualValid) {g.setColour(ink);g.setFont(inter(13));g.drawText("Learning the linear response...",bottom,juce::Justification::centred);}
    g.setColour(ink);g.setFont(inter(12,true));g.drawText("Short",36,544,120,18,juce::Justification::left);g.drawText("Integrated",320,544,160,18,juce::Justification::left);
    g.setFont(inter(11));g.drawText("3 s / 100 ms",36,614,160,16,juce::Justification::left);
    g.drawText(juce::String(meters.seconds,1)+" s measured",320,614,200,16,juce::Justification::left);
    g.drawText("Estimated residual",568,28,190,24,juce::Justification::right);
}
void DistortionAudioProcessorEditor::resized() {
    chassis=juce::Image(juce::Image::RGB,getWidth(),getHeight(),false);juce::Graphics g(chassis);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1c2732),0,0,juce::Colour(0xff151d25),800,648,true));g.fillAll();
    g.setColour(juce::Colour(0xff14191f));g.fillRoundedRectangle(juce::Rectangle<float>(18,66,764,461),2);g.fillRoundedRectangle(juce::Rectangle<float>(18,535,764,102),2);
    g.setColour(ink.withAlpha(.16f));g.drawHorizontalLine(333,34,766);g.drawVerticalLine(291,550,621);
    title.setBounds(29,21,320,36);value.setBounds(36,565,236,48);integrated.setBounds(320,565,260,48);reset.setBounds(674,564,84,32);
}
