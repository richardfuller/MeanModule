from dsl import *

section('Set')
# Set 1 Industrial Decay
pre('Set Industrial Decay 1 - Bass', w1=4, sh1=.4, w2=4, sh2=.6, f2=8, l2=.7, cut=.4, res=.3, fd=.8, dr=.9, fm=.4, e1d=.4, lr=.3, shm=.7, mx=[('VEL', 'DRV', .5)])
pre('Set Industrial Decay 2 - Lead', w1=1, fmode=1, cut=.55, res=.95, fd=.6, dr=.4, fm=.6, e1d=.5, e2s=.9, mx=[('WHL', 'CUT', .5)])
pre('Set Industrial Decay 3 - Drone', w1=4, sh1=.5, cut=.55, res=.3, fd=.5, dr=.3, fm=0, e2a=.3, e2r=.5, lw=1, lr=.4, o1l=.2, mx=[('AT', 'LFR', .5)])
# Set 2 Darkwave Drive
pre('Set Darkwave Drive 1 - Bass', w1=1, cut=.32, res=.3, fd=.6, dr=.4, fm=.6, e1d=.3, e2d=.45, e2s=.5, e2r=.1, mx=[('VEL', 'ENV', .5)])
pre('Set Darkwave Drive 2 - Stab', w1=1, w2=1, c2=3, f2=3, l2=.8, cut=.45, res=.3, fd=.55, dr=.3, fm=.3, e1d=.4, e2d=.4, e2s=.06, e2r=.15, mx=[('KEY', 'CUT', .4)])
pre('Set Darkwave Drive 3 - Arp', w1=2, w2=2, f2=6, l2=.7, fmode=2, cut=.35, res=.25, fd=.4, dr=.15, fm=.2, e1d=.3, e2d=.45, e2s=.12, e2r=.12, lr=.4, mx=[('LFO', 'CUT', .15)])
# Set 3 Void Walker
pre('Set Void Walker 1 - Sub', w1=2, l1=.7, cut=.38, res=.05, fd=.25, dr=.08, fm=0, kt=.3, e2r=.25, vela=.5, drift=.5, l2r=.13, mx=[('LF2', 'SH1', .4)])
pre('Set Void Walker 2 - Lead', w1=0, w2=2, sh2=.3, c2=12, f2=0, l1=0, l2=.9, fmode=2, cut=.2, res=.18, fd=.3, dr=.1, fm=0, e2a=.15, e2s=1, e2r=.5, gl=.65, leg=1, vib=.1, mx=[('SX', 'GLD', .7)])
pre('Set Void Walker 3 - Texture', l1=0, nz=.7, nc=.4, fmode=1, cut=.45, res=.45, fd=.3, dr=.1, fm=.6, e1a=.75, e1d=.9, e1s=.35, lfc=.3, lr=.15, e2a=.5, e2r=.6, mx=[('WHL', 'NSE', .5)])
# Set 4 Acid Reactor
pre('Set Acid Reactor 1 - Bass', w1=1, cut=.3, res=.8, fd=.7, dr=.3, fm=.8, e1d=.42, e2d=.55, e2s=.7, e2r=.12, gl=.08, leg=1, mx=[('VEL', 'ENV', .5)])
pre('Set Acid Reactor 2 - Pluck', w1=4, sh1=.35, cut=.4, res=.6, fd=.6, dr=.25, fm=.9, e1d=.35, e2d=.4, e2s=.06, e2r=.1, mx=[('WHL', 'RES', .3)])
pre('Set Acid Reactor 3 - SFX', w1=1, l1=.3, cut=.55, res=.6, fd=.5, dr=.2, fm=0, lfc=.5, ls=8, lw=1, lr=.5, e2a=.1, e2r=.3, mx=[('SY', 'LFR', .5)])
# Set 5 Cybernetic
pre('Set Cybernetic 1 - Bass', w1=4, sh1=.5, cut=.55, res=.2, fd=.6, dr=.3, fm=0, o1e=.8, e1d=.45, e2d=.45, e2s=.1, e2r=.12, mx=[('VEL', 'E2D', .4)])
pre('Set Cybernetic 2 - Arp', w1=2, w2=2, c2=7, f2=0, l2=.6, xm=.25, cut=.8, res=.15, fd=.4, dr=.15, fm=0, e2d=.35, e2s=.05, e2r=.15, mx=[('KEY', 'XMD', .4)])
pre('Set Cybernetic 3 - Pad', w1=4, sh1=.5, cut=.6, res=.3, fd=.5, dr=.25, fm=0, lw=1, lr=.7, lg=1, e2a=.1, e2d=.3, e2s=.5, e2r=.4, mx=[('AT', 'LFR', .5)])
# Set 6 Cryptic Ritual
pre('Set Cryptic Ritual 1 - Bass', w1=4, sh1=.35, fmode=1, cut=.35, res=.5, fd=.6, dr=.3, fm=0, lr=.15, shm=.6, e2r=.2, mx=[('WHL', 'CUT', .5)])
pre('Set Cryptic Ritual 2 - Chime', w1=2, w2=2, c2=6, f2=0, l2=.7, cut=.8, res=.15, fd=.4, dr=.15, fm=0, o1e=.3, e1d=.5, e2d=.7, e2s=.05, e2r=.6, mx=[('VEL', 'FM1', .4)])
pre('Set Cryptic Ritual 3 - Drone', w1=1, w2=1, f2=12, l2=.8, cut=.4, res=.25, fd=.5, dr=.25, fm=0, e2a=.6, e2r=.7, mx=[('SX', 'DRV', .5)])
# Set 7 Warehouse Rave
pre('Set Warehouse Rave 1 - Bass', w1=4, sh1=.5, l1=1, cut=.45, res=.25, fd=.9, dr=1, fm=.3, e1d=.35, e2d=.5, e2s=.6, e2r=.1, vela=.5)
pre('Set Warehouse Rave 2 - Stab', w1=1, w2=4, sh2=.3, c2=7, f2=2, l2=.7, cut=.36, res=.3, fd=.85, dr=.35, fm=.7, e1d=.46, e2d=.4, e2s=.1, e2r=.12, mx=[('WHL', 'E1D', .4)])
pre('Set Warehouse Rave 3 - Sweep', l1=0, nz=.8, nc=.1, cut=.2, res=.4, fd=.4, dr=.15, fm=0, e2a=.85, e2s=1, e2r=.4, mx=[('AEN', 'CUT', .9), ('AT', 'RES', .4)])
# Set 8 Alien Flora
pre('Set Alien Flora 1 - Pluck', w1=4, sh1=.05, cut=.6, res=.2, fd=.4, dr=.15, fm=.3, e1d=.35, e2d=.4, e2s=.05, e2r=.12, mx=[('OS2', 'SH1', .5, 'VEL')])
pre('Set Alien Flora 2 - Lead', w1=2, w2=2, f2=6, l2=.6, cut=.8, res=.2, fd=.4, dr=.15, fm=0, e2s=.9, lr=.6, o1l=.25, mx=[('WHL', 'LFR', .4)])
pre('Set Alien Flora 3 - Pad', w1=2, w2=2, c2=12, f2=0, l2=.6, fmode=2, cut=.55, res=.3, fd=.4, dr=.15, fm=0, e2a=.6, e2r=.6, mx=[('OS2', 'CUT', .1), ('KEY', 'RES', .3)])
