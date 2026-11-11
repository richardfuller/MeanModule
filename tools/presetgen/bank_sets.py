from dsl import *
import dsl

section('Set')
# Five sets of four presets that are meant to be played together. Each set splits the sonic space so the four do not fight:
#   Sub / Bass : below ~150 Hz, low cutoff, almost no top end (home register C1-C3)
#   Drone / Pad / Texture / Sweep : the middle (BP or HP filter, slow attack), keeps out of the bass and under the lead
#   Lead / Stab / Chime : the top voice, brighter, one clear line
#   Arp / Pluck : rhythm in the top octave, short notes that leave room
# Numbers 1..4 are the same on every set (1 low, 2 middle, 3 top, 4 rhythm). They use the Malevolent features: sync, aux feedback, shape flip,
# saw an octave up, linear FM, integrator saturation.

# Set 1 Rust Cathedral: ritual, industrial, low and hollow
pre('Set Rust Cathedral 1 - Sub', w1=2, l1=.9, cut=.33, res=.1, fd=.4, dr=.15, fm=0, kt=.3, af=.12, e2a=.15, e2r=.3, vela=.5, drift=.4, l2r=.13, mx=[('LF2', 'SH1', .3)])
pre('Set Rust Cathedral 2 - Drone', w1=4, sh1=.4, w2=4, sh2=.6, f2=11, l2=.8, fmode=1, cut=.5, res=.45, fd=.5, dr=.3, fm=0, e2a=.55, e2r=.7, isat=.4, lr=.2, lfc=.35, mx=[('LF2', 'SH2', .3)])
pre('Set Rust Cathedral 3 - Lead', w1=1, w2=1, c2=7, f2=0, l1=.35, l2=.9, sync=1, o2e=.5, cut=.7, res=.3, fd=.7, dr=.5, fm=.3, e1d=.55, e1s=.3, e2s=.9, e2r=.5, gl=.1, leg=1, vib=.05,
    mx=[('WHL', 'PC2', .5), ('AT', 'PC2', .6)])
pre('Set Rust Cathedral 4 - Arp', w1=4, sh1=.3, w2=4, sh2=.7, c2=12, f2=0, l2=.5, cut=.62, res=.45, fd=.7, dr=.4, fm=.4, e1d=.5, e2d=.4, e2s=.1, e2r=.1, shm=.4, lr=.55, mx=[('ARP', 'CUT', .3)])

# Set 2 Dead Frequencies: a broadcast from nowhere, thin and eerie
pre('Set Dead Frequencies 1 - Sub', w1=2, l1=.9, cut=.28, res=.18, fd=.35, dr=.12, fm=0, kt=.3, af=.2, nz=.05, nc=.9, e2a=.2, e2r=.35, vib=.05, vibr=.35, drift=.5)
pre('Set Dead Frequencies 2 - Texture', l1=0, nz=.8, nc=.2, fmode=1, cut=.62, res=.6, fd=.5, dr=.2, fm=.3, e1a=.7, e1d=.9, e1s=.4, lfc=.4, lr=.12, e2a=.55, e2r=.65,
    mx=[('SMR', 'CUT', .3), ('WHL', 'NSE', .4)])
pre('Set Dead Frequencies 3 - Lead', w1=2, w2=2, f2=9, l2=.5, cut=.58, res=.35, fd=.6, dr=.4, fm=0, kt=.6, gl=.6, leg=1, vib=.22, vibr=.55, e2a=.15, e2s=1, e2r=.45, af=.25, mx=[('WHL', 'VIB', .4)])
pre('Set Dead Frequencies 4 - Chime', w1=2, c2=12, w2=2, l1=.8, l2=0, xm=.35, xlin=1, cut=.78, res=.2, fd=.5, dr=.25, fm=0, e1d=.55, e2d=.7, e2s=.05, e2r=.6, mx=[('VEL', 'XMD', .4)])

# Set 3 Pressure Drop: heavy, overdriven, throbbing
pre('Set Pressure Drop 1 - Bass', w1=1, w2=4, sh2=.3, c2=-12, f2=6, l2=.8, af=.5, cut=.4, res=.35, fd=.9, dr=.8, fm=.5, e1d=.45, e2d=.5, e2s=.6, e2r=.12, mx=[('VEL', 'DRV', .4)])
pre('Set Pressure Drop 2 - Pad', w1=4, sh1=.5, w2=4, sh2=.5, f2=8, l2=.7, flip=1, shm=.5, lr=.22, cut=.55, res=.3, fd=.5, dr=.3, fm=0, e2a=.55, e2r=.65, mx=[('LF2', 'SH2', .4)])
pre('Set Pressure Drop 3 - Lead', w1=1, sawo=1, w2=2, c2=7, f2=0, l2=.5, cut=.66, res=.5, onset=1, fd=.75, dr=.6, fm=.35, e1d=.55, e2s=.9, e2r=.45, gl=.12, leg=1, mx=[('WHL', 'CUT', .4)])
pre('Set Pressure Drop 4 - Sweep', l1=0, nz=.9, nc=.15, fmode=2, cut=.25, res=.5, onset=1, fd=.5, dr=.2, fm=0, af=.35, e2a=.85, e2s=1, e2r=.4, mx=[('AEN', 'CUT', .7), ('AT', 'RES', .4)])

# Set 4 Acid Hymn: resonant lines that answer each other
pre('Set Acid Hymn 1 - Bass', w1=1, cut=.28, res=.62, onset=1, fd=.75, dr=.4, fm=.8, e1d=.45, e2d=.55, e2s=.65, e2r=.12, gl=.07, leg=1, isat=.25, mx=[('VEL', 'ENV', .5)])
pre('Set Acid Hymn 2 - Pad', w1=2, w2=4, sh2=.4, c2=12, f2=0, l2=.6, fmode=2, cut=.35, res=.4, fd=.5, dr=.25, fm=0, e2a=.6, e2r=.7, lfc=.3, lr=.17, mx=[('LF2', 'CUT', .15)])
pre('Set Acid Hymn 3 - Lead', w1=4, sh1=.35, w2=1, c2=12, f2=5, l2=.4, fmode=1, cut=.62, res=.6, onset=1, fd=.7, dr=.45, fm=.5, e1d=.5, e2s=.85, gl=.15, leg=1, mx=[('WHL', 'RES', .3), ('AT', 'CUT', .4)])
pre('Set Acid Hymn 4 - Arp', w1=1, cut=.5, res=.5, fd=.7, dr=.35, fm=.7, e1d=.5, e2d=.5, e2s=.08, e2r=.1, mx=[('ARP', 'CUT', .25)])

# Set 5 Steel Weather: slow, wide, cold
pre('Set Steel Weather 1 - Sub', w1=2, w2=2, c2=-12, f2=4, l1=.9, l2=.5, cut=.3, res=.12, fd=.35, dr=.1, fm=0, kt=.3, e2a=.25, e2r=.4, drift=.5, mx=[('LF2', 'PC2', .02)])
pre('Set Steel Weather 2 - Drone', w1=1, sawo=1, w2=1, f2=15, l2=.8, cut=.55, res=.3, fd=.6, dr=.3, fm=0, e2a=.7, e2r=.75, lfc=.25, lr=.1, isat=.3, af=.15, mx=[('SX', 'DRV', .5)])
pre('Set Steel Weather 3 - Chime', c1=12, w1=2, w2=2, c2=19, f2=3, l2=.5, xm=.2, xlin=1, cut=.8, res=.2, fd=.4, dr=.15, fm=0, e1d=.5, e2d=.7, e2s=.05, e2r=.6, mx=[('KEY', 'XMD', .3)])
pre('Set Steel Weather 4 - Pluck', w1=4, sh1=.2, cut=.55, res=.4, onset=1, fd=.6, dr=.3, fm=.6, e1d=.5, e2d=.55, e2s=.05, e2r=.12, mx=[('VEL', 'CUT', .5)])
dsl.OVR['Set Rust Cathedral 1 - Sub'] = dict(res=.1)
