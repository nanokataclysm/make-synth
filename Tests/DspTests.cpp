#include "SynthEngine.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

void require(bool ok,const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

// Fixed scenario used as the bit-identity reference across the patching work.
static std::vector<float> goldenRender()
{
    makesynth::SynthEngine engine;
    makesynth::Parameters p;
    p.mode = 0; p.drone = true; p.frequency = 220; p.detune = 7;
    p.cutoff = 3000; p.resonance = 0.4f; p.motion = 0.3f; p.rate = 1.0f;
    engine.setParameters(p);
    engine.prepare(48000);
    std::vector<float> out(8192);
    for (auto& x : out) x = engine.process();
    return out;
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string(argv[1]) == "--write-golden")
        {
            const auto samples = goldenRender();
            std::ofstream file("Tests/golden-engine.f32", std::ios::binary);
            require(file.good(), "Cannot open golden file for writing");
            file.write(reinterpret_cast<const char*>(samples.data()),
                       static_cast<std::streamsize>(samples.size() * sizeof(float)));
            std::cout << "wrote " << samples.size() << " golden samples\n";
            return 0;
        }

        makesynth::SynthEngine engine;
        makesynth::Parameters p;
        engine.prepare(48000);
        for (int i=0;i<48000;++i) require(engine.process()==0,"Default must be silent");

        {
            std::ifstream file("Tests/golden-engine.f32", std::ios::binary);
            require(file.good(), "Golden reference file is missing — run --write-golden");
            std::vector<float> expected(8192);
            file.read(reinterpret_cast<char*>(expected.data()),
                      static_cast<std::streamsize>(expected.size() * sizeof(float)));
            require(file.gcount() == static_cast<std::streamsize>(expected.size() * sizeof(float)),
                    "Golden reference file is truncated");
            const auto actual = goldenRender();
            // The golden file was captured on Linux/GCC. macOS (arm64, FMA in its
            // baseline ISA) and MSVC contract float expressions differently than
            // glibc/GCC does, and Apple's libm sin/tan/tanh/exp2 differ in their
            // last bit from glibc's — a measured probe showed 358/8192 samples
            // differing by up to 5.96e-08 from toolchain codegen alone with exact
            // equality. A real regression is much larger: perturbing the canary
            // constant in SynthEngine.h's std::tanh(mixed * 0.85) to 0.86 moves
            // samples by ~5.3e-3, five orders of magnitude above the tolerance
            // below, so the guard keeps its full power against real drift while
            // tolerating cross-platform codegen noise.
            for (size_t i = 0; i < expected.size(); ++i)
                require(std::abs(actual[i] - expected[i]) <= 1.0e-5f,
                        "Engine output drifted from the golden reference");
            std::cout << "golden reference matches\n";
        }

        {
            makesynth::SynthEngine e;
            makesynth::Parameters p;
            p.mode = 0; p.drone = true;
            e.setParameters(p); e.prepare(48000);

            // A non-zero patch signal must change the output.
            double silentEnergy = 0, drivenEnergy = 0;
            for (int i = 0; i < 4800; ++i) { const auto x = e.process({0.0f}); silentEnergy += x * x; }
            e.reset();
            for (int i = 0; i < 4800; ++i)
            {
                const auto x = e.process({static_cast<float>(0.5 * std::sin(i * 0.05))});
                drivenEnergy += x * x;
                require(std::isfinite(x), "Patch input produced a non-finite sample");
            }
            require(drivenEnergy != silentEnergy, "Patch input had no effect on the output");

            // Taps must be finite and must differ from each other once the filter bites.
            p.cutoff = 200; p.resonance = 0;
            e.setParameters(p); e.prepare(48000);
            double tapDifference = 0;
            for (int i = 0; i < 4800; ++i)
            {
                e.process({static_cast<float>(0.5 * std::sin(i * 0.3))});
                require(std::isfinite(e.lastPreFilter()), "Pre-filter tap is non-finite");
                require(std::isfinite(e.lastPostFilter()), "Post-filter tap is non-finite");
                tapDifference += std::abs(e.lastPreFilter() - e.lastPostFilter());
            }
            require(tapDifference > 1.0, "Taps are identical; the filter is not in the tap path");

            // Extremes must stay bounded through the tanh stage.
            for (int i = 0; i < 4800; ++i)
            {
                const auto x = e.process({i % 2 ? 50.0f : -50.0f});
                require(std::isfinite(x) && std::abs(x) < 2.0f, "Extreme patch input is unstable");
            }
            std::cout << "patch input checks passed\n";
        }

        {
            makesynth::SynthEngine e;
            makesynth::Parameters p;
            p.mode = 0; p.drone = false; p.patchConnected = true; p.patchLevel = 1.0f;

            // The gate opens for a connected patch bus, which also lets the oscillator
            // sound. Every assertion below therefore measures the DIFFERENCE between an
            // identical pair of runs -- one fed a signal, one fed silence -- so what is
            // measured is the patch contribution and not the oscillator underneath it.
            auto energyWith = [](float level, bool feedSignal, int mode)
            {
                makesynth::SynthEngine engine;
                makesynth::Parameters q;
                q.mode = mode; q.drone = false; q.patchConnected = true; q.patchLevel = level;
                q.cutoff = 3000; q.resonance = 0; q.motion = 0; q.breath = 0;
                engine.setParameters(q); engine.prepare(48000);
                double total = 0;
                for (int i = 0; i < 24000; ++i)
                {
                    const auto x = engine.process({static_cast<float>(feedSignal ? 0.4f * std::sin(i * 0.1) : 0.0f)});
                    total += x * x;
                }
                return total;
            };

            const auto silent = energyWith(1.0f, false, 0);
            require(energyWith(1.0f, true, 0) > silent * 1.05,
                    "Connected patch input must add energy with no note held");

            // patchLevel must scale the contribution monotonically.
            const auto full = energyWith(1.0f, true, 0);
            const auto quarter = energyWith(0.25f, true, 0);
            const auto muted = energyWith(0.0f, true, 0);
            require(full > quarter && quarter > muted, "patchLevel does not scale the patched signal");

            // At level zero the patch path must contribute exactly nothing, so the run is
            // sample-for-sample the silent run.
            require(muted == energyWith(0.0f, false, 0), "patchLevel of zero must mute patched audio");

            // The tap test above (tapDifference > 1.0) passes on oscillator content
            // alone and would still pass if patchIn were removed from the tap path
            // entirely. Confirm patched audio actually reaches the pre-filter tap by
            // differencing accumulated pre-filter energy between a fed and a silent
            // run, the same pattern energyWith uses above.
            auto preFilterEnergyWith = [](bool feedSignal)
            {
                makesynth::SynthEngine engine;
                makesynth::Parameters q;
                q.mode = 0; q.drone = false; q.patchConnected = true; q.patchLevel = 1.0f;
                q.cutoff = 3000; q.resonance = 0; q.motion = 0; q.breath = 0;
                engine.setParameters(q); engine.prepare(48000);
                double total = 0;
                for (int i = 0; i < 24000; ++i)
                {
                    engine.process({static_cast<float>(feedSignal ? 0.4f * std::sin(i * 0.1) : 0.0f)});
                    total += engine.lastPreFilter() * engine.lastPreFilter();
                }
                return total;
            };
            const auto preFilterSilent = preFilterEnergyWith(false);
            require(preFilterEnergyWith(true) > preFilterSilent * 1.05,
                    "Patched audio did not reach the pre-filter tap");

            // Disconnected patch input must not open the gate.
            makesynth::Parameters q;
            q.mode = 0; q.drone = false; q.patchConnected = false; q.patchLevel = 1.0f;
            makesynth::SynthEngine quiet;
            quiet.setParameters(q); quiet.prepare(48000);
            double closed = 0;
            for (int i = 0; i < 24000; ++i) closed += std::abs(quiet.process({static_cast<float>(0.4f * std::sin(i * 0.1))}));
            require(closed < 1.0e-4, "Disconnected patch input must stay gated");

            // Each mode must colour the patched signal differently. Differencing against
            // the silent run removes the oscillator, which already differs per mode.
            const auto lowPass  = energyWith(1.0f, true, 0) - energyWith(1.0f, false, 0);
            const auto bandPass = energyWith(1.0f, true, 1) - energyWith(1.0f, false, 1);
            require(lowPass > 0 && bandPass > 0, "Patched audio vanished in a mode");
            require(std::abs(lowPass - bandPass) / std::max(lowPass, bandPass) > 0.1,
                    "Modes do not colour patched audio differently");
            std::cout << "patch gate checks passed\n";
        }

        {
            makesynth::SynthEngine e;
            makesynth::Parameters p;
            p.mode = 0; p.drone = true;
            e.setParameters(p); e.prepare(48000);
            for (int i = 0; i < 1000; ++i) e.process({0.5f});
            require(e.lastPreFilter() != 0 || e.lastPostFilter() != 0, "Taps never became non-zero");
            e.reset();
            require(e.lastPreFilter() == 0 && e.lastPostFilter() == 0, "reset() must clear the taps");
        }

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

        // Every oscillator shape: audible, finite, and bounded at each rate.
        for (double sr : {32000.0,48000.0,192000.0})
            for (int wave=0;wave<5;++wave)
            {
                p={}; p.mode=0; p.drone=true; p.wave=wave; p.width=0.15f;
                engine.setParameters(p); engine.prepare(sr);
                double energy=0,peak=0;
                for (int i=0;i<static_cast<int>(sr);++i)
                {
                    const auto sample=engine.process();
                    require(std::isfinite(sample) && std::abs(sample)<2,"Invalid wave output");
                    energy+=sample*sample; peak=std::max(peak,std::abs(static_cast<double>(sample)));
                }
                require(std::sqrt(energy/sr)>0.001,"An oscillator wave is inaudible");
                std::cout << "wave " << wave << " @ " << sr << " Hz RMS " << std::sqrt(energy/sr)
                          << " peak " << peak << '\n';
            }

        // Saw, square and pulse each cross zero upwards once per cycle.
        for (int wave : {0,1,2,3,4})
        {
            p={}; p.mode=0; p.drone=true; p.detune=0; p.motion=0; p.frequency=220;
            p.wave=wave; p.cutoff=18000; p.resonance=0;
            engine.setParameters(p); engine.prepare(48000);
            for (int i=0;i<24000;++i) engine.process();
            int upward=0; float last=engine.process();
            for (int i=0;i<48000;++i)
            {
                const auto x=engine.process();
                if (last<=0 && x>0) ++upward;
                last=x;
            }
            require(std::abs(upward-220)<=1,"Oscillator wave pitch is incorrect");
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
                        p.wave=(p.wave+1)%5;
                        p.width=p.width>0.5f?0.15f:0.85f;
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
        for (float width : {0.15f,0.35f,0.85f})
        {
            p={}; p.mode=0; p.drone=true; p.wave=4; p.width=width; p.motion=0;
            p.cutoff=18000; p.resonance=0; p.detune=0;
            engine.setParameters(p); engine.prepare(48000);
            for (int i=0;i<48000;++i) engine.process();
            double mean=0;
            for (int i=0;i<96000;++i) mean+=engine.process();
            require(std::abs(mean/96000)<0.002,"Pulse width must not leave a DC offset");
        }

        std::cout << "DSP checks passed\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
