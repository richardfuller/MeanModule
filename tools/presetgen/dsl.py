"""Compact preset DSL -> C rows for PRESETS[] in mean_module_plugin.c.
pre('Bank Name', w1=1, cut=.5, mx=[('LFO','PCH',.1,'WHL'), ...])  (4th tuple item = Via source)"""
ID = dict(
    w1='OSC1_WAVE', sh1='OSC1_SHAPE', w2='OSC2_WAVE', sh2='OSC2_SHAPE', c1='OSC1_COARSE', f1='OSC1_FINE', c2='OSC2_COARSE', f2='OSC2_FINE',
    l1='OSC1_LEVEL', l2='OSC2_LEVEL', nz='NOISE_LEVEL', nc='NOISE_COLOR',
    cut='CUTOFF', res='RESONANCE', fd='FILTER_DRIVE', fmode='FILTER_MODE', fm='FLT_FM1', lfc='FLT_FM2', kt='KEY_TRACK', dr='DRIVE',
    e1a='ENV1_A', e1d='ENV1_D', e1s='ENV1_S', e1r='ENV1_R', e2a='ENV2_A', e2d='ENV2_D', e2s='ENV2_S', e2r='ENV2_R',
    o1e='OSC1_FM1', o2e='OSC2_FM1', o1l='OSC1_FM2', o2l='OSC2_FM2', xm='XMOD', shm='SHAPE_MOD',
    lr='LFO_RATE', lw='LFO_WAVE', ls='LFO_SYNC', l2r='LFO2_RATE', l2w='LFO2_WAVE', l2s='LFO2_SYNC', lg='LFO_GATE',
    gl='GLIDE', leg='LEGATO', vib='VIB_DEPTH', vibr='VIB_RATE', drift='DRIFT', drone='DRONE', vol='VOLUME', velc='VEL_CUTOFF', vela='VEL_AMP',
    atd='AT_DEST', ata='AT_AMOUNT', oct='OCTAVE', tune='TUNE', arp='ARP_MODE', arpr='ARP_RATE', arpo='ARP_OCT', arpg='ARP_GATE', pri='PRIORITY', grit='GRIT', trim='TRIM', kl='KEY_LVL',
)
SRC = dict(LFO='S_LFO', FEN='S_E1', AEN='S_E2', VEL='S_VEL', WHL='S_WHL', AT='S_AT', SX='S_SX', SY='S_SY', KEY='S_KEY', RND='S_RND', LF2='S_LF2',
           NSE='S_NSE', OS2='S_OS2', BND='S_BND', ARP='S_ARP', AGE='S_AGE', SMR='S_SMR', OS1='S_OS1', PED='S_PED')
DST = dict(PCH='D_PCH', PC2='D_PC2', SH1='D_SH1', SH2='D_SH2', LV1='D_LV1', LV2='D_LV2', NSE='D_NSE', CUT='D_CUT', RES='D_RES', INP='D_INP', DRV='D_DRV',
           AMP='D_AMP', XMD='D_XMD', LFR='D_LFR', LR2='D_LR2', FM1='D_FM1', FM2='D_FM2', GRT='D_GRT', GLD='D_GLD', VIB='D_VIB', LD1='D_LD1', LD2='D_LD2',
           ENV='D_ENV', DFT='D_DFT', PC1='D_PC1', E1A='D_E1A', E1D='D_E1D', E2A='D_E2A', E2D='D_E2D')
# defaults of the plugin (a preset starts from these); used only to skip values that equal the default
DEF = dict(w1=1, sh1=.5, w2=0, sh2=.5, c1=0, f1=0, c2=0, f2=7, l1=.8, l2=.6, nz=0, nc=0, cut=.677, res=.3, fd=.5, fmode=0, fm=.4, lfc=0, kt=.5, dr=.2,
           e1a=0, e1d=.45, e1s=0, e1r=.3, e2a=0, e2d=.5, e2s=1, e2r=.35, o1e=0, o2e=0, o1l=0, o2l=0, xm=0, shm=0, lr=.35, lw=0, ls=0, l2r=.3, l2w=0, l2s=0, lg=0,
           gl=0, leg=0, vib=0, vibr=.5, drift=.3, drone=0, vol=.7, velc=0, vela=0, atd=2, ata=.5, oct=0, tune=0, arp=0, arpr=3, arpo=1, arpg=.6, pri=0, grit=0, trim=.5, kl=0)

import json, os
_tp = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'trims.json')
TRIMS = json.load(open(_tp)) if os.path.exists(_tp) else {}

# ---- character pass: the filter and drive stage is what makes this synth sound like itself (a gritty, nonlinear, resonant filter and a hard-driven VCA),
# so every preset gets a floor of filter drive, drive, resonance and drift, plus a little hiss and the Grit macro. Ambient/soft presets get a gentler floor.
FULL = dict(fd=.85, dr=.5, res=.4, drift=.6, nz=.03)
SOFT = dict(fd=.7, dr=.3, res=.3, drift=.5)
SOFT_NAMES = {'Bass Sub 1', 'Bass Sub 2', 'Bass Log Tri', 'Bass Ghost Sub', 'Set Void Walker 1 - Sub', 'Lead Glide Tri', 'Lead Theremin', 'Lead Flute Mod',
              'Set Void Walker 2 - Lead', 'Horror Music Box', 'Horror Cold Breath', 'Horror Ghost Organ', 'Horror EVP', 'Horror Banshee', 'Horror Apparition',
              'Horror Crypt', 'Horror Chant', 'Set Void Walker 3 - Texture'}
FLOORS = {}   # name -> dict of minimum values (arps.py)
ARPS = {}   # name -> dict(arp=, arpr=, arpo=, arpg=), filled by arps.py
def tier(name):
    if name in SOFT_NAMES or name.startswith('Drone '): return 'soft'
    if name.startswith('Init') or name == 'Horror Calliope': return 'none'
    return 'full'
def apply_character(name, kw):
    t = tier(name)
    if t == 'none': return
    fl = FULL if t == 'full' else SOFT
    for k, v in fl.items():
        if kw.get(k, DEF[k]) < v: kw[k] = v
    kw['grit'] = max(kw.get('grit', 0), .2 if t == 'full' else .1)

# ---- envelope floors: the time knobs are exponential (0.4 = 26 ms, 0.5 = 70 ms, 0.6 = 190 ms, 0.7 = 510 ms), so the first drafts had 2-16 ms releases (notes cut off almost at once) and
# plucks and filter sweeps of 26-70 ms. These floors keep releases at 40-55 ms and plucks/sweeps at ~110-190 ms unless the preset is a deliberate short hit.
SHORT_HITS = {'Bass Thump', 'Bass FM Drop', 'Bass Zap Dive', 'Arp Snap Tri', 'Arp Nail Gun', 'Arp Glass', 'Arp Ice Pick', 'Arp Rez Pop', 'Horror Glass Break', 'Horror Door Slam',
              'Horror Scream Hit', 'Horror Psycho', 'Horror Nerve Zap', 'Chaos Anvil Hit', 'Chaos Ray Gun'}
def apply_env_floors(name, kw):
    g = lambda k: kw.get(k, DEF[k])
    def floor(k, v):
        if g(k) < v: kw[k] = v
    sus = g('e2s')
    floor('e2r', .48 if sus >= .5 else .44)
    if g('lg') == 0:                                   # LFO-retriggered presets need their short decays
        if sus <= .2: floor('e2d', .54 if name in SHORT_HITS else .6)
        elif sus < .9: floor('e2d', .55)
        if g('fm') >= .3: floor('e1d', .5 if name in SHORT_HITS else .55)
_cp = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'cuts.json')
CUTS = json.load(open(_cp)) if os.path.exists(_cp) else {}   # brightness calibration (tests/brightness.c): minimum Cutoff per preset

# ---- velocity: a register/velocity sweep (tests/register.c) showed most presets ignored velocity (0 dB between soft and hard). Played presets now get a floor of Velocity to Amp
# and Velocity to Cutoff; drones and ambient presets stay flat on purpose.
VEL_OFF_HORROR = {'Horror Calliope', 'Horror Ghost Organ', 'Horror Anxiety Pulse', 'Horror Saw Blade', 'Horror Creeping', 'Horror The Hive', 'Horror Dark Water', 'Horror Pendulum',
                  'Horror EVP', 'Horror Banshee', 'Horror Cold Breath', 'Horror Apparition', 'Horror Crypt', 'Horror Spirit Box', 'Horror Chant', 'Horror Nerve Zap'}
def apply_velocity(name, kw):
    bank = name.split()[0]
    played = False
    if bank in ('Bass', 'Lead', 'Arp', 'Para'): played = True
    elif bank == 'Chaos': played = name in ('Chaos Ray Gun', 'Chaos Anvil Hit', 'Chaos Teleport', 'Chaos Short Circuit', 'Chaos Plasma Beam')
    elif bank == 'Horror': played = name not in VEL_OFF_HORROR
    elif bank == 'Set': played = name.split(' - ')[-1] in ('Bass', 'Lead', 'Arp', 'Pluck', 'Stab', 'Chime', 'Sub')
    if not played: return
    plucky = name.startswith('Arp ') or kw.get('e2s', DEF['e2s']) <= .2
    if kw.get('vela', DEF['vela']) < (.5 if plucky else .4): kw['vela'] = .5 if plucky else .4
    if kw.get('velc', DEF['velc']) < .3: kw['velc'] = .3
_kp = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'keylvls.json')
KEYLVLS = json.load(open(_kp)) if os.path.exists(_kp) else {}   # register fit (tests/register.c): Key Level per preset
ROWS = []
NAMES = set()
FINAL = {}   # name -> (final parameter dict, matrix slots), for tools/presetgen/units.py

def fmt(v):
    if isinstance(v, int) or float(v) == int(v): return str(int(v))
    s = ('%.3f' % v).rstrip('0').rstrip('.')
    if s.startswith('0.'): s = s[1:]
    elif s.startswith('-0.'): s = '-' + s[2:]
    return s + 'f'

def pre(name, mx=(), **kw):
    assert name not in NAMES, name
    NAMES.add(name)
    if name in ARPS: kw.update(ARPS[name])
    for _k, _v in FLOORS.get(name, {}).items():
        if kw.get(_k, DEF[_k]) < _v: kw[_k] = _v
    apply_character(name, kw)
    apply_env_floors(name, kw)
    apply_velocity(name, kw)
    if name in KEYLVLS: kw['kl'] = KEYLVLS[name]
    # brightness fit: only static filters (little filter-envelope sweep); a swept filter is meant to start low, and opening it would flatten the envelope
    if name in CUTS and kw.get('fm', DEF['fm']) < .25 and kw.get('cut', DEF['cut']) < CUTS[name]: kw['cut'] = min(CUTS[name], .75)
    if name in TRIMS: kw['trim'] = TRIMS[name]
    FINAL[name] = (dict(kw), list(mx))
    items = []
    for k, v in kw.items():
        assert k in ID, (name, k)
        if k in DEF and abs(DEF[k] - v) < 1e-9: continue
        items.append('{P_%s,%s}' % (ID[k], fmt(v)))
    assert len(mx) <= 6, name
    for n, t in enumerate(mx, 1):
        s, d, a = t[0], t[1], t[2]
        assert s in SRC and d in DST, (name, t)
        items.append('MX(%d,%s,%s,%s)' % (n, SRC[s], DST[d], fmt(a)))
        if len(t) > 3:
            assert t[3] in SRC, (name, t)
            items.append('{P_M%d_VIA,%s}' % (n, SRC[t[3]]))
    if not items: items = ['{P_VOLUME,.7f}']
    # wrap at ~150 chars
    lines, cur = [], '    PRE("%s",' % name
    for it in items:
        if len(cur) + len(it) > 150:
            lines.append(cur); cur = '        '
        cur += it + ','
    lines.append(cur.rstrip(',') + '),')
    ROWS.append('\n'.join(lines))

def section(title):
    ROWS.append('    /* ---- %s ---- */' % title)
