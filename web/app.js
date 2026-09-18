/**
 * Make Synth Web Controller & GUI Engine
 */

let audioCtx = null;
let synthCore = null;
let scriptNode = null;
let analyser = null;
let isAudioRunning = false;

const PARAMS = [
    { id: "frequency", name: "Pitch", min: 20, max: 1000, step: 0.1, def: 110.0, unit: " Hz", centre: 110, modes: [0, 1, 2] },
    { id: "cutoff", name: "Filter", min: 30, max: 18000, step: 1, def: 1200.0, unit: " Hz", centre: 1000, modes: [0, 1, 2] },
    { id: "resonance", name: "Resonance", min: 0.0, max: 1.0, step: 0.01, def: 0.15, unit: "", modes: [0, 1, 2] },
    { id: "rate", name: "Rate", min: 0.005, max: 5.0, step: 0.001, def: 0.05, unit: " Hz", centre: 0.1, modes: [0, 1, 2] },
    { id: "detune", name: "Detune", min: 0.0, max: 35.0, step: 0.1, def: 6.0, unit: " cents", modes: [0] },
    { id: "motion", name: "Motion", min: 0.0, max: 1.0, step: 0.01, def: 0.15, unit: "", modes: [0, 1, 2] },
    { id: "breath", name: "Breathing", min: 0.0, max: 1.0, step: 0.01, def: 0.45, unit: "", modes: [1] },
    { id: "pink", name: "Noise Color", min: 0, max: 1, step: 1, def: 0, unit: "", isToggle: true, modes: [1] },
    { id: "wave", name: "Wave", min: 0, max: 4, step: 1, def: 1, unit: "",
      labels: ["Sine", "Triangle", "Saw", "Square", "Pulse"], modes: [0] },
    { id: "width", name: "Pulse Width", min: 0.15, max: 0.85, step: 0.01, def: 0.35, unit: "", modes: [0] },
    { id: "fmRatio", name: "FM Ratio", min: 0.125, max: 8.0, step: 0.001, def: 1.4142, unit: "", centre: 1.5, modes: [2] },
    { id: "fmDepth", name: "FM Depth", min: 0.0, max: 5.0, step: 0.01, def: 0.8, unit: "", modes: [2] },
    { id: "space", name: "Space", min: 0.0, max: 0.65, step: 0.01, def: 0.15, unit: "", modes: [0, 1, 2] },
    { id: "output", name: "Output", min: -48, max: 0, step: 0.1, def: -18, unit: " dB", modes: [0, 1, 2] }
];

const knobInstances = {};

function juceSkew(min, max, centre) {
    return Math.log(0.5) / Math.log((centre - min) / (max - min));
}

function applyParam(id, value) {
    if (!synthCore) return;
    if (id === "pink") synthCore.params.pink = value < 0.5;
    else if (id === "wave") {
        synthCore.params.wave = Math.round(value);
        refreshWidthState();
    }
    else synthCore.params[id] = value;
}

// Only the Pulse shape reads the duty cycle; Square is a fixed half cycle.
function refreshWidthState() {
    const knob = knobInstances.width;
    if (!knob) return;
    // Before the audio context exists, follow the wave knob's own position.
    const wave = synthCore ? synthCore.params.wave
                           : (knobInstances.wave ? knobInstances.wave.value : 1);
    knob.element.classList.toggle("disabled", Math.round(wave) !== 4);
}

function initAudio() {
    if (audioCtx) return;
    const AudioContextClass = window.AudioContext || window.webkitAudioContext;
    audioCtx = new AudioContextClass({ latencyHint: "interactive" });
    synthCore = new MakeSynthCore(audioCtx.sampleRate);

    PARAMS.forEach(p => {
        if (knobInstances[p.id]) applyParam(p.id, knobInstances[p.id].value);
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

class RotaryKnob {
    constructor(config, container) {
        this.config = config;
        this.container = container;
        this.skew = (config.centre > config.min && config.centre < config.max)
            ? juceSkew(config.min, config.max, config.centre)
            : 1;
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
        const clamped = Math.min(max, Math.max(min, v));
        const linear = (clamped - min) / (max - min);
        if (this.skew === 1) return linear;
        return Math.pow(linear, this.skew);
    }

    normToValue(n) {
        const min = this.config.min;
        const max = this.config.max;
        let proportion = Math.min(1, Math.max(0, n));
        if (this.skew !== 1 && proportion > 0) {
            proportion = Math.exp(Math.log(proportion) / this.skew);
        }
        let val = min + proportion * (max - min);
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
        if (notifyCore) applyParam(this.config.id, this.value);
    }

    setNormalized(norm, notifyCore = true) {
        this.norm = Math.min(1, Math.max(0, norm));
        this.value = this.normToValue(this.norm);
        this.updateView();
        if (notifyCore) applyParam(this.config.id, this.value);
    }

    updateView() {
        const startAngle = 135;
        const sweepAngle = 270;
        const angle = startAngle + this.norm * sweepAngle;

        this.pointer.setAttribute("transform", `rotate(${angle - 180} 50 50)`);

        const circumference = 2 * Math.PI * 38 * (sweepAngle / 360);
        const dashOffset = circumference * (1 - this.norm);
        this.valueArc.style.strokeDasharray = `${circumference}`;
        this.valueArc.style.strokeDashoffset = `${dashOffset}`;
        this.valueArc.setAttribute("transform", `rotate(${startAngle} 50 50)`);

        if (this.config.labels) {
            this.badge.value = this.config.labels[Math.min(this.config.labels.length - 1,
                Math.max(0, Math.round(this.value)))];
        } else if (this.config.isToggle) {
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

        this.dial.addEventListener("wheel", (e) => {
            ensureAudio();
            e.preventDefault();
            const delta = -Math.sign(e.deltaY) * 0.04;
            this.setNormalized(this.norm + delta);
        }, { passive: false });

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

function setMode(mode) {
    if (synthCore) synthCore.params.mode = mode;
    document.querySelectorAll(".mode-btn").forEach((btn, idx) => {
        btn.classList.toggle("active", idx === mode);
        btn.setAttribute("aria-pressed", idx === mode ? "true" : "false");
    });

    PARAMS.forEach(p => {
        const inst = knobInstances[p.id];
        if (inst) {
            inst.element.style.display = p.modes.includes(mode) ? "flex" : "none";
            inst.element.classList.toggle("disabled", p.id === "frequency" && mode === 1);
        }
    });
    refreshWidthState();
}

function startVisualizer() {
    const canvas = document.getElementById("scopeCanvas");
    const ctx = canvas.getContext("2d");
    const buffer = new Float32Array(analyser.fftSize);

    function draw() {
        requestAnimationFrame(draw);
        const w = canvas.width = canvas.clientWidth * window.devicePixelRatio;
        const h = canvas.height = canvas.clientHeight * window.devicePixelRatio;

        analyser.getFloatTimeDomainData(buffer);

        ctx.fillStyle = "#0d0e11";
        ctx.fillRect(0, 0, w, h);

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
    }).catch(() => {
        statusText.textContent = "MIDI access denied";
    });
}

const midiNotes = Array.from({ length: 16 }, () =>
    Array.from({ length: 128 }, () => ({ down: false, held: false, velocity: 0, order: 0 })));
const sustain = Array(16).fill(false);
const bend = Array(16).fill(1);
let noteOrder = 0;

function midiNoteHz(note, channel) {
    return 440 * Math.pow(2, (note - 69) / 12) * bend[channel];
}

function selectHeldNote() {
    if (!synthCore) return;
    let order = 0, chosen = -1, channel = 0, velocity = 0;
    for (let c = 0; c < 16; c++) {
        for (let n = 0; n < 128; n++) {
            const x = midiNotes[c][n];
            if (x.held && x.order > order) {
                order = x.order;
                chosen = n;
                channel = c;
                velocity = x.velocity;
            }
        }
    }
    if (chosen < 0) synthCore.setNote(false, 110);
    else synthCore.setNote(true, midiNoteHz(chosen, channel), velocity);
}

function noteOn(channel, note, velocity) {
    const n = midiNotes[channel][note];
    n.down = n.held = true;
    n.velocity = velocity;
    n.order = ++noteOrder;
    selectHeldNote();
    highlightKey(note, true);
}

function noteOff(channel, note) {
    const n = midiNotes[channel][note];
    n.down = false;
    if (!sustain[channel]) n.held = false;
    selectHeldNote();
    highlightKey(note, false);
}

function clearNotes() {
    for (let c = 0; c < 16; c++) {
        for (let n = 0; n < 128; n++) midiNotes[c][n] = { down: false, held: false, velocity: 0, order: 0 };
        sustain[c] = false;
        bend[c] = 1;
    }
    noteOrder = 0;
    if (synthCore) synthCore.setNote(false, 110);
    document.querySelectorAll(".white-key.active, .black-key.active").forEach(key => key.classList.remove("active"));
}

function handleMidiMessage(e) {
    ensureAudio();
    if (!synthCore || e.data.length < 2) return;
    const status = e.data[0];
    const data1 = e.data[1];
    const data2 = e.data.length > 2 ? e.data[2] : 0;
    const type = status & 0xf0;
    const channel = status & 0x0f;
    const currentMode = synthCore.params.mode;

    if (type === 0x90 && data2 > 0) {
        noteOn(channel, data1, data2 / 127);
    } else if (type === 0x80 || (type === 0x90 && data2 === 0)) {
        noteOff(channel, data1);
    } else if (type === 0xe0) {
        const value = data1 | (data2 << 7);
        bend[channel] = Math.pow(2, (value - 8192) / 8192 * (2 / 12));
        selectHeldNote();
    } else if (type === 0xb0) {
        const cc = data1;
        const norm = data2 / 127;
        switch (cc) {
            case 1:
            case 11:
                if (currentMode === 0 && knobInstances.motion) knobInstances.motion.setNormalized(norm);
                else if (currentMode === 1 && knobInstances.breath) knobInstances.breath.setNormalized(norm);
                else if (currentMode === 2 && knobInstances.fmDepth) knobInstances.fmDepth.setNormalized(norm);
                break;
            case 74:
                if (knobInstances.cutoff) knobInstances.cutoff.setNormalized(norm);
                break;
            case 71:
                if (knobInstances.resonance) knobInstances.resonance.setNormalized(norm);
                break;
            case 76: case 14:
                if (knobInstances.rate) knobInstances.rate.setNormalized(norm);
                break;
            case 7:
                if (knobInstances.output) knobInstances.output.setNormalized(norm);
                break;
            case 91:
                if (knobInstances.space) knobInstances.space.setNormalized(norm);
                break;
            case 77: case 12:
                if (knobInstances.detune) knobInstances.detune.setNormalized(norm);
                break;
            case 78: case 13:
                if (knobInstances.fmRatio) knobInstances.fmRatio.setNormalized(norm);
                break;
            case 75: case 15:
                if (knobInstances.fmDepth) knobInstances.fmDepth.setNormalized(norm);
                break;
            case 73:
                if (knobInstances.breath) knobInstances.breath.setNormalized(norm);
                break;
            case 80: case 16:
                if (knobInstances.frequency) knobInstances.frequency.setNormalized(norm);
                break;
            case 65: case 81:
                toggleDrone(data2 >= 64);
                break;
            case 82:
                setMode(data2 < 43 ? 0 : (data2 < 86 ? 1 : 2));
                break;
            case 83:
                if (knobInstances.pink) knobInstances.pink.setValue(data2 >= 64 ? 1 : 0);
                break;
            case 70:
                if (knobInstances.wave) knobInstances.wave.setNormalized(norm);
                break;
            case 79:
                if (knobInstances.width) knobInstances.width.setNormalized(norm);
                break;
            case 64:
                sustain[channel] = data2 >= 64;
                if (!sustain[channel]) {
                    for (let n = 0; n < 128; n++) {
                        if (!midiNotes[channel][n].down) midiNotes[channel][n].held = false;
                    }
                    selectHeldNote();
                }
                break;
            case 120:
                for (let n = 0; n < 128; n++) midiNotes[channel][n] = { down: false, held: false, velocity: 0, order: 0 };
                sustain[channel] = false;
                selectHeldNote();
                break;
            case 123:
                for (let n = 0; n < 128; n++) {
                    midiNotes[channel][n].down = false;
                    if (!sustain[channel]) midiNotes[channel][n].held = false;
                }
                selectHeldNote();
                break;
            default:
                break;
        }
    }
}

function toggleDrone(forceState) {
    ensureAudio();
    if (!synthCore) return;
    const btn = document.getElementById("droneBtn");
    const next = (forceState !== undefined) ? forceState : !synthCore.params.drone;
    synthCore.params.drone = next;
    btn.classList.toggle("active", next);
    btn.setAttribute("aria-pressed", next ? "true" : "false");
}

function stopAll() {
    ensureAudio();
    clearNotes();
    if (synthCore) synthCore.reset();
    toggleDrone(false);
}

function setupKeyboard() {
    const keyboard = document.getElementById("keyboardContainer");
    const notes = [
        { n: 48, black: false }, { n: 49, black: true },
        { n: 50, black: false }, { n: 51, black: true },
        { n: 52, black: false },
        { n: 53, black: false }, { n: 54, black: true },
        { n: 55, black: false }, { n: 56, black: true },
        { n: 57, black: false }, { n: 58, black: true },
        { n: 59, black: false },
        { n: 60, black: false }, { n: 61, black: true },
        { n: 62, black: false }, { n: 63, black: true },
        { n: 64, black: false },
        { n: 65, black: false }, { n: 66, black: true },
        { n: 67, black: false }, { n: 68, black: true },
        { n: 69, black: false }, { n: 70, black: true },
        { n: 71, black: false },
        { n: 72, black: false }
    ];

    const whiteCount = notes.filter(n => !n.black).length;
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
            noteOn(0, note.n, 0.85);
        };

        const release = (e) => {
            e.preventDefault();
            noteOff(0, note.n);
        };

        key.addEventListener("mousedown", press);
        key.addEventListener("mouseup", release);
        key.addEventListener("mouseleave", release);
        key.addEventListener("touchstart", press, { passive: false });
        key.addEventListener("touchend", release);
        key.addEventListener("touchcancel", release);

        keyboard.appendChild(key);
    });
}

function highlightKey(noteNum, active) {
    const key = document.querySelector(`[data-note="${noteNum}"]`);
    if (key) key.classList.toggle("active", active);
}

window.addEventListener("DOMContentLoaded", () => {
    const grid = document.getElementById("controlsGrid");
    PARAMS.forEach(p => {
        knobInstances[p.id] = new RotaryKnob(p, grid);
    });

    document.querySelectorAll(".mode-btn").forEach((btn, idx) => {
        btn.addEventListener("click", () => {
            ensureAudio();
            setMode(idx);
        });
    });

    document.getElementById("droneBtn").addEventListener("click", () => toggleDrone());
    document.getElementById("stopBtn").addEventListener("click", () => stopAll());

    setupKeyboard();
    setupWebMidi();
    setMode(0);

    const armAudio = () => ensureAudio();
    window.addEventListener("click", armAudio, { once: true });
    window.addEventListener("touchstart", armAudio, { once: true });
    window.addEventListener("keydown", armAudio, { once: true });
});
