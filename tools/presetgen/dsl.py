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
    atd='AT_DEST', ata='AT_AMOUNT', oct='OCTAVE', tune='TUNE', arp='ARP_MODE', arpr='ARP_RATE', arpo='ARP_OCT', arpg='ARP_GATE', pri='PRIORITY', grit='GRIT', trim='TRIM',
)
SRC = dict(LFO='S_LFO', FEN='S_E1', AEN='S_E2', VEL='S_VEL', WHL='S_WHL', AT='S_AT', SX='S_SX', SY='S_SY', KEY='S_KEY', RND='S_RND', LF2='S_LF2',
           NSE='S_NSE', OS2='S_OS2', BND='S_BND', ARP='S_ARP', AGE='S_AGE', SMR='S_SMR', OS1='S_OS1', PED='S_PED')
DST = dict(PCH='D_PCH', PC2='D_PC2', SH1='D_SH1', SH2='D_SH2', LV1='D_LV1', LV2='D_LV2', NSE='D_NSE', CUT='D_CUT', RES='D_RES', INP='D_INP', DRV='D_DRV',
           AMP='D_AMP', XMD='D_XMD', LFR='D_LFR', LR2='D_LR2', FM1='D_FM1', FM2='D_FM2', GRT='D_GRT', GLD='D_GLD', VIB='D_VIB', LD1='D_LD1', LD2='D_LD2',
           ENV='D_ENV', DFT='D_DFT', PC1='D_PC1', E1A='D_E1A', E1D='D_E1D', E2A='D_E2A', E2D='D_E2D')
# defaults of the plugin (a preset starts from these); used only to skip values that equal the default
DEF = dict(w1=1, sh1=.5, w2=0, sh2=.5, c1=0, f1=0, c2=0, f2=7, l1=.8, l2=.6, nz=0, nc=0, cut=.677, res=.3, fd=.5, fmode=0, fm=.4, lfc=0, kt=.5, dr=.2,
           e1a=0, e1d=.45, e1s=0, e1r=.3, e2a=0, e2d=.5, e2s=1, e2r=.35, o1e=0, o2e=0, o1l=0, o2l=0, xm=0, shm=0, lr=.35, lw=0, ls=0, l2r=.3, l2w=0, l2s=0, lg=0,
           gl=0, leg=0, vib=0, vibr=.5, drift=.3, drone=0, vol=.7, velc=0, vela=0, atd=2, ata=.5, oct=0, tune=0, arp=0, arpr=3, arpo=1, arpg=.6, pri=0, grit=0, trim=.5)

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
ROWS = []
NAMES = set()

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
    apply_character(name, kw)
    if name in TRIMS: kw['trim'] = TRIMS[name]
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
