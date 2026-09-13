from pathlib import Path
from html import escape
import json
from PIL import ImageFont

OUT = Path(__file__).parent
AUDIO = '#087f83'
CV = '#8256b3'
INK = '#183139'
MUTED = '#526b73'


def fit(value, preferred, width, bold=False):
    font = '/usr/share/fonts/truetype/dejavu/DejaVuSans' + ('-Bold.ttf' if bold else '.ttf')
    measured = ImageFont.truetype(font, preferred).getlength(value)
    return min(preferred, max(13, int(preferred * width / max(measured, 1))))


class Patch:
    def __init__(self, name, title, subtitle, modules):
        self.name = name
        self.ports = {}
        self.connections = []
        self.parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="1600" height="1120" viewBox="0 0 1600 1120">',
            '<rect width="100%" height="100%" fill="white"/>',
            '<defs><marker id="audio" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#087f83"/></marker><marker id="cv" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#8256b3"/></marker></defs>']
        self.text(60, 62, title, 43, bold=True)
        self.text(60, 104, subtitle, 24, color=MUTED)
        self.text(60, 144, modules, 20, color=MUTED)
        self.wire([(60, 186), (150, 186)], 'audio')
        self.text(172, 194, 'AUDIO / sound path', 22, color=AUDIO)
        self.wire([(490, 186), (580, 186)], 'cv')
        self.text(603, 194, 'CV / modulation', 22, color=CV)
        self.text(1010, 194, 'Arrows point from output to input.', 20, color=MUTED)

    def text(self, x, y, value, size=22, anchor='start', color=INK, bold=False):
        self.parts.append(f'<text x="{x}" y="{y}" font-family="DejaVu Sans, sans-serif" font-size="{size}" font-weight="{700 if bold else 400}" text-anchor="{anchor}" fill="{color}">{escape(value)}</text>')

    def wire(self, pts, kind, marker=True):
        color = AUDIO if kind == 'audio' else CV
        dash = ' stroke-dasharray="11 8"' if kind == 'cv' else ''
        end = f' marker-end="url(#{kind})"' if marker else ''
        points = ' '.join(f'{x},{y}' for x, y in pts)
        self.parts.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="4" stroke-linejoin="round"{dash}{end}/>')

    def node(self, id_, x, y, w, h, title, detail, ports, kind='audio', optional=False):
        color = AUDIO if kind == 'audio' else CV
        fill = '#f7fbfb' if kind == 'audio' else '#faf7fd'
        dash = ' stroke-dasharray="8 5"' if optional else ''
        if optional: color = '#85959a'
        self.parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="12" fill="{fill}" stroke="{color}" stroke-width="2.5"{dash}/>')
        shift = 10 if any(port[1]=='top' for port in ports) else 0
        self.text(x+w/2, y+35+shift, title, fit(title,24,w-24,True), 'middle', bold=True)
        if detail: self.text(x+w/2, y+64+shift, detail, fit(detail,18,w-24), 'middle', color=MUTED)
        for key, side, fraction, label, typ in ports:
            pcolor = AUDIO if typ == 'audio' else CV
            if side == 'left':
                px, py = x, y+h*fraction
                tx, ty, anchor = px+13, py+6, 'start'
            elif side == 'right':
                px, py = x+w, y+h*fraction
                tx, ty, anchor = px-13, py+6, 'end'
            elif side == 'bottom':
                px, py = x+w*fraction, y+h
                tx, ty, anchor = px, py-13, 'middle'
            else:
                px, py = x+w*fraction, y
                tx, ty, anchor = px, py+17, 'middle'
            self.parts.append(f'<circle cx="{px}" cy="{py}" r="6" fill="white" stroke="{pcolor}" stroke-width="2.5"/>')
            self.text(tx, ty, label, 16, anchor, color=pcolor)
            self.ports[f'{id_}.{key}'] = (px, py)

    def connect(self, source, target, kind, via=()):
        assert source in self.ports and target in self.ports
        self.wire([self.ports[source], *via, self.ports[target]], kind)
        self.connections.append({'from':source, 'to':target, 'role':kind})

    def controls(self, entries, start, note):
        self.text(60, 827, 'KNOBS TO PLAY', 24, bold=True, color=AUDIO)
        for i, (title, description, sub) in enumerate(entries):
            x = 60+i*382
            self.parts.append(f'<rect x="{x}" y="851" width="358" height="134" rx="10" fill="#f5f8f8"/>')
            self.parts.append(f'<circle cx="{x+44}" cy="894" r="23" fill="white" stroke="{INK}" stroke-width="2.5"/>')
            self.parts.append(f'<path d="M{x+44},894 L{x+55},878" stroke="{INK}" stroke-width="3"/>')
            self.text(x+82, 889, title, fit(title,20,255,True), bold=True)
            self.text(x+82, 920, description, fit(description,19,255), color=MUTED)
            self.text(x+22, 961, sub, fit(sub,18,314), color=MUTED)
        self.text(60, 1024, 'START: '+start, 21)
        self.text(60, 1062, note, 20, color=MUTED)
        self.text(60, 1100, 'Generic patch recipe • jack names and knob ranges vary by module • 2026-09-13 • see PATCH-GUIDE.md', 18, color=MUTED)

    def save(self):
        self.parts.append('</svg>')
        (OUT/f'{self.name}.svg').write_text('\n'.join(self.parts)+'\n')
        return {'name':self.name, 'ports':list(self.ports), 'cables':self.connections}


recipes=[]
p=Patch('01-detuned-drone', '01 / Slow detuned drone',
    'Two close pitches, a dark filter, and a slow change in brightness.',
    'Modules: 2 VCOs • mixer • low-pass VCF • VCA with bias • LFO • attenuator • output')
p.node('vco1',60,260,210,135,'VCO 1','triangle • ~110 Hz',[('out','right',.65,'TRI OUT','audio')])
p.node('vco2',60,470,210,135,'VCO 2','triangle • detune',[('out','right',.65,'TRI OUT','audio')])
p.node('mix',405,365,185,175,'MIXER','both levels modest',[
    ('in1','left',.45,'IN 1','audio'),('in2','left',.8,'IN 2','audio'),('out','right',.65,'OUT','audio')])
p.node('vcf',735,365,205,175,'LOW-PASS','cutoff ~800 Hz',[
    ('in','left',.65,'IN','audio'),('out','right',.65,'LP OUT','audio'),('cv','bottom',.5,'CUTOFF CV','cv')])
p.node('vca',1050,365,205,175,'VCA','bias / gain open',[
    ('in','left',.65,'IN','audio'),('out','right',.65,'OUT','audio')])
p.node('output',1370,365,170,175,'OUTPUT','level turned down',[('in','left',.65,'AUDIO IN','audio')])
p.node('lfo',405,635,185,130,'LFO','sine • 0.05 Hz',[('out','right',.68,'SINE OUT','cv')],kind='cv')
p.node('depth',735,635,205,130,'ATTENUATOR','start near zero',[
    ('in','left',.68,'IN','cv'),('out','right',.68,'OUT','cv')],kind='cv')
p.connect('vco1.out','mix.in1','audio',[(337,347.75),(337,443.75)])
p.connect('vco2.out','mix.in2','audio',[(337,557.75),(337,505)])
p.connect('mix.out','vcf.in','audio')
p.connect('vcf.out','vca.in','audio')
p.connect('vca.out','output.in','audio')
p.connect('lfo.out','depth.in','cv')
p.connect('depth.out','vcf.cv','cv',[(985,723.4),(985,585),(837.5,585)])
p.text(1075,632,'No envelope needed:',21,color=MUTED)
p.text(1075,665,'hold the VCA open.',21,color=MUTED)
p.controls([
    ('VCO 2 FINE','beat speed','Start a few cents from VCO 1.'),
    ('FILTER CUTOFF','dark ↔ bright','Sweep slowly around the base pitch.'),
    ('LFO DEPTH','amount of movement','Small sweeps keep it calm.'),
    ('RESONANCE','soft ↔ focused','Begin low; add a little at a time.')],
    'Match pitches, detune slightly, open the VCA, then add a little filter modulation.',
    '0.05 Hz means one modulation cycle every 20 seconds. Saw waves make a brighter variation.')
recipes.append(p.save())

p=Patch('02-breathing-noise','02 / Breathing noise',
    'Wind, surf, and airy swells from one noise source and one slow LFO.',
    'Modules: noise • band-pass VCF • VCA with bias • LFO • 2 attenuators + mult • optional reverb • output')
p.node('noise',60,365,200,175,'NOISE','pink or white',[('out','right',.65,'NOISE OUT','audio')])
p.node('vcf',370,365,210,175,'BAND-PASS','center ~1 kHz',[
    ('in','left',.65,'IN','audio'),('out','right',.65,'BP OUT','audio'),('cv','bottom',.5,'FREQ CV','cv')])
p.node('vca',690,365,210,175,'VCA','some initial gain',[
    ('in','left',.65,'IN','audio'),('out','right',.65,'OUT','audio'),('cv','bottom',.5,'LEVEL CV','cv')])
p.node('fx',1030,365,210,175,'REVERB','optional • ~20% wet',[
    ('in','left',.65,'IN','audio'),('out','right',.65,'OUT','audio')],optional=True)
p.node('output',1390,365,150,175,'OUTPUT','low level',[('in','left',.65,'AUDIO IN','audio')])
p.node('lfo',60,640,200,120,'LFO','sine • 0.05 Hz',[('out','right',.7,'OUT','cv')],kind='cv')
p.node('filter_depth',370,635,210,125,'FILTER DEPTH','small sweep',[
    ('in','left',.68,'IN','cv'),('out','right',.68,'OUT','cv')],kind='cv')
p.node('amp_depth',690,635,210,125,'VOLUME DEPTH','gentle swells',[
    ('in','left',.68,'IN','cv'),('out','right',.68,'OUT','cv')],kind='cv')
p.connect('noise.out','vcf.in','audio')
p.connect('vcf.out','vca.in','audio')
p.connect('vca.out','fx.in','audio')
p.connect('fx.out','output.in','audio')
p.ports['mult.in']=(310,724)
p.ports['mult.out1']=(310,724)
p.ports['mult.out2']=(310,724)
p.connect('lfo.out','mult.in','cv')
p.connect('mult.out1','filter_depth.in','cv',[(310,720)])
p.connect('mult.out2','amp_depth.in','cv',[(310,784),(650,784),(650,720)])
p.parts.append(f'<circle cx="310" cy="724" r="5" fill="{CV}"/>')
p.text(286,695,'MULT',16,color=CV)
p.connect('filter_depth.out','vcf.cv','cv',[(625,720),(625,580),(475,580)])
p.connect('amp_depth.out','vca.cv','cv',[(960,720),(960,580),(795,580)])
p.text(1040,641,'Split ONE LFO output.',21,color=MUTED)
p.text(1040,677,'Each depth knob controls a separate CV.',20,color=MUTED)
p.text(1040,721,'No reverb? Patch VCA → OUTPUT.',20,color=MUTED)
p.controls([
    ('FILTER FREQ','size of the wind','Low = muffled; high = airy.'),
    ('RESONANCE','whistling detail','Stay below self-oscillation at first.'),
    ('LFO RATE','breathing speed','Try 10–40 seconds per cycle.'),
    ('VOLUME DEPTH','soft swell ↔ silence','Keep some sound at the LFO trough.')],
    'Noise into filter; low resonance. Add quiet VCA gain, then both LFO depth controls.',
    'A low-pass output also works. Increase VCA bias or reduce CV depth if the sound closes off too much.')
recipes.append(p.save())

p=Patch('03-metallic-drone','03 / Slowly shifting metallic drone',
    'A sine oscillator modulates a second oscillator at audio rate; a slow LFO adds drift.',
    'Modules: 2 VCOs (carrier needs linear FM) • low-pass VCF • VCA with bias • LFO • 2 attenuators • output')
p.node('lfo',60,225,210,120,'LFO','sine • ~0.03 Hz',[('out','bottom',.5,'SINE OUT','cv')],kind='cv')
p.node('drift',60,390,210,115,'DRIFT DEPTH','very small',[
    ('in','top',.5,'IN','cv'),('out','bottom',.5,'OUT','cv')],kind='cv')
p.node('mod',60,575,210,175,'VCO 2','modulator • ~156 Hz',[
    ('cv','top',.5,'PITCH CV','cv'),('out','right',.65,'SINE OUT','audio')])
p.node('fm_depth',405,575,210,175,'FM DEPTH','start at zero',[
    ('in','left',.65,'IN','cv'),('out','right',.65,'OUT','cv')],kind='cv')
p.node('carrier',405,335,210,175,'VCO 1','carrier • ~110 Hz',[
    ('fm','bottom',.5,'LINEAR FM IN','cv'),('out','right',.65,'SINE OUT','audio')])
p.node('vcf',765,335,210,175,'LOW-PASS','start fairly open',[
    ('in','left',.65,'IN','audio'),('out','right',.65,'LP OUT','audio')])
p.node('vca',1100,335,195,175,'VCA','bias / gain open',[
    ('in','left',.65,'IN','audio'),('out','right',.65,'OUT','audio')])
p.node('output',1395,335,150,175,'OUTPUT','low level',[('in','left',.65,'AUDIO IN','audio')])
p.connect('lfo.out','drift.in','cv')
p.connect('drift.out','mod.cv','cv')
p.connect('mod.out','fm_depth.in','cv')
p.connect('fm_depth.out','carrier.fm','cv',[(665,688.75),(665,545),(510,545)])
p.connect('carrier.out','vcf.in','audio')
p.connect('vcf.out','vca.in','audio')
p.connect('vca.out','output.in','audio')
p.text(770,639,'The FM cable carries audio-rate modulation.',22,color=CV)
p.text(770,678,'Raise FM depth slowly to bring out sidebands.',21,color=MUTED)
p.text(770,720,'Use the LINEAR FM jack on the carrier.',21,color=MUTED)
p.controls([
    ('FM DEPTH','main timbre control','Pure tone → richer / metallic.'),
    ('VCO 2 PITCH','change the frequency ratio','Move away from simple octaves.'),
    ('FILTER CUTOFF','soften the edges','Lower it as the sound gets brighter.'),
    ('DRIFT DEPTH','stable ↔ slowly moving','Use only slight pitch movement.')],
    'Sine waves; VCO 1 near 110 Hz, VCO 2 near 156 Hz. Start with FM and drift at zero.',
    'These are starting points, not tuning requirements. Linear FM depth and pitch stability depend on the oscillator.')
recipes.append(p.save())

(OUT/'patch-connections.json').write_text(json.dumps(recipes,indent=2)+'\n')
print(json.dumps({'patches':len(recipes),'cables':[len(r['cables']) for r in recipes]}))
