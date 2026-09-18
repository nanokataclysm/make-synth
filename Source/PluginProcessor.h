#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "SynthEngine.h"
#include <atomic>

class MakeSynthProcessor final : public juce::AudioProcessor
{
public:
    MakeSynthProcessor();
    const juce::String getName() const override { return "Make Synth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Make Synth"; }
    void changeProgramName(int, const juce::String&) override {}
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    void requestPanic() noexcept { panic.store(true); }
    juce::AudioProcessorValueTreeState state;
    std::atomic<float> outputPeak {0};

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    // Index-addressed: readParameters() and the MIDI CC table below both use
    // these positions, so new entries are appended, never inserted.
    static constexpr std::array<const char*,21> ids {
        "mode","drone","frequency","cutoff","resonance","rate","motion",
        "detune","fmRatio","fmDepth","breath","noise","space","output",
        "wave","width","patchLevel",
        "cvCutoffAmount","cvPitchAmount","cvFmAmount","cvWidthAmount"};
    float value(size_t i, float fallback = 0) const noexcept;
    makesynth::Parameters readParameters() const noexcept;
    void handleMidi(const juce::MidiMessage&) noexcept;
    void selectNote() noexcept;
    void clearNotes() noexcept;
    struct Note { bool down = false, held = false; float velocity = 0; uint64_t order = 0; };
    std::array<std::array<Note,128>,16> notes {};
    std::array<bool,16> sustain {};
    std::array<float,16> bend {};
    uint64_t noteOrder = 0;
    void setParameterFromMidi(size_t i, float normalizedValue) noexcept;
    std::array<std::atomic<float>*,21> values {};
    std::array<juce::RangedAudioParameter*,21> params {};
    makesynth::SynthEngine engine;
    juce::dsp::Oversampling<float> oversampling;
    juce::Reverb reverb;
    juce::SmoothedValue<float,juce::ValueSmoothingTypes::Multiplicative> master;
    juce::SmoothedValue<float> wetMix;
    juce::AudioBuffer<float> dry;
    juce::AudioBuffer<float> patchScratch;
    int maximumBlock = 512;
    std::atomic<bool> panic {false};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MakeSynthProcessor)
};
