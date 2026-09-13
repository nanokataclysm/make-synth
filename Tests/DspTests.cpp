#include "SynthEngine.h"
#include <iostream>
#include <stdexcept>
#include <vector>

void require(bool ok,const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

int main()
{
    try
    {
        makesynth::SynthEngine engine;
        makesynth::Parameters p;
        engine.prepare(48000);
        for (int i=0;i<48000;++i) require(engine.process()==0,"Default must be silent");

        for (double sr : {32000.0,48000.0,192000.0})
            for (int mode=0;mode<3;++mode)
            {
                p={}; p.mode=mode; p.drone=true;
                engine.setParameters(p); engine.prepare(sr);
                double energy=0;
                for (int i=0;i<static_cast<int>(sr);++i)
                {
                    const auto sample=engine.process();
                    require(std::isfinite(sample) && std::abs(sample)<2,"Invalid default output");
                    energy+=sample*sample;
                }
                require(std::sqrt(energy/sr)>0.001,"A synth mode is inaudible");
                std::cout << "mode " << mode << " @ " << sr << " Hz RMS " << std::sqrt(energy/sr) << '\n';
            }

        p={}; p.drone=true; p.detune=0; p.motion=0; p.frequency=220;
        engine.setParameters(p); engine.prepare(48000);
        for (int i=0;i<24000;++i) engine.process();
        int crossings=0; float previous=engine.process();
        for (int i=0;i<48000;++i)
        {
            const auto x=engine.process();
            if (previous<=0 && x>0) ++crossings;
            previous=x;
        }
        require(std::abs(crossings-220)<=1,"Drone oscillator pitch is incorrect");

        p.drone=false; engine.setParameters(p);
        for (int i=0;i<144000;++i) engine.process();
        require(engine.process()==0,"Released envelope must settle to silence");
        engine.setNote(true,440,0.7f);
        double energy=0;
        for (int i=0;i<24000;++i) { const auto x=engine.process(); energy+=x*x; }
        require(energy>1,"MIDI gate must open the voice");

        // Extreme automation, low and high sample rates, and all filter modes.
        for (double sr : {32000.0,192000.0,384000.0})
            for (int mode=0;mode<3;++mode)
            {
                p={}; p.mode=mode; p.drone=true; p.resonance=1; p.motion=1;
                p.rate=5; p.fmDepth=5; p.fmRatio=8; p.frequency=1000;
                engine.setParameters(p); engine.prepare(sr);
                for (int i=0;i<static_cast<int>(sr/2);++i)
                {
                    if (i%4000==0)
                    {
                        p.cutoff=p.cutoff>1000?30.0f:18000.0f;
                        engine.setParameters(p);
                    }
                    const auto x=engine.process();
                    require(std::isfinite(x) && std::abs(x)<2.1f,"Extreme settings are unstable");
                }
            }

        p={}; p.mode=1; p.drone=true; p.pink=false;
        engine.setParameters(p); engine.prepare(48000);
        std::vector<float> reference(4096);
        for (auto& x:reference) x=engine.process();
        engine.reset();
        for (const auto x:reference) require(x==engine.process(),"Reset must be deterministic");

        p={}; p.mode=2; p.drone=true; p.fmRatio=1; p.fmDepth=1.2f; p.motion=0;
        engine.setParameters(p); engine.prepare(48000);
        for (int i=0;i<48000;++i) engine.process();
        double mean=0;
        for (int i=0;i<96000;++i) mean+=engine.process();
        require(std::abs(mean/96000)<0.002,"FM must not leave a persistent DC offset");
        std::cout << "DSP checks passed\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
