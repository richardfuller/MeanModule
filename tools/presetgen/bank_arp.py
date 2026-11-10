from dsl import *

section('Arp')
# --- Classic plucks
pre('Arp Saw Pluck', w1=1, cut=.38, res=.25, fd=.5, dr=.2, fm=.6, e1d=.35, e2d=.4, e2s=.06, e2r=.12, mx=[('VEL', 'ENV', .5)])
pre('Arp Rubb Tri', w1=2, w2=2, f2=6, l1=.85, l2=.7, cut=.4, res=.2, fd=.4, dr=.1, fm=.5, e1d=.4, e2d=.42, e2s=.06, e2r=.12, mx=[('KEY', 'E1D', -.3)])
pre('Arp Hollow', w1=4, sh1=.1, cut=.55, res=.2, fd=.4, dr=.1, fm=.4, e1d=.35, e2d=.4, e2s=.05, e2r=.1, vela=.5)
pre('Arp Soft Bounce', w1=2, l1=.9, fmode=2, cut=.28, res=.15, fd=.3, dr=.1, fm=.15, e1d=.3, e2d=.4, e2s=.05, e2r=.12, lr=.3, mx=[('LFO', 'CUT', .1)])
pre('Arp Brass Seq', w1=1, w2=1, f2=11, cut=.35, res=.2, fd=.55, dr=.3, fm=.5, e1a=.12, e1d=.45, e2a=.05, e2d=.45, e2s=.15, e2r=.12, velc=.7)
pre('Arp Ghost', w1=2, l1=.9, nz=.18, nc=.35, cut=.55, res=.15, fd=.3, dr=.1, fm=.3, e1d=.35, e2d=.45, e2s=.05, e2r=.15, atd=0, mx=[('AT', 'NSE', .4)])
pre('Arp Snap Tri', w1=2, l1=.95, cut=.6, res=.15, fd=.4, dr=.15, fm=.3, o1e=.5, e1d=.12, e2d=.4, e2s=.06, e2r=.1, mx=[('WHL', 'E1D', .4)])
# --- Acid sequences
pre('Arp Acid 303', w1=1, cut=.3, res=.72, fd=.7, dr=.3, fm=.85, e1d=.42, e2d=.5, e2s=.5, e2r=.1, gl=.06, leg=1, mx=[('VEL', 'ENV', .5)])
pre('Arp Squelch', w1=4, sh1=.35, fmode=1, cut=.4, res=.75, fd=.6, dr=.3, fm=.6, e1d=.4, e2d=.5, e2s=.45, e2r=.1, mx=[('SY', 'RES', .15)])
pre('Arp Bite Seq', w1=1, w2=4, sh2=.5, f2=10, cut=.4, res=.4, fd=.7, dr=.5, fm=.6, e1d=.38, e2d=.45, e2s=.3, e2r=.1, mx=[('VEL', 'DRV', .5)])
pre('Arp Rez Pop', w1=1, l1=.3, cut=.35, res=.95, fd=.5, dr=.2, fm=.7, e1d=.4, e2d=.45, e2s=.1, e2r=.1, mx=[('VEL', 'E1D', .4)])
pre('Arp Liquid', w1=4, sh1=.3, cut=.35, res=.45, fd=.5, dr=.2, fm=.7, e1d=.4, e2d=.5, e2s=.2, e2r=.15, lr=.5, mx=[('LFO', 'CUT', .3, 'WHL')])
pre('Arp Plastik', w1=4, sh1=.5, fmode=2, cut=.42, res=.45, fd=.5, dr=.2, fm=.3, e1d=.35, e2d=.45, e2s=.06, e2r=.1, mx=[('WHL', 'SH1', .5)])
# --- FM and metallic arps
pre('Arp FM Bell', w1=2, w2=2, c2=14, f2=0, l1=.8, l2=.6, xm=.3, cut=.85, res=.1, fd=.4, dr=.1, fm=0, e2d=.6, e2s=.05, e2r=.5, mx=[('VEL', 'XMD', .4)])
pre('Arp Ice Pick', w1=2, l1=.9, cut=.85, res=.15, fd=.4, dr=.15, fm=0, e2d=.35, e2s=.05, e2r=.15, ls=8, lw=1, lr=.6, o1l=.2, mx=[('WHL', 'LFR', .5)])
pre('Arp Steel Seq', w1=4, sh1=.5, w2=2, c2=7, f2=0, l2=.5, cut=.55, res=.2, fd=.5, dr=.2, fm=0, o1e=.25, e1d=.35, e2d=.4, e2s=.06, e2r=.12, velc=.7)
pre('Arp Tinker', w1=2, c1=24, l1=.9, cut=.8, res=.1, fd=.3, dr=.1, fm=0, lr=.2, e2d=.45, e2s=.05, e2r=.3, mx=[('LFO', 'PCH', .015)])
pre('Arp Glass', w1=4, sh1=.5, fmode=2, cut=.4, res=.35, fd=.4, dr=.15, fm=.8, e1d=.3, e2d=.4, e2s=.05, e2r=.15, mx=[('KEY', 'RES', .3)])
pre('Arp Wire Pluck', w1=4, sh1=.5, w2=2, c2=12, l2=0, cut=.7, res=.2, fd=.4, dr=.15, fm=.3, e1d=.35, e2d=.4, e2s=.06, e2r=.12, mx=[('OS2', 'SH1', .4, 'VEL')])
# --- Dark and industrial sequences
pre('Arp Dark Stab', w1=1, w2=1, c2=3, f2=0, cut=.35, res=.3, fd=.6, dr=.3, fm=.5, e1d=.4, e2d=.45, e2s=.15, e2r=.15, mx=[('WHL', 'CUT', .4)])
pre('Arp Drive Seq', w1=1, cut=.5, res=.3, fd=.7, dr=1, fm=.5, e1d=.35, e2d=.4, e2s=.15, e2r=.08, mx=[('VEL', 'DRV', .5), ('FEN', 'AMP', .3)])
pre('Arp Nail Gun', w1=4, sh1=.5, nz=.3, nc=.1, fmode=1, cut=.6, res=.3, fd=.6, dr=.4, fm=0, o1e=.8, e1d=.2, e2d=.3, e2s=.05, e2r=.08, mx=[('VEL', 'NSE', .6)])
pre('Arp Grit Bounce', w1=4, sh1=.5, cut=.5, res=.3, fd=.6, dr=.5, fm=.4, e1d=.38, e2d=.42, e2s=.12, e2r=.1, lr=.55, shm=.7, atd=3, ata=.5)
pre('Arp Stutter', w1=1, cut=.5, res=.3, fd=.5, dr=.3, fm=.5, e1d=.35, e2d=.3, e2s=.4, e2r=.1, lw=1, lr=.75, lg=1, mx=[('SX', 'LFR', .5)])
pre('Arp Mecha', w1=4, sh1=.5, w2=4, sh2=.2, f2=0, fmode=1, cut=.5, res=.4, fd=.6, dr=.3, fm=0, lfc=.4, lr=.6, e2d=.4, e2s=.2, e2r=.1)
pre('Arp Rave Stab', w1=1, w2=4, sh2=.5, c2=7, f2=0, cut=.46, res=.3, fd=.6, dr=.6, fm=.7, e1d=.3, e2d=.32, e2s=.1, e2r=.12, mx=[('VEL', 'E1D', .4)])
