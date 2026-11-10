from dsl import *

# Expressive Keys and Clavs. Velocity does the playing: it drives the level (Velocity to Amp), the filter (Velocity to Cutoff) and, through the matrix, the FM depth or the drive,
# so a patch barks when struck hard and muffles when played softly. Mono, so the "two hands" are a lead hand over a held key; organs are single-note organs.
section('Keys')
# --- Electric pianos
pre('Keys Tine EP', w1=2, w2=2, c2=12, f2=3, l1=.9, l2=.5, xm=.08, cut=.7, res=.2, fd=.5, dr=.15, fm=.2, e1d=.6, e2d=.76, e2s=.18, e2r=.55, vela=.8, velc=.7, lr=.7,
    mx=[('VEL', 'XMD', .3), ('LFO', 'AMP', .12, 'WHL')])
pre('Keys Wurly', w1=4, sh1=.35, w2=2, c2=12, f2=2, l1=.8, l2=.4, cut=.58, res=.3, fd=.6, dr=.25, fm=.35, e1d=.58, e2d=.68, e2s=.25, e2r=.5, vela=.8, velc=.8, lr=.7,
    mx=[('VEL', 'DRV', .35), ('LFO', 'AMP', .1, 'WHL')])
pre('Keys Suitcase', w1=2, w2=2, f2=5, l1=.9, l2=.7, cut=.52, res=.15, fd=.5, dr=.15, fm=.2, e1d=.6, e2d=.8, e2s=.22, e2r=.6, vela=.85, velc=1, lr=.65, mx=[('LFO', 'AMP', .18, 'WHL')])
pre('Keys Bell EP', w1=2, w2=2, c2=19, f2=0, l1=.8, l2=.4, xm=.15, cut=.78, res=.2, fd=.5, dr=.15, fm=.1, e2d=.82, e2s=.1, e2r=.6, vela=.8, velc=.5, mx=[('VEL', 'XMD', .4), ('AEN', 'XMD', .25)])
pre('Keys Dirty EP', w1=4, sh1=.4, w2=2, c2=12, f2=4, l1=.8, l2=.5, cut=.55, res=.35, fd=.7, dr=.4, fm=.3, e1d=.6, e2d=.7, e2s=.2, e2r=.5, vela=.75, velc=.8, lr=.7,
    mx=[('VEL', 'DRV', .6), ('LFO', 'AMP', .15, 'WHL')])
# --- Clavs
pre('Keys Clav', w1=4, sh1=.15, w2=1, c2=0, f2=6, l1=.85, l2=.4, cut=.6, res=.35, fd=.6, dr=.25, fm=.6, e1d=.58, e2d=.64, e2s=.1, e2r=.45, vela=.8, velc=.9, kt=.7, mx=[('VEL', 'ENV', .6)])
pre('Keys Funk Clav', w1=4, sh1=.2, l1=.9, fmode=1, cut=.5, res=.5, fd=.6, dr=.25, fm=.7, e1d=.56, e2d=.62, e2s=.12, e2r=.44, vela=.8, velc=.9, kt=.7, gl=.05, leg=1, mx=[('VEL', 'ENV', .5)])
pre('Keys Pick Clav', w1=4, sh1=.3, w2=4, sh2=.1, c2=12, f2=0, l1=.8, l2=.35, cut=.7, res=.3, fd=.55, dr=.2, fm=.5, e1d=.55, e2d=.6, e2s=.08, e2r=.42, vela=.85, velc=.9, kt=.7)
pre('Keys Dirty Clav', w1=4, sh1=.12, w2=1, f2=8, l1=.8, l2=.5, cut=.55, res=.4, fd=.8, dr=.5, fm=.6, e1d=.58, e2d=.64, e2s=.12, e2r=.45, vela=.8, velc=.9, kt=.6, mx=[('VEL', 'DRV', .6), ('VEL', 'ENV', .4)])
# --- Organs
pre('Keys Combo Organ', w1=4, sh1=.5, w2=4, sh2=.5, c2=12, f2=4, l1=.8, l2=.45, cut=.72, res=.2, fd=.5, dr=.2, fm=.3, e1d=.5, e2s=1, e2r=.46, vela=.3, velc=.5, vib=.25, vibr=.65)
pre('Keys Drawbar Organ', w1=2, w2=2, c2=12, f2=3, l1=.85, l2=.6, cut=.78, res=.15, fd=.5, dr=.2, fm=.35, e1d=.5, e2s=1, e2r=.46, vela=.3, velc=.5, lr=.78, mx=[('LFO', 'PCH', .02, 'WHL'), ('LFO', 'AMP', .1, 'WHL')])
pre('Keys Church Organ', w1=1, w2=2, c2=-12, f2=2, l1=.7, l2=.8, cut=.55, res=.1, fd=.5, dr=.15, fm=0, e2a=.42, e2s=1, e2r=.62, vela=.25, velc=.4, drift=.4)
pre('Keys Perc Organ', w1=2, w2=2, c2=19, f2=0, l1=.85, l2=.7, cut=.75, res=.15, fd=.5, dr=.2, fm=.3, e1d=.5, e2s=1, e2r=.45, vela=.3, velc=.5, mx=[('FEN', 'LV2', .7)])
# --- Struck and plucked keys
pre('Keys Harpsichord', w1=1, w2=4, sh2=.12, c2=0, f2=5, l1=.8, l2=.55, nz=.04, cut=.78, res=.25, fd=.5, dr=.2, fm=.3, e1d=.55, e2d=.62, e2s=.12, e2r=.42, vela=.55, velc=.6)
pre('Keys Toy Piano', w1=2, c1=12, w2=2, c2=14, f2=0, l1=.85, l2=.4, xm=.1, cut=.8, res=.2, fd=.5, dr=.15, fm=.1, e2d=.64, e2s=.06, e2r=.5, vela=.7, velc=.5, mx=[('VEL', 'XMD', .3)])
pre('Keys Marimba', w1=2, w2=2, c2=19, f2=0, l1=.9, l2=.2, cut=.62, res=.15, fd=.5, dr=.15, fm=.3, o1e=.1, e1d=.54, e2d=.66, e2s=.04, e2r=.5, vela=.8, velc=.7)
pre('Keys Vibes', w1=2, w2=2, f2=3, l1=.9, l2=.6, cut=.7, res=.15, fd=.5, dr=.15, fm=.15, e1d=.6, e2d=.84, e2s=.22, e2r=.62, vela=.75, velc=.5, lr=.58, mx=[('LFO', 'AMP', .3)])
pre('Keys Muted Piano', w1=1, w2=2, c2=-12, f2=2, l1=.7, l2=.5, cut=.45, res=.2, fd=.55, dr=.2, fm=.4, e1d=.6, e2d=.72, e2s=.1, e2r=.5, vela=.85, velc=1, mx=[('VEL', 'ENV', .5)])
pre('Keys Honky Piano', w1=1, w2=1, f2=22, l1=.8, l2=.7, cut=.62, res=.25, fd=.6, dr=.25, fm=.4, e1d=.58, e2d=.7, e2s=.1, e2r=.5, vela=.8, velc=.8)
pre('Keys FM Piano', w1=2, w2=2, c2=12, f2=0, l1=.9, l2=.5, xm=.12, cut=.8, res=.15, fd=.5, dr=.15, fm=.1, e2d=.74, e2s=.12, e2r=.55, vela=.8, velc=.4, mx=[('VEL', 'XMD', .45), ('AEN', 'XMD', .35)])
