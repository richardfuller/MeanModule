from dsl import *

section('Bass')
# --- Sub archetypes
pre('Bass Sub 1', w1=2, l1=.95, cut=.5, res=.05, fd=.25, dr=.08, fm=0, kt=.6, e2d=.5, e2r=.15, vela=.5, drift=.15)
pre('Bass Sub 2', w1=2, w2=2, f2=9, l1=.85, l2=.8, cut=.5, res=.05, fd=.25, dr=.1, fm=0, kt=.6, lr=.12, e2r=.15, vela=.4, mx=[('LFO', 'PC2', .05)])
pre('Bass Log Tri', w1=2, l1=.95, cut=.38, res=.12, fd=.3, dr=.1, fm=.25, e1d=.3, kt=.5, velc=.9, vela=.3, e2d=.55, e2s=.8, e2r=.18)
pre('Bass Ghost Sub', w1=4, sh1=.35, l1=.9, fmode=2, cut=.22, res=.28, fd=.4, dr=.15, fm=0, kt=.3, lr=.2, shm=.6, e2r=.3, mx=[('SX', 'CUT', .4)])
pre('Bass Deep FM', w1=2, w2=2, c2=7, f2=0, l1=.8, l2=.5, xm=.12, cut=.55, res=.1, fd=.3, dr=.1, fm=0, lr=.3, e2r=.2, mx=[('LFO', 'FM1', .15, 'WHL')])
pre('Bass Square Peg', w1=4, sh1=.5, l1=.9, cut=.48, res=.1, fd=.35, dr=.15, fm=0, kt=.6, e2d=.5, e2r=.1, vela=.5, drift=.1)
# --- Acid and pluck
pre('Bass Acid 1', w1=1, cut=.3, res=.7, fd=.7, dr=.3, fm=.8, e1d=.42, e2d=.55, e2s=.7, e2r=.12, gl=.08, leg=1, mx=[('VEL', 'ENV', .5)])
pre('Bass Acid 2', w1=1, cut=.26, res=.68, fd=.7, dr=.3, fm=.85, e1a=.5, e1d=.62, e2d=.6, e2s=.7, e2r=.14, gl=.08, leg=1, mx=[('VEL', 'E1A', .25)])
pre('Bass Grit Pluck', w1=1, l1=.9, cut=.4, res=.25, fd=.7, dr=.3, fm=.5, e1d=.45, e2d=.5, e2s=.08, e2r=.1, mx=[('FEN', 'DRV', .8), ('VEL', 'E1D', .3)])
pre('Bass Thump', w1=2, l1=.95, cut=.5, res=.05, fd=.3, dr=.3, fm=0, o1e=.6, e1d=.35, e2d=.5, e2s=.05, e2r=.12, vela=.5, drift=0)
pre('Bass Rezzy', w1=1, l1=.4, cut=.4, res=.96, fd=.55, dr=.2, fm=.2, kt=1, e2d=.6, e2s=.7, e2r=.2, mx=[('KEY', 'CUT', .3)])
pre('Bass FM Drop', w1=2, w2=2, c2=5, f2=0, l1=.85, l2=.5, xm=.12, cut=.55, res=.1, fd=.4, dr=.2, fm=0, o1e=.3, e1d=.4, e2d=.45, e2s=.05, e2r=.1, mx=[('VEL', 'FM1', .3, 'FEN')])
# --- Overdriven and industrial
pre('Bass Dirt PWM', w1=4, sh1=.4, w2=4, sh2=.3, c2=-12, f2=5, l2=.6, cut=.45, res=.25, fd=.8, dr=1, fm=0, lr=.3, shm=.8, mx=[('SY', 'CUT', .5)])
pre('Bass Saw Rip', w1=1, w2=1, f2=12, l2=.8, cut=.45, res=.35, fd=.85, dr=1, fm=.3, e1d=.4, mx=[('WHL', 'RES', .25)])
pre('Bass VCA Clip', w1=1, l1=1, l2=0, cut=.6, res=.2, fd=.95, dr=1, fm=0, e2r=.1, mx=[('WHL', 'DRV', .4)])
pre('Bass Fuzz', w1=1, nz=.2, nc=.3, cut=.42, res=.3, fd=.85, dr=.8, fm=.3, lr=.45, mx=[('LFO', 'NSE', .2)])
pre('Bass Hard Sync', w1=1, w2=1, c2=7, f2=0, cut=.45, res=.3, fd=.6, dr=.4, fm=.6, e1d=.35, e2d=.35, e2s=.12, lw=1, lr=.78, lg=1, mx=[('AT', 'LFR', .4)])
pre('Bass Growl', w1=4, sh1=.3, fmode=1, cut=.38, res=.55, fd=.7, dr=.45, fm=0, lfc=.25, lr=.5, mx=[('WHL', 'CUT', .6)])
pre('Bass Rave Stomp', w1=4, sh1=.5, w2=1, f2=8, l2=.7, cut=.45, res=.3, fd=.6, dr=.75, fm=.5, e1d=.4, e2d=.5, e2s=.6, mx=[('VEL', 'CUT', .5, 'FEN')])
# --- Modulated and SFX
pre('Bass Zap Dive', w1=4, sh1=.5, cut=.7, res=.2, fd=.6, dr=.3, fm=0, o1e=.9, e1d=.55, e2d=.6, e2s=.08, e2r=.15, mx=[('NSE', 'AMP', .4, 'AEN')])
pre('Bass Sync Drone', w1=1, w2=1, f2=12, cut=.5, res=.25, fd=.6, dr=.3, fm=.3, e2a=.2, e2d=.5, e2s=.5, e2r=.15, lw=1, lr=.62, lg=1, mx=[('AT', 'LFR', .5)])
pre('Bass Slow Mod', w1=3, w2=1, c2=-12, f2=9, cut=.5, res=.2, fd=.5, dr=.2, fm=0, e2a=.55, e2r=.7, lr=.2, o1l=.25, mx=[('WHL', 'LFR', .5)])
pre('Bass Grit Wobble', w1=1, w2=1, c2=-12, f2=8, cut=.35, res=.35, fd=.8, dr=.8, fm=0, lfc=.8, lr=.6, mx=[('SY', 'LFR', .5)])
pre('Bass Broken Tape', w1=3, cut=.45, res=.15, fd=.4, dr=.2, fm=0, lr=.15, drift=.8, mx=[('LFO', 'PCH', .08), ('NSE', 'PCH', .02)])
pre('Bass Mod Chaos', w1=1, w2=4, sh2=.4, c2=-12, f2=0, cut=.4, res=.4, fd=.7, dr=.4, fm=0, lfc=.6, lw=1, lr=.45, e2d=.6, e2s=.5, mx=[('AEN', 'LFR', .8), ('VEL', 'LFR', .3)])
