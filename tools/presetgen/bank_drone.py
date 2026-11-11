from dsl import *

section('Drone')
# --- Sub-harmonic
pre('Drone Deep Void', w1=2, l1=.95, cut=.3, res=.3, fd=.4, dr=.15, fm=0, lfc=.6, lr=.15, e2a=.5, e2r=.6, mx=[('WHL', 'LFR', .5)])
pre('Drone Tectonic', w1=1, w2=1, f2=28, l2=.8, oct=-1, cut=.35, res=.2, fd=.6, dr=.4, fm=0, lr=.12, shm=.6, e2a=.45, e2r=.55, atd=3, ata=.6)
pre('Drone Abyss', w1=2, l1=.95, cut=.25, res=.2, fd=.35, dr=.1, fm=.6, e1a=.7, e1d=.85, e1s=.3, e2a=.5, e2r=.6, drift=.5, mx=[('SX', 'PC1', .02)])
pre('Drone Dark Wash', w1=4, sh1=.4, cut=.4, res=.25, fd=.5, dr=.2, fm=0, e2a=.6, e2d=.85, e2s=.8, e2r=.7, mx=[('AEN', 'CUT', .5), ('VEL', 'E2A', .5)])
pre('Drone Hollow Earth', w1=2, l1=.9, fmode=2, cut=.2, res=.8, fd=.4, dr=.15, fm=0, kt=.3, e2a=.4, e2r=.6, mx=[('WHL', 'CUT', .5)])
pre('Drone Ground Swell', w1=2, w2=2, f2=5, l2=.8, cut=.4, res=.15, fd=.4, dr=.15, fm=.5, e1a=.8, e1d=.9, e1s=.4, e1r=.7, lr=.1, e2a=.5, e2r=.6, mx=[('LFO', 'AMP', .5), ('SY', 'DRV', .6)])
# --- Noise and windscapes
pre('Drone Cold Wind', l1=0, nz=.7, nc=.5, fmode=1, cut=.55, res=.5, fd=.3, dr=.1, fm=0, kt=.3, lr=.13, e2a=.7, e2r=.7, mx=[('LFO', 'CUT', .35)])
pre('Drone Ruins', w1=2, l1=.8, nz=.2, nc=.2, cut=.7, res=.2, fd=.5, dr=.3, fm=0, e2a=.3, e2r=.6, mx=[('NSE', 'FM1', .15, 'WHL')])
pre('Drone Dust Storm', w1=1, l1=.6, cut=.5, res=.9, fd=.6, dr=.5, fm=0, e2a=.3, e2r=.6, atd=0, mx=[('NSE', 'CUT', .12), ('AT', 'NSE', 1)])
pre('Drone Static Sea', l1=0, nz=.9, cut=.64, res=.3, fd=.3, dr=.1, fm=0, lr=.2, e2a=.4, e2r=.6, mx=[('LFO', 'CUT', .4), ('SY', 'LFR', .5)])
pre('Drone Ghost Breath', l1=0, nz=.1, nc=.3, fmode=1, cut=.55, res=.45, fd=.3, dr=.1, fm=.7, e1a=.6, e1d=.7, e1s=.3, e1r=.6, e2a=.5, e2r=.7, kt=.5, mx=[('FEN', 'NSE', .8), ('VEL', 'ENV', .5)])
pre('Drone Vinyl Crack', l1=0, nz=.5, fmode=2, cut=.5, res=.2, fd=.3, dr=.1, fm=0, lw=4, lr=.85, e2a=.1, e2r=.4, mx=[('LFO', 'AMP', .9)])
# --- Complex FM textures
pre('Drone Iron Scrape', w1=2, w2=2, c2=7, f2=0, l2=.5, xm=.3, cut=.8, res=.2, fd=.5, dr=.25, fm=0, lr=.12, e2a=.35, e2r=.5, mx=[('LFO', 'PC2', .3), ('WHL', 'XMD', .5)])
pre('Drone Reactor', w1=4, sh1=.5, cut=.6, res=.3, fd=.5, dr=.3, fm=0, ls=8, lw=1, lr=.4, o1l=.15, e2a=.3, e2r=.5, mx=[('SX', 'LFR', .3)])
pre('Drone Bell Toll', w1=2, w2=2, c2=6, f2=0, l2=.7, xm=.2, cut=.8, res=.15, fd=.4, dr=.15, fm=0, e2d=.85, e2s=.2, e2r=.85, mx=[('AEN', 'XMD', .5)])
pre('Drone Wire', w1=4, sh1=.06, cut=.75, res=.25, fd=.5, dr=.2, fm=0, lr=.22, shm=.8, e2a=.3, e2r=.5, mx=[('WHL', 'SH1', .6)])
pre('Drone Stasis', w1=2, w2=1, c2=7, f2=3, l2=.55, cut=.5, res=.3, fd=.4, dr=.15, fm=0, lfc=.5, lr=.2, l2r=.15, e2a=.5, e2r=.7, atd=3, ata=.6, mx=[('LF2', 'SH1', .5)])
pre('Drone Radiation', w1=1, l1=.3, cut=.5, res=.92, fd=.5, dr=.2, fm=0, lfc=.7, lw=1, lr=.6, e2a=.2, e2r=.4, mx=[('SY', 'CUT', .5)])
pre('Drone Glass Fog', w1=2, w2=2, c2=19, f2=0, l2=.5, xm=.25, fmode=2, cut=.6, res=.4, fd=.4, dr=.15, fm=0, e2a=.6, e2r=.7, l2r=.18, mx=[('WHL', 'RES', .3), ('LF2', 'SH1', .5)])
# --- Rhythmic and pulsing
pre('Drone Heartbeat', w1=2, l1=.95, cut=.35, res=.2, fd=.5, dr=.3, fm=0, lw=2, lr=.52, e2a=.05, e2r=.3, mx=[('LFO', 'AMP', .5), ('WHL', 'LFR', .4)])
pre('Drone Machinery', w1=4, sh1=.5, w2=4, sh2=.4, f2=0, c2=-12, cut=.55, res=.3, fd=.6, dr=.4, fm=0, lr=.6, o1l=.25, e2a=.1, atd=3, ata=.6)
pre('Drone Warning', w1=1, fmode=1, cut=.5, res=.5, fd=.5, dr=.25, fm=0, lfc=.6, lr=.45, e2a=.1, mx=[('SX', 'RES', .3)])
pre('Drone Breather', w1=1, w2=1, f2=10, cut=.3, res=.3, fd=.5, dr=.5, fm=0, lfc=.5, lr=.25, e2a=.3, e2r=.5, mx=[('SY', 'LFR', .5)])
pre('Drone Engine Room', w1=4, sh1=.4, w2=4, sh2=.6, f2=0, c2=-12, cut=.35, res=.3, fd=.6, dr=.4, fm=0, lw=1, lr=.7, e2a=.05, mx=[('LFO', 'PCH', .01), ('WHL', 'CUT', .5)])
pre('Drone Radar', w1=1, cut=.3, res=.6, fd=.5, dr=.2, fm=.9, e1d=.4, e2d=.55, e2s=.5, e2r=.3, lw=1, lr=.4, lg=1, mx=[('AT', 'LFR', .4)])
