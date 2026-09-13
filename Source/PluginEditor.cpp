#include "PluginEditor.h"

namespace
{
const juce::Colour bg(0xff10191d),panel(0xff18272c),text(0xffe7f1ef),muted(0xff93acae),line(0xff30484d);
}

SynthLook::SynthLook()
{
    setColour(juce::Slider::textBoxTextColourId,text);
    setColour(juce::Slider::textBoxBackgroundColourId,juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
    setColour(juce::ComboBox::backgroundColourId,panel); setColour(juce::ComboBox::outlineColourId,line);
    setColour(juce::ComboBox::textColourId,text); setColour(juce::ComboBox::arrowColourId,muted);
    setColour(juce::PopupMenu::backgroundColourId,panel); setColour(juce::PopupMenu::textColourId,text);
    setColour(juce::TextButton::buttonColourId,panel); setColour(juce::TextButton::textColourOffId,text);
    setColour(juce::TextButton::buttonOnColourId,accent); setColour(juce::TextButton::textColourOnId,bg);
}

void SynthLook::drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float position,float start,float end,juce::Slider& s)
{
    auto r=juce::Rectangle<float>(static_cast<float>(x),static_cast<float>(y),static_cast<float>(w),static_cast<float>(h)).reduced(12);
    const float radius=std::min(r.getWidth(),r.getHeight())*0.5f,cx=r.getCentreX(),cy=r.getCentreY();
    const auto active=s.isEnabled()?accent:muted.withAlpha(0.25f);
    juce::Path track; track.addCentredArc(cx,cy,radius,radius,0,start,end,true);
    g.setColour(line); g.strokePath(track,juce::PathStrokeType(3.0f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    juce::Path arc; arc.addCentredArc(cx,cy,radius,radius,0,start,start+position*(end-start),true);
    g.setColour(active); g.strokePath(arc,juce::PathStrokeType(3.0f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    g.setColour(bg.brighter(0.055f)); g.fillEllipse(cx-radius+7,cy-radius+7,2*radius-14,2*radius-14);
    g.setColour(line); g.drawEllipse(cx-radius+7,cy-radius+7,2*radius-14,2*radius-14,1);
    const auto angle=start+position*(end-start);
    g.setColour(active); g.drawLine(cx+std::sin(angle)*radius*0.40f,cy-std::cos(angle)*radius*0.40f,
                                  cx+std::sin(angle)*radius*0.72f,cy-std::cos(angle)*radius*0.72f,3);
}

MakeSynthEditor::Knob::Knob(juce::AudioProcessorValueTreeState& state,const char* id,const char* title,const char* suffix,const char* tip)
{
    label.setText(title,juce::dontSendNotification); label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(13.0f,juce::Font::bold)); label.setColour(juce::Label::textColourId,text);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,115,25);
    slider.setTextValueSuffix(suffix); slider.setTooltip(tip);
    slider.setNumDecimalPlacesToDisplay(2);
    slider.setRotaryParameters(juce::MathConstants<float>::pi*1.2f,juce::MathConstants<float>::pi*2.8f,true);
    addAndMakeVisible(label); addAndMakeVisible(slider);
    attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state,id,slider);
    const auto control=juce::String(id);
    slider.textFromValueFunction=[control](double v)
    {
        const int places=control=="rate" || control=="fmRatio" ? 3 :
                         control=="frequency" || control=="cutoff" || control=="detune" || control=="output" ? 1 : 2;
        return juce::String(v,places);
    };
    slider.updateText();
}
void MakeSynthEditor::Knob::resized()
{
    auto r=getLocalBounds(); label.setBounds(r.removeFromTop(25)); slider.setBounds(r);
}

MakeSynthEditor::MakeSynthEditor(MakeSynthProcessor& p)
    : AudioProcessorEditor(p),processor(p),
      pitch(p.state,"frequency","DRONE PITCH"," Hz","Pitch of the held drone. MIDI notes set their own pitch."),
      cutoff(p.state,"cutoff","FILTER"," Hz","Low-pass cutoff, or band-pass centre in Breathing Noise."),
      resonance(p.state,"resonance","RESONANCE","","Emphasise frequencies around the filter cutoff."),
      rate(p.state,"rate","MOTION RATE"," Hz","Speed of the slow modulation. 0.05 Hz is a 20-second cycle."),
      motion(p.state,"motion","MOTION DEPTH","","Filter sweep depth; in Metallic Drone, a small amount of pitch drift."),
      detune(p.state,"detune","DETUNE"," cents","Move the two oscillators apart for slow beating."),
      fmRatio(p.state,"fmRatio","FM RATIO"," x","Modulator frequency relative to the played pitch."),
      fmDepth(p.state,"fmDepth","FM DEPTH","","Add sidebands and metallic complexity."),
      breath(p.state,"breath","BREATHING","","Loudness movement. Zero is steady; one fades towards silence."),
      space(p.state,"space","SPACE","","Blend a stereo reverb into the sound."),
      output(p.state,"output","OUTPUT"," dB","Master output level after the reverb.")
{
    setLookAndFeel(&look);
    for (auto* k : {&pitch,&cutoff,&resonance,&rate,&motion,&detune,&fmRatio,&fmDepth,&breath,&space,&output}) addAndMakeVisible(k);
    mode.addItemList({"01  Detuned Drone","02  Breathing Noise","03  Metallic Drone"},1);
    noise.addItemList({"Pink noise","White noise"},1);
    mode.setTooltip("Choose one of the three synth patches.");
    drone.setClickingTogglesState(true); drone.setTooltip("Latch a continuous drone. Switch off for a gradual release.");
    stop.setTooltip("Stop held notes, the drone, and the reverb tail.");
    for (auto* c : std::initializer_list<juce::Component*>{&mode,&noise,&drone,&stop}) addAndMakeVisible(c);
    modeAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.state,"mode",mode);
    noiseAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.state,"noise",noise);
    droneAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state,"drone",drone);
    stop.onClick=[this]
    {
        auto* parameter=processor.state.getParameter("drone");
        parameter->beginChangeGesture(); parameter->setValueNotifyingHost(0); parameter->endChangeGesture();
        processor.requestPanic();
    };
    setResizable(true,true); setResizeLimits(900,620,1440,992); setSize(1000,690);
    updateMode(); startTimerHz(30);
}
MakeSynthEditor::~MakeSynthEditor() { stopTimer(); setLookAndFeel(nullptr); }

void MakeSynthEditor::updateMode()
{
    const auto m=static_cast<int>(processor.state.getRawParameterValue("mode")->load());
    if (m==selectedMode) return;
    selectedMode=m;
    look.accent=m==0?juce::Colour(0xff63d9cb):m==1?juce::Colour(0xff87bfe9):juce::Colour(0xffc4a3ff);
    look.setColour(juce::TextButton::buttonOnColourId,look.accent);
    detune.setVisible(m==0); breath.setVisible(m==1); noise.setVisible(m==1);
    fmRatio.setVisible(m==2); fmDepth.setVisible(m==2); pitch.setEnabled(m!=1);
    resized(); repaint();
}
void MakeSynthEditor::timerCallback()
{
    updateMode(); meter=std::max(processor.outputPeak.load(),meter*0.88f);
    repaint(0,getHeight()-55,getWidth(),55);
}

void MakeSynthEditor::resized()
{
    const int w=getWidth(),h=getHeight(),margin=28;
    mode.setBounds(w/3,29,w/3,40); drone.setBounds(w-220,29,105,40); stop.setBounds(w-105,29,77,40);
    const int cell=(w-2*margin)/5,knobH=(h-315)/2;
    int x=margin;
    for (auto* k : {&pitch,&cutoff,&resonance,&rate,&motion}) { k->setBounds(x,238,cell,knobH); x+=cell; }
    const int y=238+knobH+7;
    detune.setBounds(margin,y,cell,knobH); breath.setBounds(margin,y,cell,knobH);
    fmRatio.setBounds(margin,y,cell,knobH); fmDepth.setBounds(margin+cell,y,cell,knobH);
    noise.setBounds(margin+cell+15,y+64,cell-30,36);
    space.setBounds(margin+3*cell,y,cell,knobH); output.setBounds(margin+4*cell,y,cell,knobH);
}

void MakeSynthEditor::paint(juce::Graphics& g)
{
    g.fillAll(bg);
    g.setColour(text); g.setFont(juce::FontOptions(28.0f,juce::Font::bold)); g.drawText("MAKE SYNTH",28,21,270,38,juce::Justification::centredLeft);
    g.setColour(muted); g.setFont(juce::FontOptions(12.0f)); g.drawText("THREE SLOW MACHINES",29,59,250,22,juce::Justification::centredLeft);
    auto r=juce::Rectangle<float>(28,105,static_cast<float>(getWidth()-56),108);
    g.setColour(panel); g.fillRoundedRectangle(r,10);
    const juce::StringArray routes=selectedMode==0?juce::StringArray{"TWO TRIANGLES","LOW-PASS","HELD VOICE","SPACE"}:
                                  selectedMode==1?juce::StringArray{"NOISE","BAND-PASS","SLOW SWELLS","SPACE"}:
                                                  juce::StringArray{"FM PAIR","LOW-PASS","HELD VOICE","SPACE"};
    const int cw=(getWidth()-104)/4;
    for (int i=0;i<4;++i)
    {
        auto box=juce::Rectangle<int>(52+i*cw,125,cw-24,42);
        g.setColour(look.accent.withAlpha(0.10f)); g.fillRoundedRectangle(box.toFloat(),6);
        g.setColour(look.accent); g.setFont(juce::FontOptions(13.0f,juce::Font::bold)); g.drawText(routes[i],box,juce::Justification::centred);
        if (i<3) { g.setColour(muted); g.drawArrow(juce::Line<float>(static_cast<float>(box.getRight()+4),146,static_cast<float>(box.getRight()+19),146),1.2f,5,4); }
    }
    g.setColour(muted); g.setFont(juce::FontOptions(13.0f));
    const juce::String caption=selectedMode==0?"Tune close. Let the beating and filter drift do the work.":
                              selectedMode==1?"One slow motion opens the colour and lets the noise breathe.":
                                              "Bring in FM depth, then move the ratio away from simple octaves.";
    g.drawText(caption,52,176,getWidth()-104,23,juce::Justification::centredLeft);
    const int cell=(getWidth()-56)/5,y=238+(getHeight()-315)/2+7;
    if (selectedMode!=2)
    {
        const auto desc=selectedMode==0?"A few cents can make\na whole landscape.":"Pink is softer.\nWhite carries more air.";
        g.setColour(muted); g.setFont(juce::FontOptions(15.0f));
        g.drawFittedText(desc,28+cell+(selectedMode==1?cell:0),y+102,selectedMode==1?cell:2*cell-15,65,juce::Justification::centred,3);
    }
    g.setColour(line); g.drawHorizontalLine(getHeight()-47,28,static_cast<float>(getWidth()-28));
    g.setColour(muted); g.setFont(juce::FontOptions(12.0f));
    g.drawText("MONOPHONIC MIDI  /  LATCH DRONE TO SUSTAIN  /  4x",28,getHeight()-36,getWidth()-280,23,juce::Justification::centredLeft);
    const float level=juce::jlimit(0.0f,1.0f,(juce::Decibels::gainToDecibels(meter,-60.0f)+60.0f)/60.0f);
    g.setColour(line); g.fillRoundedRectangle(static_cast<float>(getWidth()-210),static_cast<float>(getHeight()-29),128,7,3);
    g.setColour(look.accent); g.fillRoundedRectangle(static_cast<float>(getWidth()-210),static_cast<float>(getHeight()-29),128*level,7,3);
    g.setColour(muted); g.drawText(meter>0.0001f?"LIVE":"IDLE",getWidth()-73,getHeight()-37,50,23,juce::Justification::centredRight);
}
