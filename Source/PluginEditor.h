#pragma once
#include "PluginProcessor.h"

class SynthLook final : public juce::LookAndFeel_V4
{
public:
    SynthLook();
    void drawRotarySlider(juce::Graphics&,int,int,int,int,float,float,float,juce::Slider&) override;
    juce::Colour accent {0xff63d9cb};
};

class MakeSynthEditor final : public juce::AudioProcessorEditor,private juce::Timer
{
public:
    explicit MakeSynthEditor(MakeSynthProcessor&);
    ~MakeSynthEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Knob final : juce::Component
    {
        Knob(juce::AudioProcessorValueTreeState&,const char*,const char*,const char*,const char*);
        void resized() override;
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void timerCallback() override;
    void updateMode();
    MakeSynthProcessor& processor;
    SynthLook look;
    juce::TooltipWindow tooltip {this,700};
    juce::ComboBox mode,noise;
    juce::TextButton drone {"DRONE"},stop {"STOP"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment,noiseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> droneAttachment;
    Knob pitch,cutoff,resonance,rate,motion,detune,fmRatio,fmDepth,breath,space,output;
    int selectedMode=-1;
    float meter=0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MakeSynthEditor)
};
