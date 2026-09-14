/**
 * Make Synth - Web Audio DSP Engine
 * Exact mirror of SynthEngine.h (zero external dependencies)
 */

class MakeSynthFilter {
    constructor() {
        this.reset();
    }
    reset() {
        this.s1 = 0;
        this.s2 = 0;
        this.a1 = 1;
        this.a2 = 0;
        this.a3 = 0;
        this.k = 1;
    }
    tune(rate, cutoff, q) {
        const clampedCutoff = Math.min(Math.max(cutoff, 15.0), rate * 0.42);
        const g = Math.tan(Math.PI * clampedCutoff / rate);
        this.k = 1.0 / Math.max(0.001, q);
        this.a1 = 1.0 / (1.0 + g * (g + this.k));
        this.a2 = g * this.a1;
        this.a3 = g * this.a2;
    }
    process(input, bandpass = false) {
        const v3 = input - this.s2;
        const v1 = this.a1 * this.s1 + this.a2 * v3;
        const v2 = this.s2 + this.a2 * this.s1 + this.a3 * v3;
        this.s1 = 2 * v1 - this.s1;
        this.s2 = 2 * v2 - this.s2;
        return bandpass ? (this.k * v1) : v2;
    }
}

// Schroeder-Moorer stereo reverb (matching JUCE Reverb parameters)
class StereoReverb {
    constructor(sampleRate) {
        this.sampleRate = sampleRate;
        const combTunings = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617];
        const allpassTunings = [556, 441, 341, 225];
        const stereoSpread = 23;
        const scale = sampleRate / 44100;

        this.combL = combTunings.map(t => new CombFilter(Math.round(t * scale)));
        this.combR = combTunings.map(t => new CombFilter(Math.round((t + stereoSpread) * scale)));
        this.allpassL = allpassTunings.map(t => new AllpassFilter(Math.round(t * scale)));
        this.allpassR = allpassTunings.map(t => new AllpassFilter(Math.round((t + stereoSpread) * scale)));
        this.setParameters(0.5, 0.5, 0.33);
    }
    setParameters(roomSize, damping, wetLevel) {
        this.wet = wetLevel;
        this.dry = 1.0 - wetLevel * 0.5;
        const feedback = 0.7 + roomSize * 0.28;
        const damp = damping * 0.4;
        for (let i = 0; i < this.combL.length; ++i) {
            this.combL[i].feedback = feedback;
            this.combL[i].damp = damp;
            this.combR[i].feedback = feedback;
            this.combR[i].damp = damp;
        }
    }
    process(input) {
        if (this.wet <= 0.001) return [input, input];
        let outL = 0, outR = 0;
        for (let i = 0; i < this.combL.length; ++i) {
            outL += this.combL[i].process(input);
            outR += this.combR[i].process(input);
        }
        for (let i = 0; i < this.allpassL.length; ++i) {
            outL = this.allpassL[i].process(outL);
            outR = this.allpassR[i].process(outR);
        }
        return [
            input * this.dry + outL * this.wet * 0.25,
            input * this.dry + outR * this.wet * 0.25
        ];
    }
    reset() {
        this.combL.forEach(c => c.reset());
        this.combR.forEach(c => c.reset());
        this.allpassL.forEach(a => a.reset());
        this.allpassR.forEach(a => a.reset());
    }
}

class CombFilter {
    constructor(size) {
        this.buffer = new Float32Array(Math.max(1, size));
        this.idx = 0;
        this.filterStore = 0;
        this.feedback = 0.8;
        this.damp = 0.2;
    }
    process(input) {
        const output = this.buffer[this.idx];
        this.filterStore = (output * (1 - this.damp)) + (this.filterStore * this.damp);
        this.buffer[this.idx] = input + (this.filterStore * this.feedback);
        if (++this.idx >= this.buffer.length) this.idx = 0;
        return output;
    }
    reset() { this.buffer.fill(0); this.filterStore = 0; this.idx = 0; }
}

class AllpassFilter {
    constructor(size) {
        this.buffer = new Float32Array(Math.max(1, size));
        this.idx = 0;
        this.feedback = 0.5;
    }
    process(input) {
        const bufOut = this.buffer[this.idx];
        const output = -input + bufOut;
        this.buffer[this.idx] = input + (bufOut * this.feedback);
        if (++this.idx >= this.buffer.length) this.idx = 0;
        return output;
    }
    reset() { this.buffer.fill(0); this.idx = 0; }
}

class MakeSynthCore {
    constructor(sampleRate = 48000) {
        this.rate = Math.max(8000, sampleRate);
        this.reverb = new StereoReverb(this.rate);

        this.params = {
            mode: 0,
            drone: false,
            frequency: 110.0,
            cutoff: 1200.0,
            resonance: 0.15,
            rate: 0.05,
            motion: 0.15,
            detune: 6.0,
            fmRatio: 1.4142,
            fmDepth: 0.8,
            breath: 0.45,
            pink: true,
            space: 0.2,
            output: 0.7
        };

        this.current = Object.assign({}, this.params);
        this.weights = [1, 0, 0];
        this.filters = [new MakeSynthFilter(), new MakeSynthFilter(), new MakeSynthFilter()];

        this.phase1 = 0;
        this.phase2 = 0;
        this.carrierPhase = 0;
        this.modPhase = 0;
        this.lfoPhase = 0;

        this.envelope = 0;
        this.noteActive = false;
        this.noteFrequency = 110.0;
        this.velocity = 1.0;

        this.randomState = 0x8c31f249 >>> 0;
        this.pinkCounter = 0;
        this.pinkRows = new Float32Array(16);
        this.pinkSum = 0;
        this.pinkMix = 1.0;

        this.dcInput = 0;
        this.dcOutput = 0;
        this.coefficientCounter = 0;

        this.updateSmoothing();
    }

    updateSmoothing() {
        this.smoothing = 1.0 - Math.exp(-1.0 / (0.025 * this.rate));
        this.attack = 1.0 - Math.exp(-1.0 / (0.008 * this.rate));
        this.release = 1.0 - Math.exp(-1.0 / (0.15 * this.rate));
        this.dcCoefficient = Math.exp(-2.0 * Math.PI * 5.0 / this.rate);
    }

    setSampleRate(newRate) {
        this.rate = Math.max(8000, newRate);
        this.reverb = new StereoReverb(this.rate);
        this.updateSmoothing();
        this.reset();
    }

    reset() {
        this.phase1 = this.phase2 = this.carrierPhase = this.modPhase = this.lfoPhase = 0;
        this.envelope = 0;
        this.noteActive = false;
        this.noteFrequency = 110;
        this.velocity = 1;
        this.current = Object.assign({}, this.params);
        this.weights = [0, 0, 0];
        this.weights[Math.min(2, Math.max(0, this.params.mode))] = 1;
        this.randomState = 0x8c31f249 >>> 0;
        this.pinkCounter = 0;
        this.pinkRows.fill(0);
        this.pinkSum = 0;
        this.pinkMix = this.params.pink ? 1.0 : 0.0;
        this.dcInput = this.dcOutput = 0;
        this.coefficientCounter = 0;
        this.filters.forEach(f => f.reset());
        this.reverb.reset();
    }

    setNote(active, frequency, vel = 1.0) {
        this.noteActive = active;
        if (active) {
            this.noteFrequency = frequency;
            this.velocity = vel;
        }
    }

    advance(phase, hz) {
        phase += 2 * Math.PI * hz / this.rate;
        if (phase >= 2 * Math.PI) phase -= 2 * Math.PI;
        if (phase < 0) phase += 2 * Math.PI;
        return phase;
    }

    triangle(phase, hz) {
        let result = 0;
        for (let n = 1; n <= 13 && (n * hz < this.rate * 0.44); n += 2) {
            const sign = (((n - 1) / 2) % 2 === 0) ? 1.0 : -1.0;
            result += sign * Math.sin(n * phase) / (n * n);
        }
        return result * (8.0 / (Math.PI * Math.PI));
    }

    random() {
        this.randomState ^= (this.randomState << 13) >>> 0;
        this.randomState ^= (this.randomState >>> 17) >>> 0;
        this.randomState ^= (this.randomState << 5) >>> 0;
        return (this.randomState / 2147483648.0) - 1.0;
    }

    trailingZeroes(n) {
        if (n === 0) return 16;
        let count = 0;
        while ((n & 1) === 0) {
            count++;
            n >>>= 1;
        }
        return count;
    }

    processSample() {
        const smooth = (curr, target) => curr + this.smoothing * (target - curr);
        const wantedPitch = this.noteActive ? this.noteFrequency : (this.params.drone ? this.params.frequency : this.current.frequency);
        this.current.frequency = Math.min(16000, Math.max(10, smooth(this.current.frequency, wantedPitch)));
        this.current.cutoff = smooth(this.current.cutoff, this.params.cutoff);
        this.current.resonance = smooth(this.current.resonance, this.params.resonance);
        this.current.rate = smooth(this.current.rate, this.params.rate);
        this.current.motion = smooth(this.current.motion, this.params.motion);
        this.current.detune = smooth(this.current.detune, this.params.detune);
        this.current.fmRatio = smooth(this.current.fmRatio, this.params.fmRatio);
        this.current.fmDepth = smooth(this.current.fmDepth, this.params.fmDepth);
        this.current.breath = smooth(this.current.breath, this.params.breath);
        this.current.output = smooth(this.current.output, this.params.output);
        this.current.space = smooth(this.current.space, this.params.space);

        const gate = this.noteActive ? this.velocity : (this.params.drone ? 1.0 : 0.0);
        this.envelope += (gate > this.envelope ? this.attack : this.release) * (gate - this.envelope);
        if (gate === 0 && this.envelope < 1.0e-7) this.envelope = 0;

        for (let i = 0; i < 3; ++i) {
            const targetWeight = (i === this.params.mode) ? 1.0 : 0.0;
            this.weights[i] += this.smoothing * (targetWeight - this.weights[i]);
        }

        this.lfoPhase = this.advance(this.lfoPhase, this.current.rate);
        const lfo = Math.sin(this.lfoPhase);
        const base = Math.min(this.rate * 0.1, Math.max(10.0, this.current.frequency));
        const second = base * Math.pow(2.0, this.current.detune / 1200.0);

        this.phase1 = this.advance(this.phase1, base);
        this.phase2 = this.advance(this.phase2, second);
        const drone = 0.5 * (this.triangle(this.phase1, base) + this.triangle(this.phase2, second));

        const white = this.random();
        this.pinkCounter = (this.pinkCounter + 1) >>> 0;
        const row = this.trailingZeroes(this.pinkCounter) & 15;
        this.pinkSum -= this.pinkRows[row];
        this.pinkRows[row] = this.random();
        this.pinkSum += this.pinkRows[row];
        const pink = (this.pinkSum + white) * 0.13;
        this.pinkMix += this.smoothing * ((this.params.pink ? 1.0 : 0.0) - this.pinkMix);
        const noise = (white + this.pinkMix * (pink - white)) * 0.75;

        const modFrequency = Math.min(this.rate * 0.12, Math.max(1.0, base * this.current.fmRatio * Math.pow(2.0, this.current.motion * 0.04 * lfo)));
        const maxIndex = Math.max(0.0, (this.rate * 0.4 - base) / modFrequency - 1.0);
        const index = Math.min(this.current.fmDepth, maxIndex);
        this.modPhase = this.advance(this.modPhase, modFrequency);
        this.carrierPhase = this.advance(this.carrierPhase, base + modFrequency * index * Math.sin(this.modPhase));
        const metallic = Math.sin(this.carrierPhase);

        if ((this.coefficientCounter++ & 31) === 0) {
            const sweep = this.current.cutoff * Math.pow(2.0, this.current.motion * 3.0 * lfo);
            const q = 0.707 + Math.min(1.0, Math.max(0.0, this.current.resonance)) * 9.0;
            this.filters[0].tune(this.rate, sweep, q);
            this.filters[1].tune(this.rate, sweep, q);
            this.filters[2].tune(this.rate, this.current.cutoff, q);
            this.reverb.setParameters(0.4 + this.current.space * 0.5, 0.4, this.current.space);
        }

        const swell = 1.0 - this.current.breath * 0.5 + this.current.breath * 0.5 * lfo;
        const mixed = this.weights[0] * this.filters[0].process(drone)
                    + this.weights[1] * this.filters[1].process(noise, true) * swell
                    + this.weights[2] * this.filters[2].process(metallic);

        const shaped = Math.tanh(mixed * 0.85);
        this.dcOutput = shaped - this.dcInput + this.dcCoefficient * this.dcOutput;
        this.dcInput = shaped;

        const synthSample = this.dcOutput * this.envelope * this.current.output;
        return this.reverb.process(synthSample);
    }
}

if (typeof window !== 'undefined') {
    window.MakeSynthCore = MakeSynthCore;
}
if (typeof globalThis !== 'undefined') {
    globalThis.MakeSynthCore = MakeSynthCore;
}
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { MakeSynthCore };
}
