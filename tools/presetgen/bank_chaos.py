from dsl import *

section('Chaos')
# --- Glitch and malfunction
pre('Chaos Glitch Box', w1=1, w2=4, sh2=.4, c2=-12, f2=0, cut=.55, res=.35, fd=.6, dr=.3, fm=.4, e1d=.35, e2d=.3, e2s=.4, e2r=.1, lw=1, lr=.7, lg=1, mx=[('SX', 'LFR', .5)])
pre('Chaos Short Circuit', w1=2, l1=.8, nz=.15, cut=.7, res=.3, fd=.6, dr=.4, fm=0, e2d=.35, e2s=.1, e2r=.1, mx=[('NSE', 'FM1', .2), ('VEL', 'AMP', .5, 'AEN')])
pre('Chaos CPU Melt', w1=1, w2=1, f2=15, cut=.5, res=.3, fd=.6, dr=.4, fm=0, lr=.5, e2a=.1, e2r=.4, drift=1, mx=[('AEN', 'LFR', -.7), ('WHL', 'PCH', .05, 'LFO')])
pre('Chaos Wire Tap', l1=0, nz=.5, nc=0, fmode=2, cut=.6, res=.4, fd=.5, dr=.3, fm=0, lr=.55, e2a=.05, e2r=.2, mx=[('LFO', 'NSE', .5)])
pre('Chaos Tape Snag', w1=2, w2=2, f2=9, cut=.6, res=.15, fd=.4, dr=.2, fm=0, lr=.2, drift=.9, e2a=.1, e2r=.4, atd=0, mx=[('LFO', 'PCH', .1), ('AT', 'LD1', .8)])
pre('Chaos Broken Jack', w1=1, w2=1, f2=10, cut=.6, res=.2, fd=.5, dr=.3, fm=0, lw=1, lr=.5, e2a=.02, e2r=.1, mx=[('LFO', 'AMP', -.9), ('VEL', 'LFR', .5)])
# --- Sci-fi and lasers
pre('Chaos Ray Gun', w1=4, sh1=.5, cut=.7, res=.25, fd=.5, dr=.25, fm=0, o1e=.9, e1d=.5, e2d=.5, e2s=.08, e2r=.15, mx=[('VEL', 'ENV', .3)])
pre('Chaos UFO Hover', w1=2, w2=2, f2=12, cut=.4, res=.9, fd=.5, dr=.2, fm=0, lfc=.5, lr=.4, e2a=.2, e2r=.5, mx=[('SY', 'CUT', .4)])
pre('Chaos Teleport', w1=0, w2=2, c2=12, f2=0, l1=0, l2=.9, cut=.8, res=.2, fd=.4, dr=.2, fm=0, o2e=.8, e1a=.55, e1d=.7, e1s=.3, e1r=.3, e2a=.3, e2r=.5, mx=[('WHL', 'DRV', .5)])
pre('Chaos Alien Comm', w1=4, sh1=.5, fmode=1, cut=.5, res=.55, fd=.5, dr=.2, fm=0, lw=1, lr=.65, shm=.8, e2a=.02, e2r=.2, mx=[('AT', 'LFR', .4)])
pre('Chaos Warp Drive', w1=1, w2=1, f2=8, cut=.5, res=.3, fd=.5, dr=.2, fm=0, lr=.3, o1l=.25, e2a=.4, e2r=.5, mx=[('WHL', 'LFR', .7)])
pre('Chaos Mothership', w1=1, w2=1, c1=-24, c2=-24, f2=12, cut=.3, res=.35, fd=.6, dr=.3, fm=.8, e1a=.8, e1d=.9, e1s=.4, e1r=.7, e2a=.5, e2r=.7, mx=[('SX', 'DRV', .5)])
pre('Chaos Plasma Beam', w1=1, l1=.5, nz=.3, nc=.1, cut=.55, res=.4, fd=.6, dr=.3, fm=0, e2a=.05, e2r=.2, mx=[('NSE', 'CUT', .1), ('KEY', 'RES', .3)])
# --- Industrial noise and impacts
pre('Chaos Steam Vent', l1=0, nz=.9, nc=0, fmode=2, cut=.6, res=.3, fd=.4, dr=.1, fm=0, e2a=.05, e2d=.5, e2s=.5, e2r=.35, mx=[('VEL', 'NSE', .5, 'AEN')])
pre('Chaos Factory Crash', w1=1, l1=.4, nz=.7, nc=.2, cut=.5, res=.25, fd=.8, dr=1, fm=.5, e1d=.6, e2d=.6, e2s=.1, e2r=.4, atd=1, ata=.6)
pre('Chaos Scrap Metal', w1=1, l1=.5, cut=.55, res=.5, fd=.7, dr=.4, fm=0, lfc=.5, ls=8, lw=1, lr=.5, e2a=.05, mx=[('WHL', 'RES', .3)])
pre('Chaos Pressure Valve', w1=1, l1=.3, cut=.3, res=.97, fd=.5, dr=.2, fm=.8, e1d=.55, e2d=.55, e2s=.2, e2r=.3, mx=[('VEL', 'E1D', .4)])
pre('Chaos Ground Fault', w1=1, cut=.6, res=.3, fd=.7, dr=.5, fm=0, lw=1, lr=.6, shm=.5, e2a=.02, mx=[('SY', 'LFR', .5)])
# --- Audio-rate mayhem
pre('Chaos Feedback Loop', w1=1, l1=.25, fmode=1, cut=.5, res=.98, fd=.6, dr=.4, fm=0, kt=1, e2a=.1, e2r=.3, mx=[('KEY', 'CUT', .4)])
pre('Chaos Tear Gas', w1=1, w2=2, c2=12, f2=0, l1=.6, l2=.3, cut=.55, res=.5, fd=.8, dr=.6, fm=0, e2a=.05, e2r=.2, mx=[('OS2', 'CUT', .1, 'WHL')])
pre('Chaos Modem Dial', w1=4, sh1=.5, cut=.8, res=.2, fd=.4, dr=.2, fm=0, ls=8, lw=1, lr=.4, e2a=.01, e2r=.1, mx=[('LFO', 'PCH', .3), ('SX', 'LFR', .4)])
pre('Chaos Screech', w1=2, w2=2, c2=12, f2=0, l2=.4, cut=.85, res=.3, fd=.5, dr=.3, fm=0, ls=8, lr=1, o1l=.3, e2a=.02, e2r=.2, atd=4, ata=.5)
pre('Chaos Geiger', l1=0, nz=.8, nc=0, fmode=2, cut=.55, res=.3, fd=.4, dr=.3, fm=0, lw=4, lr=.9, e2a=.0, e2r=.1, mx=[('LFO', 'AMP', 1)])
pre('Chaos Radio Static', w1=4, sh1=.5, nz=.3, nc=.2, cut=.45, res=.4, fd=.6, dr=.3, fm=0, e2a=.02, e2r=.2, mx=[('NSE', 'SH1', .8), ('WHL', 'CUT', .6)])
