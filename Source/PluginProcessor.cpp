#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout MakeSynthProcessor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"mode",1},"Synth mode",
          juce::StringArray{"Detuned Drone","Breathing Noise","Metallic Drone"},0));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"drone",1},"Drone hold",false));
    auto add = [&p](const char* id, const char* name, float min, float max, float def, float centre = 0.0f)
    {
        juce::NormalisableRange<float> r(min,max);
        if (centre > min && centre < max) r.setSkewForCentre(centre);
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},name,r,def));
    };
    add("frequency","Drone pitch",20,1000,110,110);
    add("cutoff","Filter frequency",30,18000,1200,1000);
    add("resonance","Resonance",0,1,0.15f);
    add("rate","Motion rate",0.005f,5,0.05f,0.1f);
    add("motion","Motion depth",0,1,0.15f);
    add("detune","Detune",0,35,6);
    add("fmRatio","FM ratio",0.125f,8,1.4142f,1.5f);
    add("fmDepth","FM depth",0,5,0.8f);
    add("breath","Breathing depth",0,1,0.45f);
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"noise",1},"Noise colour",
          juce::StringArray{"Pink","White"},0));
    add("space","Space",0,0.65f,0.15f);
    add("output","Output level",-48,0,-18);
    // Triangle is index 1 and the default, so patches saved before the wave
    // selector existed still reload as the original detuned triangle pair.
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"wave",1},"Oscillator wave",
          juce::StringArray{"Sine","Triangle","Saw","Square","Pulse"},1));
    add("width","Pulse width",0.15f,0.85f,0.35f);
    return p;
}

MakeSynthProcessor::MakeSynthProcessor()
    : AudioProcessor(BusesProperties()
          .withOutput("Output",      juce::AudioChannelSet::stereo(), true)
          .withInput ("Patch In",    juce::AudioChannelSet::stereo(), false)
          .withOutput("Pre-Filter",  juce::AudioChannelSet::stereo(), false)
          .withOutput("Post-Filter", juce::AudioChannelSet::stereo(), false)),
      state(*this,nullptr,"MakeSynthState",layout()),
      oversampling(2,2,juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,true,true)
{
    for (size_t i=0; i<ids.size(); ++i)
    {
        values[i]=state.getRawParameterValue(ids[i]);
        params[i]=state.getParameter(ids[i]);
    }
    clearNotes();
}

void MakeSynthProcessor::setParameterFromMidi(size_t i, float normalizedValue) noexcept
{
    if (i < params.size() && params[i] != nullptr)
        params[i]->setValueNotifyingHost(std::clamp(normalizedValue, 0.0f, 1.0f));
}

float MakeSynthProcessor::value(size_t i, float fallback) const noexcept
{
    const auto x=values[i]->load(std::memory_order_relaxed);
    return std::isfinite(x) ? x : fallback;
}

makesynth::Parameters MakeSynthProcessor::readParameters() const noexcept
{
    makesynth::Parameters p;
    p.mode=std::clamp(static_cast<int>(value(0)),0,2); p.drone=value(1)>0.5f;
    p.frequency=std::clamp(value(2,110),20.0f,1000.0f);
    p.cutoff=std::clamp(value(3,1200),30.0f,18000.0f);
    p.resonance=std::clamp(value(4),0.0f,1.0f);
    p.rate=std::clamp(value(5,0.05f),0.005f,5.0f);
    p.motion=std::clamp(value(6),0.0f,1.0f);
    p.detune=std::clamp(value(7),0.0f,35.0f);
    p.fmRatio=std::clamp(value(8,1.4142f),0.125f,8.0f);
    p.fmDepth=std::clamp(value(9),0.0f,5.0f);
    p.breath=std::clamp(value(10),0.0f,1.0f); p.pink=value(11)<0.5f;
    p.wave=std::clamp(static_cast<int>(value(14,1)),0,4);
    p.width=std::clamp(value(15,0.35f),0.15f,0.85f);
    return p;
}

bool MakeSynthProcessor::isBusesLayoutSupported(const BusesLayout& b) const
{
    if (b.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    const auto patch = b.getChannelSet(true, 0);
    if (!patch.isDisabled() && patch != juce::AudioChannelSet::mono()
                            && patch != juce::AudioChannelSet::stereo())
        return false;
    for (int bus = 1; bus <= 2; ++bus)
    {
        const auto tap = b.getChannelSet(false, bus);
        if (!tap.isDisabled() && tap != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void MakeSynthProcessor::prepareToPlay(double sr,int block)
{
    maximumBlock=std::clamp(block,1,32768);
    oversampling.initProcessing(static_cast<size_t>(maximumBlock)); oversampling.reset();
    setLatencySamples(static_cast<int>(std::lround(oversampling.getLatencyInSamples())));
    engine.setParameters(readParameters()); engine.prepare(sr*4);
    reverb.setSampleRate(sr); reverb.reset();
    juce::Reverb::Parameters rp;
    rp.roomSize=0.78f; rp.damping=0.55f; rp.width=1.0f; rp.wetLevel=1.0f; rp.dryLevel=0.0f; rp.freezeMode=0;
    reverb.setParameters(rp);
    dry.setSize(2,maximumBlock,false,false,true);
    patchScratch.setSize(1,maximumBlock,false,false,true);
    master.reset(sr,0.03); master.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(std::clamp(value(13,-18),-48.0f,0.0f)));
    wetMix.reset(sr,0.04); wetMix.setCurrentAndTargetValue(std::clamp(value(12),0.0f,0.65f));
    clearNotes(); outputPeak.store(0);
}

void MakeSynthProcessor::clearNotes() noexcept
{
    for (auto& channel : notes) for (auto& n : channel) n={};
    sustain.fill(false); bend.fill(1.0f); noteOrder=0;
    engine.setNote(false,110);
}

void MakeSynthProcessor::selectNote() noexcept
{
    uint64_t order=0; int chosen=-1,channel=0; float velocity=0;
    for (int c=0;c<16;++c) for (int n=0;n<128;++n)
    {
        const auto& x=notes[static_cast<size_t>(c)][static_cast<size_t>(n)];
        if (x.held && x.order>order) { order=x.order; chosen=n; channel=c; velocity=x.velocity; }
    }
    if (chosen<0) engine.setNote(false,110);
    else engine.setNote(true,static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(chosen))*bend[static_cast<size_t>(channel)],velocity);
}

void MakeSynthProcessor::handleMidi(const juce::MidiMessage& m) noexcept
{
    const int c=m.getChannel()-1;
    if (c<0 || c>=16) return;
    auto& channel=notes[static_cast<size_t>(c)];
    if (m.isNoteOn())
    {
        auto& n=channel[static_cast<size_t>(m.getNoteNumber())];
        n.down=n.held=true; n.velocity=m.getFloatVelocity(); n.order=++noteOrder;
    }
    else if (m.isNoteOff())
    {
        auto& n=channel[static_cast<size_t>(m.getNoteNumber())];
        n.down=false; if (!sustain[static_cast<size_t>(c)]) n.held=false;
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        const int val = m.getControllerValue();
        if (cc == 64)
        {
            sustain[static_cast<size_t>(c)] = val >= 64;
            if (!sustain[static_cast<size_t>(c)]) for (auto& n:channel) if (!n.down) n.held=false;
        }
        else
        {
            const float v = static_cast<float>(val) / 127.0f;
            const int currentMode = std::clamp(static_cast<int>(value(0)), 0, 2);
            switch (cc)
            {
                case 1:  // Mod Wheel -> primary expressive motion/depth for active mode
                    if (currentMode == 0) setParameterFromMidi(6, v);      // motion
                    else if (currentMode == 1) setParameterFromMidi(10, v);// breath
                    else setParameterFromMidi(9, v);                      // fmDepth
                    break;
                case 11: // Expression pedal
                    if (currentMode == 1) setParameterFromMidi(10, v);     // breath
                    else if (currentMode == 0) setParameterFromMidi(6, v); // motion
                    else setParameterFromMidi(9, v);                      // fmDepth
                    break;
                case 74: setParameterFromMidi(3, v); break;                // Filter cutoff / brightness
                case 71: setParameterFromMidi(4, v); break;                // Filter resonance / timbre
                case 76: case 14: setParameterFromMidi(5, v); break;       // Modulation rate
                case 7:  setParameterFromMidi(13, v); break;               // Master volume -> output
                case 91: setParameterFromMidi(12, v); break;               // Reverb send -> space
                case 77: case 12: setParameterFromMidi(7, v); break;       // Detune (Mode 0)
                case 78: case 13: setParameterFromMidi(8, v); break;       // FM Ratio (Mode 2)
                case 75: case 15: setParameterFromMidi(9, v); break;       // FM Depth (Mode 2)
                case 73: setParameterFromMidi(10, v); break;               // Breathing (Mode 1)
                case 80: case 16: setParameterFromMidi(2, v); break;       // Drone Pitch
                case 65: case 81: setParameterFromMidi(1, val >= 64 ? 1.0f : 0.0f); break; // Drone Latch
                case 82: // Mode switch (0, 1, 2)
                    setParameterFromMidi(0, val < 43 ? 0.0f : (val < 86 ? 0.5f : 1.0f));
                    break;
                case 83: setParameterFromMidi(11, val >= 64 ? 1.0f : 0.0f); break; // Noise color (pink/white)
                case 70: setParameterFromMidi(14, v); break;               // Oscillator wave (Mode 0)
                case 79: setParameterFromMidi(15, v); break;               // Pulse width (Mode 0)
                default: break;
            }
        }
    }
    else if (m.isAllSoundOff()) { for (auto& n:channel) n={}; sustain[static_cast<size_t>(c)]=false; }
    else if (m.isAllNotesOff()) { for (auto& n:channel) { n.down=false; if (!sustain[static_cast<size_t>(c)]) n.held=false; } }
    else if (m.isPitchWheel()) bend[static_cast<size_t>(c)]=std::exp2((m.getPitchWheelValue()-8192)/8192.0f * (2.0f/12.0f));
    else return;
    selectNote();
}

void MakeSynthProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels()<2) return;
    // Input and output buses alias the same memory, so the patch signal must be
    // copied out before the buffer is cleared.
    const bool patchActive = getBus(true,0) != nullptr && getBus(true,0)->isEnabled();
    const int patchSamples = std::min(buffer.getNumSamples(), patchScratch.getNumSamples());
    patchScratch.clear();
    if (patchActive)
    {
        auto patchBus = getBusBuffer(buffer, true, 0);
        const int channels = patchBus.getNumChannels();
        for (int c = 0; c < channels; ++c)
            patchScratch.addFrom(0, 0, patchBus, c, 0, patchSamples, 1.0f / static_cast<float>(channels));
    }
    buffer.clear();
    auto parameters = readParameters();
    parameters.patchConnected = patchActive;
    engine.setParameters(parameters);
    if (panic.exchange(false)) { clearNotes(); engine.reset(); reverb.reset(); oversampling.reset(); }
    master.setTargetValue(juce::Decibels::decibelsToGain(std::clamp(value(13,-18),-48.0f,0.0f)));
    wetMix.setTargetValue(std::clamp(value(12),0.0f,0.65f));
    auto next=midi.cbegin();
    if (buffer.getNumSamples()==0)
    {
        for (const auto metadata : midi)
            if (metadata.numBytes<=3) handleMidi(metadata.getMessage());
        midi.clear();
        return;
    }
    float peak=0;
    juce::dsp::AudioBlock<float> full(buffer);
    for (int start=0;start<buffer.getNumSamples();start+=maximumBlock)
    {
        const auto count=std::min(maximumBlock,buffer.getNumSamples()-start);
        // Seed the block with the patch signal so the existing oversampler
        // upsamples it for us — no second filter chain, no extra latency.
        for (int c=0;c<2;++c)
        {
            auto* channel=buffer.getWritePointer(c,start);
            for (int i=0;i<count;++i)
                channel[i] = start+i < patchSamples ? patchScratch.getSample(0,start+i) : 0.0f;
        }
        auto host=full.getSubsetChannelBlock(0,2).getSubBlock(static_cast<size_t>(start),static_cast<size_t>(count));
        auto high=oversampling.processSamplesUp(host);
        for (size_t i=0;i<high.getNumSamples();++i)
        {
            if ((i&3u)==0)
                while (next!=midi.cend() && (*next).samplePosition<=start+static_cast<int>(i/4))
                { if ((*next).numBytes<=3) handleMidi((*next).getMessage()); ++next; }
            const float x=engine.process(high.getChannelPointer(0)[i]);
            high.getChannelPointer(0)[i]=x; high.getChannelPointer(1)[i]=x;
        }
        oversampling.processSamplesDown(host);
        for (int c=0;c<2;++c) dry.copyFrom(c,0,buffer,c,start,count);
        auto* left=buffer.getWritePointer(0,start); auto* right=buffer.getWritePointer(1,start);
        reverb.processStereo(left,right,count);
        for (int i=0;i<count;++i)
        {
            const float wet=wetMix.getNextValue(), gain=master.getNextValue();
            for (int c=0;c<2;++c)
            {
                auto& x=buffer.getWritePointer(c,start)[i];
                x=((1-wet)*dry.getSample(c,i)+wet*x)*gain;
                if (!std::isfinite(x)) x=0;
                if (std::abs(x)>0.85f) x=std::copysign(0.85f+0.13f*std::tanh((std::abs(x)-0.85f)/0.13f),x);
                peak=std::max(peak,std::abs(x));
            }
        }
    }
    outputPeak.store(peak,std::memory_order_relaxed);
    midi.clear();
}

juce::AudioProcessorEditor* MakeSynthProcessor::createEditor() { return new MakeSynthEditor(*this); }
void MakeSynthProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml=state.copyState().createXml()) copyXmlToBinary(*xml,dest);
}
void MakeSynthProcessor::setStateInformation(const void* data,int size)
{
    if (auto xml=getXmlFromBinary(data,size))
        if (xml->hasTagName(state.state.getType())) state.replaceState(juce::ValueTree::fromXml(*xml));
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MakeSynthProcessor(); }
