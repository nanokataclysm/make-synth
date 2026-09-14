/**
 * Make Synth Web Controller & GUI Engine
 */

let audioCtx = null;
let synthCore = null;
let scriptNode = null;
let analyser = null;
let isAudioRunning = false;

// Parameter definitions
const PARAMS = [
    { id: "frequency", name: "Pitch", min: 20, max: 880, step: 0.1, def: 110.0, unit: " Hz", skew: 0.3, modes: [0, 1, 2] },
    { id: "cutoff", name: "Filter", min: 40, max: 12000, step: 1, def: 1200.0, unit: " Hz", skew: 0.3, modes: [0, 1, 2] },
    { id: "resonance", name: "Resonance", min: 0.0, max: 1.0, step: 0.01, def: 0.15, unit: "", skew: 1, modes: [0, 1, 2] },
    { id: "rate", name: "Rate", min: 0.005, max: 4.0, step: 0.001, def: 0.05, unit: " Hz", skew: 0.3, modes: [0, 1, 2] },
    
    // Mode 0: Detuned
    { id: "detune", name: "Detune", min: 0.0, max: 35.0, step: 0.1, def: 6.0, unit: " ct", skew: 1, modes: [0] },
    { id: "motion", name: "Motion", min: 0.0, max: 1.0, step: 0.01, def: 0.15, unit: "", skew: 1, modes: [0, 1] },
    
    // Mode 1: Breathing
    { id: "breath", name: "Breathing", min: 0.0, max: 1.0, step: 0.01, def: 0.45, unit: "", skew: 1, modes: [1] },
    { id: "pink", name: "Noise Color", min: 0, max: 1, step: 1, def: 1, unit: "", isToggle: true, modes: [1] },
    
    // Mode 2: Metallic
    { id: "fmRatio", name: "FM Ratio", min: 0.25, max: 8.0, step: 0.001, def: 1.414, unit: "", skew: 0.5, modes: [2] },
    { id: "fmDepth", name: "FM Depth", min: 0.0, max: 5.0, step: 0.01, def: 0.8, unit: "", skew: 0.5, modes: [2] },
    
    // Global master
    { id: "space", name: "Space", min: 0.0, max: 1.0, step: 0.01, def: 0.2, unit: "", skew: 1, modes: [0, 1, 2] },
    { id: "output", name: "Output", min: 0.0, max: 1.5, step: 0.01, def: 0.7, unit: "", skew: 1, modes: [0, 1, 2] }
];

const knobInstances = {};

function initAudio() {
    if (audioCtx) return;
    const AudioContextClass = window.AudioContext || window.webkitAudioContext;
    audioCtx = new AudioContextClass({ latencyHint: "interactive" });
    synthCore = new MakeSynthCore(audioCtx.sampleRate);

    // Apply current param values to core
    PARAMS.forEach(p => {
        if (knobInstances[p.id]) {
            synthCore.params[p.id] = knobInstances[p.id].value;
        }
    });

    const bufferSize = 512;
    scriptNode = audioCtx.createScriptProcessor(bufferSize, 0, 2);
    analyser = audioCtx.createAnalyser();
    analyser.fftSize = 1024;

    scriptNode.onaudioprocess = (e) => {
        const outL = e.outputBuffer.getChannelData(0);
        const outR = e.outputBuffer.getChannelData(1);
        for (let i = 0; i < bufferSize; ++i) {
            const [l, r] = synthCore.processSample();
            outL[i] = l;
            outR[i] = r;
        }
    };

    scriptNode.connect(analyser);
    analyser.connect(audioCtx.destination);
    isAudioRunning = true;
    startVisualizer();
}

function ensureAudio() {
    if (!audioCtx) initAudio();
    if (audioCtx && audioCtx.state === "suspended") {
        audioCtx.resume();
    }
}

// Knob Component Class
class RotaryKnob {
    constructor(config, container) {
        this.config = config;
        this.container = container;
        this.value = config.def;
        this.norm = this.valueToNorm(this.value);

        this.element = document.createElement("div");
        this.element.className = "knob-widget";
        this.element.dataset.paramId = config.id;

        this.element.innerHTML = `
            <div class="knob-label">${config.name}</div>
            <div class="knob-dial">
                <svg class="knob-svg" viewBox="0 0 100 100">
                    <circle class="knob-track" cx="50" cy="50" r="38" stroke-dasharray="190" stroke-dashoffset="0" stroke-linecap="round" />
                    <circle class="knob-value-arc" cx="50" cy="50" r="38" stroke-dasharray="190" stroke-dashoffset="190" stroke-linecap="round" />
                    <circle class="knob-body" cx="50" cy="50" r="30" />
                    <line class="knob-pointer" x1="50" y1="50" x2="50" y2="24" />
                </svg>
            </div>
            <input type="text" class="knob-badge" value="" />
        `;

        this.dial = this.element.querySelector(".knob-dial");
        this.valueArc = this.element.querySelector(".knob-value-arc");
        this.pointer = this.element.querySelector(".knob-pointer");
        this.badge = this.element.querySelector(".knob-badge");

        this.attachEvents();
        this.updateView();
        container.appendChild(this.element);
    }

    valueToNorm(v) {
        const min = this.config.min;
        const max = this.config.max;
        const skew = this.config.skew || 1;
        const clamped = Math.min(max, Math.max(min, v));
        const linear = (clamped - min) / (max - min);
        return Math.pow(linear, 1 / skew);
    }

    normToValue(n) {
        const min = this.config.min;
        const max = this.config.max;
        const skew = this.config.skew || 1;
        const linear = Math.pow(Math.min(1, Math.max(0, n)), skew);
        let val = min + linear * (max - min);
        if (this.config.step >= 1) {
            val = Math.round(val);
        } else {
            const places = (this.config.step.toString().split(".")[1] || "").length;
            val = parseFloat(val.toFixed(places));
        }
        return val;
    }

    setValue(val, notifyCore = true) {
        this.value = Math.min(this.config.max, Math.max(this.config.min, val));
        this.norm = this.valueToNorm(this.value);
        this.updateView();
        if (notifyCore && synthCore) {
            synthCore.params[this.config.id] = this.value;
        }
    }

    setNormalized(norm, notifyCore = true) {
        this.norm = Math.min(1, Math.max(0, norm));
        this.value = this.normToValue(this.norm);
        this.updateView();
        if (notifyCore && synthCore) {
            synthCore.params[this.config.id] = this.value;
        }
    }

    updateView() {
        const startAngle = 135;
        const sweepAngle = 270;
        const angle = startAngle + this.norm * sweepAngle;

        // Rotate dial SVG pointer
        this.pointer.setAttribute("transform", `rotate(${angle - 180} 50 50)`);

        // Update value arc
        const circumference = 2 * Math.PI * 38 * (sweepAngle / 360);
        const dashOffset = circumference * (1 - this.norm);
        this.valueArc.style.strokeDasharray = `${circumference}`;
        this.valueArc.style.strokeDashoffset = `${dashOffset}`;
        this.valueArc.setAttribute("transform", `rotate(${startAngle} 50 50)`);

        // Format badge
        if (this.config.isToggle) {
            this.badge.value = this.value >= 0.5 ? "White" : "Pink";
        } else {
            const places = this.config.step < 0.01 ? 3 : (this.config.step < 1 ? 2 : 1);
            this.badge.value = `${this.value.toFixed(places)}${this.config.unit}`;
        }
    }

    attachEvents() {
        let startY = 0;
        let startNorm = 0;

        const onPointerMove = (e) => {
            const pageY = e.touches ? e.touches[0].pageY : e.pageY;
            const deltaY = startY - pageY;
            const sensitivity = 0.005;
            this.setNormalized(startNorm + deltaY * sensitivity);
        };

        const onPointerUp = () => {
            this.element.classList.remove("dragging");
            window.removeEventListener("mousemove", onPointerMove);
            window.removeEventListener("mouseup", onPointerUp);
            window.removeEventListener("touchmove", onPointerMove);
            window.removeEventListener("touchend", onPointerUp);
        };

        const onPointerDown = (e) => {
            ensureAudio();
            e.preventDefault();
            this.element.classList.add("dragging");
            startY = e.touches ? e.touches[0].pageY : e.pageY;
            startNorm = this.norm;

            if (this.config.isToggle) {
                this.setValue(this.value >= 0.5 ? 0 : 1);
                return;
            }

            window.addEventListener("mousemove", onPointerMove);
            window.addEventListener("mouseup", onPointerUp);
            window.addEventListener("touchmove", onPointerMove, { passive: false });
            window.addEventListener("touchend", onPointerUp);
        };

        this.dial.addEventListener("mousedown", onPointerDown);
        this.dial.addEventListener("touchstart", onPointerDown, { passive: false });

        // Wheel
        this.dial.addEventListener("wheel", (e) => {
            ensureAudio();
            e.preventDefault();
            const delta = -Math.sign(e.deltaY) * 0.04;
            this.setNormalized(this.norm + delta);
        }, { passive: false });

        // Direct badge edit
        this.badge.addEventListener("change", () => {
            const num = parseFloat(this.badge.value);
            if (!isNaN(num)) {
                this.setValue(num);
            } else {
                this.updateView();
            }
        });
    }
}

// Mode change handler
function setMode(mode) {
    ensureAudio();
    if (synthCore) synthCore.params.mode = mode;
    document.querySelectorAll(".mode-btn").forEach((btn, idx) => {
        btn.classList.toggle("active", idx === mode);
    });

    PARAMS.forEach(p => {
        const inst = knobInstances[p.id];
        if (inst) {
            inst.element.style.display = p.modes.includes(mode) ? "flex" : "none";
        }
    });
}

// Visualizer
function startVisualizer() {
    const canvas = document.getElementById("scopeCanvas");
    const ctx = canvas.getContext("2d");
    const buffer = new Float32Array(analyser.frequencyBinCount);

    function draw() {
        requestAnimationFrame(draw);
        const w = canvas.width = canvas.clientWidth * window.devicePixelRatio;
        const h = canvas.height = canvas.clientHeight * window.devicePixelRatio;

        analyser.getFloatTimeDomainData(buffer);

        ctx.fillStyle = "#0d0e11";
        ctx.fillRect(0, 0, w, h);

        // Grid lines
        ctx.strokeStyle = "#1b1d22";
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(0, h / 2);
        ctx.lineTo(w, h / 2);
        ctx.stroke();

        ctx.strokeStyle = "#5da38b";
        ctx.lineWidth = 2 * window.devicePixelRatio;
        ctx.shadowBlur = 8;
        ctx.shadowColor = "rgba(93, 163, 139, 0.4)";
        ctx.beginPath();

        const sliceWidth = w / buffer.length;
        let x = 0;
        for (let i = 0; i < buffer.length; i++) {
            const v = buffer[i];
            const y = (0.5 - v * 0.45) * h;
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
            x += sliceWidth;
        }
        ctx.stroke();
        ctx.shadowBlur = 0;
    }
    draw();
}

// Web MIDI Controller Support
function setupWebMidi() {
    const statusDot = document.getElementById("midiDot");
    const statusText = document.getElementById("midiText");

    if (!navigator.requestMIDIAccess) {
        statusText.textContent = "Web MIDI not supported";
        return;
    }

    navigator.requestMIDIAccess().then(access => {
        function updateMidi() {
            const inputs = Array.from(access.inputs.values());
            if (inputs.length > 0) {
                statusDot.classList.add("connected");
                statusText.textContent = `MIDI: ${inputs[0].name}`;
            } else {
                statusDot.classList.remove("connected");
                statusText.textContent = "MIDI: No device";
            }
            inputs.forEach(input => {
                input.onmidimessage = handleMidiMessage;
            });
        }
        access.onstatechange = updateMidi;
        updateMidi();
    }).catch(err => {
        statusText.textContent = "MIDI access denied";
    });
}

function handleMidiMessage(e) {
    ensureAudio();
    const [status, data1, data2] = e.data;
    const type = status & 0xf0;
    const currentMode = synthCore ? synthCore.params.mode : 0;

    if (type === 0x90 && data2 > 0) { // Note On
        const freq = 440 * Math.pow(2, (data1 - 69) / 12);
        synthCore.setNote(true, freq, data2 / 127);
        highlightKey(data1, true);
    } else if (type === 0x80 || (type === 0x90 && data2 === 0)) { // Note Off
        synthCore.setNote(false, 0);
        highlightKey(data1, false);
    } else if (type === 0xb0) { // Control Change
        const cc = data1;
        const norm = data2 / 127;
        switch (cc) {
            case 1: // Mod Wheel
                if (currentMode === 0 && knobInstances["motion"]) knobInstances["motion"].setNormalized(norm);
                else if (currentMode === 1 && knobInstances["breath"]) knobInstances["breath"].setNormalized(norm);
                else if (currentMode === 2 && knobInstances["fmDepth"]) knobInstances["fmDepth"].setNormalized(norm);
                break;
            case 74: // Cutoff
                if (knobInstances["cutoff"]) knobInstances["cutoff"].setNormalized(norm);
                break;
            case 71: // Resonance
                if (knobInstances["resonance"]) knobInstances["resonance"].setNormalized(norm);
                break;
            case 76: case 14: // Rate
                if (knobInstances["rate"]) knobInstances["rate"].setNormalized(norm);
                break;
            case 7: // Output
                if (knobInstances["output"]) knobInstances["output"].setNormalized(norm);
                break;
            case 91: // Space
                if (knobInstances["space"]) knobInstances["space"].setNormalized(norm);
                break;
            case 77: case 12: // Detune
                if (knobInstances["detune"]) knobInstances["detune"].setNormalized(norm);
                break;
            case 78: case 13: // FM Ratio
                if (knobInstances["fmRatio"]) knobInstances["fmRatio"].setNormalized(norm);
                break;
            case 75: case 15: // FM Depth
                if (knobInstances["fmDepth"]) knobInstances["fmDepth"].setNormalized(norm);
                break;
            case 73: // Breath
                if (knobInstances["breath"]) knobInstances["breath"].setNormalized(norm);
                break;
            case 80: case 16: // Pitch
                if (knobInstances["frequency"]) knobInstances["frequency"].setNormalized(norm);
                break;
            case 65: case 81: // Drone toggle
                toggleDrone(data2 >= 64);
                break;
            case 82: // Mode switch
                setMode(data2 < 43 ? 0 : (data2 < 86 ? 1 : 2));
                break;
        }
    }
}

function toggleDrone(forceState) {
    ensureAudio();
    const btn = document.getElementById("droneBtn");
    const next = (forceState !== undefined) ? forceState : !synthCore.params.drone;
    synthCore.params.drone = next;
    btn.classList.toggle("active", next);
}

function stopAll() {
    ensureAudio();
    if (synthCore) synthCore.reset();
    toggleDrone(false);
}

// Virtual Keyboard
function setupKeyboard() {
    const keyboard = document.getElementById("keyboardContainer");
    const notes = [
        { n: 48, name: "C3", black: false }, { n: 49, name: "C#3", black: true },
        { n: 50, name: "D3", black: false }, { n: 51, name: "D#3", black: true },
        { n: 52, name: "E3", black: false },
        { n: 53, name: "F3", black: false }, { n: 54, name: "F#3", black: true },
        { n: 55, name: "G3", black: false }, { n: 56, name: "G#3", black: true },
        { n: 57, name: "A3", black: false }, { n: 58, name: "A#3", black: true },
        { n: 59, name: "B3", black: false },
        { n: 60, name: "C4", black: false }, { n: 61, name: "C#4", black: true },
        { n: 62, name: "D4", black: false }, { n: 63, name: "D#4", black: true },
        { n: 64, name: "E4", black: false },
        { n: 65, name: "F4", black: false }, { n: 66, name: "F#4", black: true },
        { n: 67, name: "G4", black: false }, { n: 68, name: "G#4", black: true },
        { n: 69, name: "A4", black: false }, { n: 70, name: "A#4", black: true },
        { n: 71, name: "B4", black: false },
        { n: 72, name: "C5", black: false }
    ];

    let whiteCount = notes.filter(n => !n.black).length;
    let whiteIdx = 0;

    notes.forEach(note => {
        const key = document.createElement("div");
        key.dataset.note = note.n;
        if (note.black) {
            key.className = "black-key";
            const leftPercent = ((whiteIdx - 0.35) / whiteCount) * 100;
            key.style.left = `${leftPercent}%`;
        } else {
            key.className = "white-key";
            whiteIdx++;
        }

        const press = (e) => {
            e.preventDefault();
            ensureAudio();
            const freq = 440 * Math.pow(2, (note.n - 69) / 12);
            synthCore.setNote(true, freq, 0.85);
            key.classList.add("active");
        };

        const release = (e) => {
            e.preventDefault();
            synthCore.setNote(false, 0);
            key.classList.remove("active");
        };

        key.addEventListener("mousedown", press);
        key.addEventListener("mouseup", release);
        key.addEventListener("mouseleave", release);
        key.addEventListener("touchstart", press, { passive: false });
        key.addEventListener("touchend", release);

        keyboard.appendChild(key);
    });
}

function highlightKey(noteNum, active) {
    const key = document.querySelector(`[data-note="${noteNum}"]`);
    if (key) key.classList.toggle("active", active);
}

// Initial bootstrap
window.addEventListener("DOMContentLoaded", () => {
    const grid = document.getElementById("controlsGrid");
    PARAMS.forEach(p => {
        knobInstances[p.id] = new RotaryKnob(p, grid);
    });

    document.querySelectorAll(".mode-btn").forEach((btn, idx) => {
        btn.addEventListener("click", () => setMode(idx));
    });

    document.getElementById("droneBtn").addEventListener("click", () => toggleDrone());
    document.getElementById("stopBtn").addEventListener("click", () => stopAll());

    setupKeyboard();
    setupWebMidi();
    setMode(0);

    // Any first click awakens audio context
    window.addEventListener("click", () => ensureAudio(), { once: true });
    window.addEventListener("touchstart", () => ensureAudio(), { once: true });
});
