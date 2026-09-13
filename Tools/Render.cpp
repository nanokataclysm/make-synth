#include "PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <chrono>
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
