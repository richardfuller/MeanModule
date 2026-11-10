from dsl import *

section('Lead')
# --- Expressive and glide
pre('Lead Glide Tri', w1=2, l1=.95, cut=.6, res=.15, fd=.3, dr=.15, fm=.2, e1d=.5, gl=.35, leg=1, pri=0, lr=.65, mx=[('LFO', 'PCH', .03), ('SX', 'GLD', .6)])
pre('Lead Solo Saw', w1=1, w2=1, f2=12, cut=.62, res=.25, fd=.55, dr=.25, fm=.35, e1d=.5, e2s=.9, lr=.62, gl=.1, leg=1, mx=[('LFO', 'PCH', .05, 'SY'), ('WHL', 'LFR', .3)])
pre('Lead Flute Mod', w1=2, l1=.9, nz=.18, nc=.45, cut=.55, res=.12, fd=.3, dr=.1, fm=.25, e1d=.4, e2a=.1, e2s=.9, velc=.8, vela=.3)
pre('Lead Theremin', w1=2, l1=.9, fmode=2, cut=.16, res=.1, fd=.3, dr=.1, fm=0, e2a=.15, e2s=1, e2r=.3, gl=.6, leg=1, lr=.65, vib=0, mx=[('LFO', 'PCH', .025, 'AT')])
pre('Lead Vow', w1=4, sh1=.35, fmode=1, cut=.45, res=.55, fd=.5, dr=.2, fm=.2, e1d=.5, e2s=.9, gl=.08, leg=1, mx=[('WHL', 'CUT', .4)])
pre('Lead Soft Sync', w1=2, w2=4, sh2=.5, c2=0, f2=5, l2=.55, cut=.65, res=.15, fd=.35, dr=.15, fm=.2, e2s=.9, lr=.3, mx=[('LFO', 'SH2', .6), ('KEY', 'AMP', .2)])
# --- Hard sync and tearing
pre('Lead Tear Saw', w1=1, w2=1, f2=11, cut=.65, res=.3, fd=.6, dr=.3, fm=.3, o1e=.25, e1d=.55, e2s=.9, mx=[('VEL', 'ENV', .5)])
pre('Lead Sync Rip', w1=4, sh1=.5, cut=.62, res=.3, fd=.55, dr=.3, fm=.2, e2a=.05, e2d=.35, e2s=.35, lw=1, lr=.8, lg=1, mx=[('WHL', 'LFR', .3)])
pre('Lead Laser Bite', w1=4, sh1=.4, cut=.7, res=.25, fd=.5, dr=.25, fm=.2, o1e=.7, e1d=.5, e2d=.45, e2s=.3, mx=[('VEL', 'E2D', .5)])
pre('Lead Buzz', w1=4, sh1=.03, cut=.7, res=.3, fd=.6, dr=.25, fm=.2, e2s=.9, lr=.75, mx=[('LFO', 'CUT', .4, 'WHL')])
pre('Lead Acid', w1=1, cut=.38, res=.8, fd=.7, dr=.3, fm=.8, e1d=.5, e2d=.55, e2s=.8, gl=.06, leg=1, mx=[('VEL', 'ENV', .5)])
pre('Lead Pulse Cut', w1=4, sh1=.3, w2=4, sh2=.7, f2=9, cut=.75, res=.2, fd=.5, dr=.25, fm=.15, e2s=.9, lr=.3, atd=3, ata=.5, mx=[('LFO', 'SH1', .6)])
# --- FM and metallic
pre('Lead Metal Solo', w1=2, w2=2, c2=7, f2=0, l1=.8, l2=.75, xm=.3, cut=.82, res=.15, fd=.4, dr=.15, fm=0, e2s=.9, mx=[('WHL', 'FM1', .4)])
pre('Lead Ice FM', w1=2, l1=.9, cut=.85, res=.15, fd=.4, dr=.15, fm=0, e2s=.9, ls=8, lw=1, lr=.55, o1l=.25, mx=[('SY', 'LFR', .3)])
pre('Lead Steel Zap', w1=1, w2=2, c2=7, f2=0, l2=.55, xm=.2, cut=.7, res=.2, fd=.5, dr=.2, fm=.2, o1e=.4, e1d=.45, e2d=.5, e2s=.4, mx=[('VEL', 'FM1', .3)])
pre('Lead Glass Cut', w1=4, sh1=.5, w2=2, c2=12, l2=0, fmode=2, cut=.55, res=.35, fd=.5, dr=.2, fm=0, e2s=.9, mx=[('OS2', 'CUT', .15), ('AT', 'CUT', .4)])
pre('Lead Grit Bell', w1=2, w2=2, c2=3, f2=0, l2=.8, cut=.8, res=.15, fd=.6, dr=.3, fm=0, e2d=.65, e2s=.3, e2r=.5, lr=.3, mx=[('LFO', 'PC2', .03), ('WHL', 'DRV', .5)])
pre('Lead Wire FM', w1=4, sh1=.5, w2=2, c2=12, l2=0, cut=.8, res=.2, fd=.4, dr=.15, fm=0, e2s=.9, lr=.5, mx=[('OS2', 'SH1', .7), ('LFO', 'PC2', .3, 'SX')])
pre('Lead Alloy', w1=2, w2=3, c2=7, f2=0, l2=.7, cut=.78, res=.2, fd=.45, dr=.2, fm=0, o1l=.2, o2e=.3, e1d=.6, e2s=.9, lr=.25, mx=[('WHL', 'LFR', .4)])
# --- Distorted and feedback
pre('Lead Scream', w1=1, l1=.45, cut=.45, res=.95, fd=.6, dr=.6, fm=0, kt=1, e2s=.9, mx=[('KEY', 'CUT', .5)])
pre('Lead Howl', w1=1, cut=.4, res=.4, fd=.8, dr=1, fm=.5, e1d=.6, e2s=.9, atd=3, ata=.7)
pre('Lead Fuzz', w1=4, sh1=.5, nz=.4, nc=.2, cut=.6, res=.3, fd=.9, dr=.8, fm=.2, e2s=.9, mx=[('WHL', 'NSE', .5)])
pre('Lead Burn Saw', w1=1, w2=1, f2=14, cut=.6, res=.3, fd=.8, dr=.8, fm=.2, e2s=.9, lr=.2, mx=[('LFO', 'PCH', .1, 'VEL')])
pre('Lead Squeal', w1=1, l1=.5, fmode=1, cut=.55, res=.9, fd=.6, dr=.3, fm=0, e2s=.9, mx=[('AEN', 'CUT', .5), ('WHL', 'RES', .1)])
pre('Lead Broken Pin', w1=4, sh1=.5, cut=.65, res=.3, fd=.6, dr=.4, fm=.2, e2d=.35, e2s=.4, lw=1, lr=.8, lg=1, mx=[('NSE', 'FM1', .06), ('SY', 'LFR', .6)])
