#include "PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void require(bool ok,const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
void set(MakeSynthProcessor& p,const char* id,float value)
{
    auto* parameter=p.state.getParameter(id);
    require(parameter!=nullptr,"Unknown parameter");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
double rms(const juce::AudioBuffer<float>& b,int start=0,int count=-1)
{
    if (count<0) count=b.getNumSamples()-start;
    double sum=0;
    for (int i=start;i<start+count;++i)
    {
        const auto x=b.getSample(0,i);
        require(std::isfinite(x) && std::abs(x)<=0.981f,"Output is nonfinite or exceeds its ceiling");
        sum+=x*x;
    }
    return count>0?std::sqrt(sum/count):0;
}
// Two deterministic renders that only differ in one fed signal: their per-sample
// difference isolates that signal's own contribution from anything the two runs
// share (e.g. an oscillator hum), which a plain RMS threshold cannot.
double diffRms(const juce::AudioBuffer<float>& a,const juce::AudioBuffer<float>& b,int start,int count)
{
    double sum=0;
    for (int i=start;i<start+count;++i)
    {
        const auto d=a.getSample(0,i)-b.getSample(0,i);
        sum+=d*d;
    }
    return std::sqrt(sum/count);
}
juce::AudioBuffer<float> render(MakeSynthProcessor& p,int count,int block=257,juce::MidiBuffer events={})
{
    juce::AudioBuffer<float> result(2,count);
    for (int start=0;start<count;start+=block)
    {
        const auto n=std::min(block,count-start);
        juce::AudioBuffer<float> b(2,n);
        juce::MidiBuffer midi;
        midi.addEvents(events,start,n,-start);
        p.processBlock(b,midi);
        require(midi.isEmpty(),"Instrument must consume MIDI input");
        for (int c=0;c<2;++c) result.copyFrom(c,start,b,c,0,n);
    }
    return result;
}
// Renders with the Patch In bus enabled, feeding a sine into it.
juce::AudioBuffer<float> renderWithPatch(MakeSynthProcessor& p, int count, float amplitude,
                                         double radiansPerSample, int block = 257)
{
    juce::AudioBuffer<float> result(2, count);
    int phase = 0;
    for (int start = 0; start < count; start += block)
    {
        const auto n = std::min(block, count - start);
        juce::AudioBuffer<float> b(p.getTotalNumOutputChannels(), n);
        b.clear();
        // Patch In is input bus 0, which shares channels 0-1 with the main output.
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                b.setSample(c, i, amplitude * static_cast<float>(std::sin((phase + i) * radiansPerSample)));
        phase += n;
        juce::MidiBuffer midi;
        p.processBlock(b, midi);
        for (int c = 0; c < 2; ++c) result.copyFrom(c, start, b, c, 0, n);
    }
    return result;
}
void setup(MakeSynthProcessor& p,double sr=48000,int block=512)
{
    p.setRateAndBufferSizeDetails(sr,block);
    p.prepareToPlay(sr,block);
}
void tests()
{
    MakeSynthProcessor p;
    set(p,"space",0); setup(p);
    require(rms(render(p,4800))==0,"New instrument should be silent");
    require(p.getLatencySamples()>0,"Oversampling latency must be reported");
    {
        require(p.getBusCount(true) == 1, "Expected exactly one input bus");
        require(p.getBusCount(false) == 3, "Expected three output buses");
        require(p.getBus(true, 0)->getName() == "Patch In", "Input bus 0 must be Patch In");
        require(p.getBus(false, 1)->getName() == "Pre-Filter", "Output bus 1 must be Pre-Filter");
        require(p.getBus(false, 2)->getName() == "Post-Filter", "Output bus 2 must be Post-Filter");
        require(!p.getBus(true, 0)->isEnabledByDefault(), "Patch In must default to disabled");
        require(!p.getBus(false, 1)->isEnabledByDefault(), "Pre-Filter must default to disabled");
        require(!p.getBus(false, 2)->isEnabledByDefault(), "Post-Filter must default to disabled");

        using Set = juce::AudioChannelSet;
        auto layoutOf = [](Set in, Set out0, Set out1, Set out2)
        {
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add(in);
            l.outputBuses.add(out0); l.outputBuses.add(out1); l.outputBuses.add(out2);
            return l;
        };
        const auto none = Set::disabled();
        require(p.checkBusesLayoutSupported(layoutOf(none, Set::stereo(), none, none)),
                "Stereo out with everything else off must be supported");
        require(p.checkBusesLayoutSupported(layoutOf(Set::stereo(), Set::stereo(), none, none)),
                "Stereo patch input must be supported");
        require(p.checkBusesLayoutSupported(layoutOf(Set::mono(), Set::stereo(), none, none)),
                "Mono patch input must be supported");
        require(p.checkBusesLayoutSupported(layoutOf(none, Set::stereo(), Set::stereo(), Set::stereo())),
                "Both taps enabled must be supported");
        require(!p.checkBusesLayoutSupported(layoutOf(none, Set::mono(), none, none)),
                "Mono main output must be rejected");
        require(!p.checkBusesLayoutSupported(layoutOf(none, none, none, none)),
                "Disabled main output must be rejected");
        require(!p.checkBusesLayoutSupported(layoutOf(none, Set::stereo(), Set::mono(), none)),
                "Mono tap must be rejected");
    }
    {
        // Enabling Patch In sets patchConnected, which holds the envelope gate
        // open on its own -- the local oscillator drones at audible RMS with
        // zero patch signal fed in. A plain "is the output loud enough" check
        // against that drone cannot fail even if the patch signal is destroyed
        // before it is read (e.g. by reverting the capture-before-buffer.clear()
        // fix), so this is differential: render twice with identical settings,
        // once fed a signal and once fed silence, and require the per-sample
        // difference to be substantial.
        auto renderPatched = [](float amplitude)
        {
            MakeSynthProcessor patched;
            auto layout = patched.getBusesLayout();
            layout.inputBuses.getReference(0) = juce::AudioChannelSet::stereo();
            require(patched.setBusesLayout(layout), "Could not enable the Patch In bus");
            set(patched, "space", 0); set(patched, "output", 0); set(patched, "drone", 0);
            set(patched, "patchLevel", 1.0f);
            setup(patched);
            return renderWithPatch(patched, 24000, amplitude, 0.05);
        };
        const auto fed = renderPatched(0.5f);
        const auto baseline = renderPatched(0.0f);
        const auto diff = diffRms(fed, baseline, 0, 24000);
        std::cout << "patch input contribution RMS " << diff << '\n';
        require(diff > 0.01, "Patched audio did not survive into the output");
    }
    {
        // A host may send a single processBlock call far larger than the block
        // size declared at prepareToPlay; the sub-block loop inside
        // processBlock exists to tolerate that. patchScratch must be sized to
        // survive it too, not just the declared maximumBlock.
        //
        // An enabled Patch In bus also holds the engine's envelope open on its
        // own (patchConnected), so the oscillator hums even when no patch
        // signal is actually fed in. Non-zero output past the old capacity is
        // therefore not proof the patch signal survived; compare against a
        // silent-patch baseline at the same offsets to isolate its contribution.
        auto renderPatched = [](float amplitude)
        {
            MakeSynthProcessor patched;
            auto layout = patched.getBusesLayout();
            layout.inputBuses.getReference(0) = juce::AudioChannelSet::stereo();
            require(patched.setBusesLayout(layout), "Could not enable the Patch In bus");
            set(patched, "space", 0); set(patched, "output", 0); set(patched, "drone", 0);
            setup(patched, 48000, 256);
            return renderWithPatch(patched, 4000, amplitude, 0.05, 2000);
        };
        const auto fed = renderPatched(0.5f);
        const auto baseline = renderPatched(0.0f);
        const auto earlyDiff = diffRms(fed, baseline, 0, 256);
        const auto lateDiff = diffRms(fed, baseline, 1500, 500);
        std::cout << "oversized-buffer patch contribution early " << earlyDiff << ", late " << lateDiff << '\n';
        require(earlyDiff > 0.01, "Patch signal missing from the start of an oversized host buffer");
        require(lateDiff > 0.01,
                "Patch signal did not survive past prepareToPlay's block size within an oversized host buffer");
    }
    {
        MakeSynthProcessor tapped;
        auto layout = tapped.getBusesLayout();
        layout.outputBuses.getReference(1) = juce::AudioChannelSet::stereo();
        layout.outputBuses.getReference(2) = juce::AudioChannelSet::stereo();
        require(tapped.setBusesLayout(layout), "Could not enable the tap buses");
        set(tapped, "drone", 1); set(tapped, "output", 0); set(tapped, "space", 0);
        // A high drone pitch well above a low, static cutoff (motion disabled so the
        // cutoff does not sweep) so the post-filter tap's energy must visibly drop
        // relative to the pre-filter tap's, pinning which tap is which rather than
        // just confirming both are non-silent.
        set(tapped, "frequency", 800); set(tapped, "cutoff", 40);
        set(tapped, "resonance", 0); set(tapped, "motion", 0); set(tapped, "breath", 0);
        setup(tapped);

        // Derived rather than hardcoded: a disabled bus contributes zero channels,
        // so later buses pack down and Post-Filter's offset is not always 4.
        const int preOffset = tapped.getChannelIndexInProcessBlockBuffer(false, 1, 0);
        const int postOffset = tapped.getChannelIndexInProcessBlockBuffer(false, 2, 0);

        juce::AudioBuffer<float> b(tapped.getTotalNumOutputChannels(), 4096);
        b.clear();
        juce::MidiBuffer midi;
        tapped.processBlock(b, midi);

        double pre = 0, post = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto a = b.getSample(preOffset, i), c = b.getSample(postOffset, i);
            require(std::isfinite(a) && std::isfinite(c), "Tap output is non-finite");
            pre += a * a; post += c * c;
        }
        require(pre > 0.0001, "Pre-filter tap produced no audio");
        require(post > 0.0001, "Post-filter tap produced no audio");
        require(post < pre * 0.5,
                "Post-filter tap did not show the filter's attenuation (pre/post may be swapped)");
        std::cout << "taps pre " << std::sqrt(pre / 4096) << " post " << std::sqrt(post / 4096) << '\n';

        // With the tap buses off, nothing beyond the main pair may be written.
        MakeSynthProcessor plain;
        set(plain, "drone", 1); set(plain, "output", 0);
        setup(plain);
        require(plain.getTotalNumOutputChannels() == 2,
                "Disabled taps must not add output channels");
        juce::AudioBuffer<float> narrow(2, 4096);
        narrow.clear();
        juce::MidiBuffer none;
        plain.processBlock(narrow, none);
        require(rms(narrow) > 0.0001, "Main output went silent when taps were disabled");
    }
    {
        // Disabled buses pack down in the process-block buffer: enabling Post-Filter
        // alone (Pre-Filter left disabled) moves its real offset to where Pre-Filter
        // would otherwise sit. A hardcoded channel index for a single-tap host layout
        // silently misses this and writes nowhere.
        auto tapOnly = [](int busIndex) -> double
        {
            MakeSynthProcessor tp;
            auto layout = tp.getBusesLayout();
            layout.outputBuses.getReference(busIndex) = juce::AudioChannelSet::stereo();
            require(tp.setBusesLayout(layout), "Could not enable a single tap bus");
            set(tp, "drone", 1); set(tp, "output", 0); set(tp, "space", 0);
            setup(tp);
            require(tp.getTotalNumOutputChannels() == 4,
                    "Enabling a single tap bus must add exactly two channels");
            const int offset = tp.getChannelIndexInProcessBlockBuffer(false, busIndex, 0);
            juce::AudioBuffer<float> buf(tp.getTotalNumOutputChannels(), 4096);
            buf.clear();
            juce::MidiBuffer midi;
            tp.processBlock(buf, midi);
            double energy = 0;
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const auto x = buf.getSample(offset, i);
                require(std::isfinite(x), "Single tap output is non-finite");
                energy += x * x;
            }
            return energy;
        };
        require(tapOnly(1) > 0.0001, "Pre-Filter-only tap produced no audio");
        require(tapOnly(2) > 0.0001, "Post-Filter-only tap produced no audio");
    }
    for (int mode=0;mode<3;++mode)
    {
        set(p,"mode",static_cast<float>(mode)); set(p,"drone",1); setup(p);
        auto audio=render(p,48000,777);
        const auto level=rms(audio);
        require(level>0.0001,"Drone mode does not produce audio");
        std::cout << "Plugin mode " << mode << " RMS " << level << '\n';
        p.requestPanic(); set(p,"drone",0);
        require(rms(render(p,1024))==0,"Panic must silence voices and effects immediately");
    }

    set(p,"mode",0); set(p,"drone",0); setup(p);
    juce::MidiBuffer events;
    events.addEvent(juce::MidiMessage::noteOn(1,57,0.8f),137);
    events.addEvent(juce::MidiMessage::noteOff(1,57),24000);
    auto audio=render(p,144000,509,events);
    require(rms(audio,0,137)==0,"MIDI note started before its sample offset");
    require(rms(audio,3000,12000)>0.001,"MIDI note did not sound");
    require(rms(audio,140000,4000)<0.000001,"MIDI note did not release");

    // The same MIDI stream must give the same result across host block sizes,
    // including blocks larger than prepareToPlay's advertised maximum.
    events.clear();
    events.addEvent(juce::MidiMessage::noteOn(2,45,0.7f),23);
    events.addEvent(juce::MidiMessage::controllerEvent(2,64,127),501);
    events.addEvent(juce::MidiMessage::noteOn(1,64,0.5f),900);
    events.addEvent(juce::MidiMessage::noteOff(1,64),1701);
    events.addEvent(juce::MidiMessage::pitchWheel(2,12000),2200);
    events.addEvent(juce::MidiMessage::noteOff(2,45),2601);
    events.addEvent(juce::MidiMessage::controllerEvent(2,64,0),8100);
    setup(p,48000,64); auto a=render(p,12000,31,events);
    setup(p,48000,64); auto b=render(p,12000,1027,events);
    for (int i=0;i<a.getNumSamples();++i)
        require(std::abs(a.getSample(0,i)-b.getSample(0,i))<0.000002f,"MIDI timing depends on block size");
    require(rms(a,5000,2000)>0.001,"Sustain pedal failed to hold a released note");

    events.clear();
    events.addEvent(juce::MidiMessage::controllerEvent(1, 74, 100), 10);
    events.addEvent(juce::MidiMessage::controllerEvent(1, 71, 90), 20);
    events.addEvent(juce::MidiMessage::controllerEvent(1, 1, 80), 30);
    render(p, 512, 512, events);
    require(std::abs(p.state.getRawParameterValue("cutoff")->load() - p.state.getParameter("cutoff")->convertFrom0to1(100.0f / 127.0f)) < 1.0f, "MIDI CC 74 must control filter cutoff");
    require(std::abs(p.state.getRawParameterValue("resonance")->load() - (90.0f / 127.0f)) < 0.01f, "MIDI CC 71 must control resonance");
    require(std::abs(p.state.getRawParameterValue("motion")->load() - (80.0f / 127.0f)) < 0.01f, "MIDI CC 1 must control motion in mode 0");

    // Every oscillator wave must survive the full plugin path at its narrowest
    // pulse width, which is the loudest and most discontinuous setting.
    set(p,"mode",0); set(p,"drone",1); set(p,"width",0.15f); set(p,"output",-6);
    for (int wave=0;wave<5;++wave)
    {
        set(p,"wave",static_cast<float>(wave)); setup(p);
        const auto level=rms(render(p,48000));
        require(level>0.0001,"An oscillator wave produces no audio");
        std::cout << "Plugin wave " << wave << " RMS " << level << '\n';
        p.requestPanic();
    }
    set(p,"drone",0); set(p,"width",0.35f); set(p,"wave",1); set(p,"output",-18);

    events.clear();
    events.addEvent(juce::MidiMessage::controllerEvent(1, 70, 127), 10);
    events.addEvent(juce::MidiMessage::controllerEvent(1, 79, 64), 20);
    setup(p); render(p, 512, 512, events);
    require(p.state.getRawParameterValue("wave")->load() == 4, "MIDI CC 70 must select the oscillator wave");
    require(std::abs(p.state.getRawParameterValue("width")->load()
                     - p.state.getParameter("width")->convertFrom0to1(64.0f / 127.0f)) < 0.01f,
            "MIDI CC 79 must control pulse width");
    set(p,"wave",1);

    {
        MakeSynthProcessor cc;
        setup(cc);
        juce::MidiBuffer events;
        events.addEvent(juce::MidiMessage::controllerEvent(1, 85, 100), 10);
        render(cc, 512, 512, events);
        require(std::abs(cc.state.getRawParameterValue("patchLevel")->load() - (100.0f/127.0f)) < 0.01f,
                "MIDI CC 85 must control patch level");
    }

    {
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        require(ed != nullptr, "Editor creation failed");
        std::function<void(juce::Component*)> walk = [&](juce::Component* c)
        {
            if (auto* s = dynamic_cast<juce::Slider*>(c))
            {
                const auto t = s->getTextFromValue(s->getValue());
                require(!t.contains("Hz Hz") && !t.contains("dB dB") && !t.contains("cents cents"),
                        "Knob readout duplicates its unit suffix");
            }
            for (int i = 0; i < c->getNumChildComponents(); ++i)
                walk(c->getChildComponent(i));
        };
        walk(ed.get());
    }

    setup(p);
    juce::AudioBuffer<float> zero(2,0); juce::MidiBuffer zeroMidi;
    zeroMidi.addEvent(juce::MidiMessage::noteOn(1,60,0.8f),0);
    p.processBlock(zero,zeroMidi);
    require(rms(render(p,4096))>0.001,"Zero-length control block lost MIDI");

    set(p,"mode",2); set(p,"fmDepth",3.2f); set(p,"frequency",73.4f);
    set(p,"output",-22); set(p,"drone",1);
    juce::MemoryBlock saved; p.getStateInformation(saved);
    MakeSynthProcessor restored;
    restored.setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));
    for (const auto* id:{"mode","fmDepth","frequency","output","drone"})
        require(std::abs(p.state.getRawParameterValue(id)->load()-restored.state.getRawParameterValue(id)->load())<0.0001f,"Parameter state roundtrip failed");
    const char garbage[]="not a preset";
    restored.setStateInformation(garbage,sizeof(garbage));
    require(restored.state.getRawParameterValue("mode")->load()==2,"Invalid preset corrupted state");

    for (double sr:{22050.0,44100.0,96000.0})
    {
        set(p,"cutoff",18000); set(p,"resonance",1); set(p,"output",0);
        setup(p,sr,1);
        require(rms(render(p,4096,1024))>0,"Reprepare/sample-rate change failed");
    }
    setup(p); const auto start=std::chrono::steady_clock::now();
    rms(render(p,480000,512));
    const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cout << "Rendered 10 seconds in " << elapsed << " seconds (" << 100*elapsed/10 << "% of one core)\n";
    std::cout << "Processor checks passed\n";
}
void demos(const juce::File& directory)
{
    require(directory.createDirectory().wasOk(),"Cannot create render directory");
    const char* names[]={"01-detuned-drone","02-breathing-noise","03-metallic-drone"};
    for (int mode=0;mode<3;++mode)
    {
        MakeSynthProcessor p;
        set(p,"mode",static_cast<float>(mode)); set(p,"drone",1);
        if (mode==1) { set(p,"rate",0.16f); set(p,"breath",0.85f); set(p,"motion",0.5f); set(p,"output",-8); }
        if (mode==2) { set(p,"fmDepth",1.8f); set(p,"cutoff",2300); }
        setup(p);
        auto audio=render(p,12*48000,512);
        audio.applyGainRamp(0,4800,0,1);
        audio.applyGainRamp(audio.getNumSamples()-24000,24000,1,0);
        const auto file=directory.getChildFile(juce::String(names[mode])+".wav");
        require(!file.existsAsFile() || file.deleteFile(),"Cannot replace previous demo");
        std::unique_ptr<juce::OutputStream> stream=file.createOutputStream();
        require(stream!=nullptr,"Cannot open WAV file");
        juce::WavAudioFormat format;
        auto options=juce::AudioFormatWriterOptions{}.withSampleRate(48000).withNumChannels(2).withBitsPerSample(24);
        auto writer=format.createWriterFor(stream,options);
        require(writer!=nullptr && writer->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples()),"WAV encoding failed");
        std::cout << file.getFullPathName() << " RMS " << rms(audio) << '\n';
    }
}
void snapshot(const juce::File& directory)
{
    require(directory.createDirectory().wasOk(),"Cannot create snapshot directory");
    for (int mode=0;mode<3;++mode)
    {
        MakeSynthProcessor p; set(p,"mode",static_cast<float>(mode));
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        const auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.0f);
        const auto file=directory.getChildFile("mode-"+juce::String(mode+1)+".png");
        require(!file.existsAsFile() || file.deleteFile(),"Cannot replace screenshot");
        auto output=file.createOutputStream(); juce::PNGImageFormat png;
        require(output!=nullptr && png.writeImageToStream(image,*output),"Screenshot write failed");
        std::cout << file.getFullPathName() << '\n';
    }
}
}
int main(int argc,char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialise;
    try
    {
        if (argc==2 && juce::String(argv[1])=="--test") tests();
        else if (argc==3 && juce::String(argv[1])=="--render") demos(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]));
        else if (argc==3 && juce::String(argv[1])=="--snapshot") snapshot(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]));
        else { std::cout << "MakeSynthRender --test | --render DIRECTORY | --snapshot DIRECTORY\n"; return 2; }
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
