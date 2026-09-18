#!/usr/bin/env node
"use strict";

const fs = require("fs");
const path = require("path");
eval(fs.readFileSync(path.join(__dirname, "..", "web", "synth-engine.js"), "utf8"));

let failed = 0;
function check(name, ok, extra) {
    if (!ok) failed++;
    console.log(`${ok ? "PASS" : "FAIL"}  ${name}${extra ? " — " + extra : ""}`);
}

function peakOf(core, samples) {
    let peak = 0;
    let finite = true;
    for (let i = 0; i < samples; i++) {
        const [l, r] = core.processSample();
        if (!Number.isFinite(l) || !Number.isFinite(r)) finite = false;
        peak = Math.max(peak, Math.abs(l), Math.abs(r));
    }
    return { peak, finite };
}

{
    const c = new MakeSynthCore(48000);
    let energy = 0;
    for (let i = 0; i < 48000; i++) {
        const [l] = c.processSample();
        energy += l * l;
    }
    check("default is silent", energy === 0, `energy=${energy}`);
}

{
    const c = new MakeSynthCore(48000);
    let min = Infinity, max = -Infinity, sum = 0, pos = 0, neg = 0, below = 0, above = 0;
    const N = 200000;
    for (let i = 0; i < N; i++) {
        const x = c.random();
        if (x < min) min = x;
        if (x > max) max = x;
        sum += x;
        if (x < 0) neg++; else pos++;
        if (x < -1) below++;
        if (x >= 1) above++;
    }
    const mean = sum / N;
    check("xorshift stays in [-1, 1)", below === 0 && above === 0 && min >= -1 && max < 1,
        `min=${min} max=${max} below=${below} above=${above}`);
    check("xorshift is two-sided with near-zero mean", pos > N * 0.3 && neg > N * 0.3 && Math.abs(mean) < 0.05,
        `mean=${mean} pos=${pos} neg=${neg}`);
}

{
    const c = new MakeSynthCore(48000);
    c.params.mode = 2;
    c.params.drone = true;
    c.params.output = -18;
    c.params.space = 0.15;
    const { peak, finite } = peakOf(c, 96000);
    check("default metallic at -18 dB does not clip", finite && peak <= 0.85,
        `peak=${peak.toFixed(4)}`);
}

{
    const c = new MakeSynthCore(48000);
    c.params.mode = 2;
    c.params.drone = true;
    c.params.output = 0;
    c.params.resonance = 1;
    c.params.space = 0.65;
    c.params.fmDepth = 5;
    const { peak, finite } = peakOf(c, 96000);
    check("hot metallic at 0 dB stays within plugin limiter ceiling", finite && peak <= 0.981,
        `peak=${peak.toFixed(4)}`);
}

{
    const quiet = new MakeSynthCore(48000);
    const loud = new MakeSynthCore(48000);
    for (const c of [quiet, loud]) {
        c.params.mode = 0;
        c.params.drone = true;
        c.params.space = 0;
    }
    quiet.params.output = -18;
    loud.params.output = 0;
    let eQuiet = 0, eLoud = 0;
    for (let i = 0; i < 48000; i++) {
        eQuiet += quiet.processSample()[0] ** 2;
        eLoud += loud.processSample()[0] ** 2;
    }
    const ratio = Math.sqrt(eLoud / 48000) / Math.sqrt(eQuiet / 48000);
    check("0 dB is about 18 dB hotter than the default", ratio > 6 && ratio < 10,
        `amplitude ratio=${ratio.toFixed(3)} (expect ~7.94)`);
}

{
    function bandRatio(usePink) {
        const c = new MakeSynthCore(48000);
        c.params.mode = 1;
        c.params.drone = true;
        c.params.pink = usePink;
        c.params.cutoff = 8000;
        c.params.resonance = 0;
        c.params.breath = 0;
        c.params.motion = 0;
        c.params.space = 0;
        c.params.output = 0;
        const N = 8192;
        const buf = new Float64Array(N);
        for (let i = 0; i < 4096; i++) c.processSample();
        for (let i = 0; i < N; i++) buf[i] = c.processSample()[0];
        function mag(k) {
            let re = 0, im = 0;
            for (let n = 0; n < N; n++) {
                const a = 2 * Math.PI * k * n / N;
                re += buf[n] * Math.cos(a);
                im -= buf[n] * Math.sin(a);
            }
            return re * re + im * im;
        }
        let low = 0, high = 0;
        for (let k = 2; k < 40; k++) low += mag(k);
        for (let k = 800; k < 1200; k++) high += mag(k);
        return low / (high + 1e-12);
    }
    const pink = bandRatio(true);
    const white = bandRatio(false);
    check("boolean true selects pink (more low-band energy than white)", pink > white,
        `pink=${pink.toFixed(4)} white=${white.toFixed(4)}`);
}

{
    const names = ["sine", "triangle", "saw", "square", "pulse"];
    for (let wave = 0; wave < 5; wave++) {
        const c = new MakeSynthCore(48000);
        c.params.mode = 0;
        c.params.drone = true;
        c.params.wave = wave;
        c.params.width = 0.15;
        c.params.output = 0;
        c.params.resonance = 1;
        c.params.space = 0.65;
        const { peak, finite } = peakOf(c, 96000);
        check(`${names[wave]} at 0 dB stays within the plugin limiter ceiling`,
            finite && peak > 0.001 && peak <= 0.981, `peak=${peak.toFixed(4)}`);
    }
}

{
    // The DC blocker plus the in-oscillator offset removal must leave pulse centred.
    for (const width of [0.15, 0.35, 0.85]) {
        const c = new MakeSynthCore(48000);
        c.params.mode = 0;
        c.params.drone = true;
        c.params.wave = 4;
        c.params.width = width;
        c.params.motion = 0;
        c.params.detune = 0;
        c.params.cutoff = 18000;
        c.params.resonance = 0;
        c.params.space = 0;
        c.params.output = 0;
        for (let i = 0; i < 48000; i++) c.processSample();
        let mean = 0;
        for (let i = 0; i < 96000; i++) mean += c.processSample()[0];
        mean /= 96000;
        check(`pulse width ${width} leaves no DC offset`, Math.abs(mean) < 0.002,
            `mean=${mean.toFixed(6)}`);
    }
}

{
    // Bright shapes must actually carry more high-harmonic energy than the sine.
    function brightness(wave) {
        const c = new MakeSynthCore(48000);
        c.params.mode = 0;
        c.params.drone = true;
        c.params.wave = wave;
        c.params.frequency = 110;
        c.params.detune = 0;
        c.params.motion = 0;
        c.params.cutoff = 18000;
        c.params.resonance = 0;
        c.params.space = 0;
        c.params.output = 0;
        for (let i = 0; i < 8192; i++) c.processSample();
        let fundamental = 0, upper = 0;
        const N = 8192;
        const buf = new Float64Array(N);
        for (let i = 0; i < N; i++) buf[i] = c.processSample()[0];
        function mag(k) {
            let re = 0, im = 0;
            for (let n = 0; n < N; n++) {
                const a = 2 * Math.PI * k * n / N;
                re += buf[n] * Math.cos(a);
                im -= buf[n] * Math.sin(a);
            }
            return re * re + im * im;
        }
        // 110 Hz at 48 kHz over 8192 points puts the fundamental near bin 19.
        for (let k = 15; k <= 23; k++) fundamental += mag(k);
        for (let k = 40; k < 400; k++) upper += mag(k);
        return upper / (fundamental + 1e-12);
    }
    const sine = brightness(0);
    const saw = brightness(2);
    const square = brightness(3);
    check("saw is richer in harmonics than sine", saw > sine * 10,
        `saw=${saw.toFixed(5)} sine=${sine.toFixed(8)}`);
    check("square is richer in harmonics than sine", square > sine * 10,
        `square=${square.toFixed(5)} sine=${sine.toFixed(8)}`);
}

if (failed) {
    console.error(`\n${failed} web DSP check(s) failed`);
    process.exit(1);
}
console.log("\nWeb DSP checks passed");
