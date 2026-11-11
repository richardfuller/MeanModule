from dsl import *

section('Horror')
# --- Haunted keys. Calliope per the brief (two pulses an octave apart, LFO on both pitches, Mod Wheel on the LFO rate = failing motor), plus the traits asked for in chat:
# steam-temperature pitch wander (Random + Smooth Random) and a tied 1/32 arp so a held chord cycles like several pipes.
pre('Horror Calliope', w1=4, sh1=.35, w2=4, sh2=.65, c2=12, f2=-34, l1=.8, l2=.75, nz=.04, nc=.2, cut=.58, res=.25, fd=.55, dr=.3, fm=.6, e1d=.5, e2d=.55, e2s=.9, e2r=.1,
    drift=1, lr=.3, arp=1, arpr=5, arpo=1, arpg=1, mx=[('LFO', 'PCH', .05), ('WHL', 'LFR', .6), ('RND', 'PCH', .04), ('SMR', 'PC2', .03)])
pre('Horror Music Box', w1=2, c1=24, l1=.9, cut=.85, res=.1, fd=.3, dr=.1, fm=0, lr=.2, drift=.9, e2d=.5, e2s=.05, e2r=.45, mx=[('LFO', 'PCH', .06), ('KEY', 'LD1', 1)])
pre('Horror Ghost Organ', w1=4, sh1=.4, w2=2, f2=9, l2=.8, fmode=1, cut=.5, res=.35, fd=.4, dr=.15, fm=0, e2a=.1, e2r=.3, atd=0, l2r=.2, mx=[('NSE', 'FM1', .03), ('AT', 'NSE', .4), ('LF2', 'SH1', .6), ('SX', 'CUT', .4)])
pre('Horror Broken Toy', w1=4, sh1=.5, cut=.65, res=.3, fd=.5, dr=.3, fm=.2, o1e=.35, e1d=.3, e2d=.35, e2s=.4, lw=1, lr=.7, lg=1, mx=[('SX', 'LFR', .5)])
pre('Horror Dusty Piano', w1=1, cut=.45, res=.15, fd=.5, dr=.2, fm=.3, e1d=.4, e2d=.6, e2s=.2, e2r=.35, lw=4, lr=.45, lfc=.3, drift=.7)
pre('Horror Cursed Bell', w1=2, w2=2, c2=6, f2=0, l2=.7, xm=.2, cut=.8, res=.15, fd=.4, dr=.15, fm=0, e2d=.75, e2s=.1, e2r=.6, mx=[('VEL', 'XMD', .4)])
# --- Cinematic tension
pre('Horror Anxiety Pulse', w1=1, cut=.4, res=.3, fd=.5, dr=.25, fm=0, lw=1, lr=.66, e2a=.02, e2r=.2, mx=[('LFO', 'AMP', .45), ('WHL', 'LFR', .5)])
pre('Horror Saw Blade', w1=1, cut=.5, res=.8, fd=.7, dr=.5, fm=0, lfc=.35, ls=8, lw=1, lr=.45, e2a=.05, e2r=.2, mx=[('AT', 'CUT', .5)])
pre('Horror Creeping', w1=2, w2=1, c2=1, f2=0, l2=.7, cut=.4, res=.25, fd=.5, dr=.2, fm=0, e2a=.85, e2r=.6, mx=[('SY', 'DRV', .6)])
pre('Horror The Hive', w1=4, sh1=.03, w2=4, sh2=.04, f2=15, cut=.75, res=.3, fd=.5, dr=.25, fm=0, lr=.7, shm=.8, e2a=.2, e2r=.4, mx=[('WHL', 'LFR', .5)])
pre('Horror Dark Water', w1=2, l1=.9, cut=.3, res=.6, fd=.5, dr=.2, fm=0, lfc=.6, lr=.35, e2a=.3, e2r=.5, mx=[('KEY', 'RES', .3)])
pre('Horror Pendulum', w1=1, fmode=1, cut=.45, res=.55, fd=.5, dr=.25, fm=0, lfc=.7, lr=.1, e2a=.3, e2r=.5, atd=3, ata=.6)
# --- Jumpscares and stings
pre('Horror Scream Hit', w1=1, l1=.5, fmode=2, cut=.45, res=.9, fd=.6, dr=.4, fm=.6, e1d=.5, e2d=.5, e2s=.1, e2r=.2, mx=[('VEL', 'ENV', .5)])
pre('Horror Door Slam', w1=4, sh1=.5, w2=4, sh2=.5, c2=-12, f2=0, l2=.8, cut=.4, res=.2, fd=.9, dr=1, fm=0, e2d=.55, e2s=.2, e2r=.3, mx=[('WHL', 'DRV', .5)])
pre('Horror Nerve Zap', l1=0, nz=.8, nc=0, fmode=2, cut=.5, res=.3, fd=.5, dr=.4, fm=0, lw=1, lr=.85, e2d=.3, e2s=.3, e2r=.1, mx=[('LFO', 'AMP', .9), ('AT', 'NSE', .3)])
pre('Horror Psycho', w1=1, w2=1, c2=6, f2=0, l2=.8, oct=1, cut=.5, res=.3, fd=.6, dr=.4, fm=.6, e1d=.4, e2d=.5, e2s=.15, e2r=.2, velc=.8)
pre('Horror Glass Break', l1=0, nz=.8, nc=0, fmode=2, cut=.7, res=.3, fd=.4, dr=.2, fm=.5, e1d=.35, e2d=.45, e2s=.15, e2r=.2, mx=[('KEY', 'RES', .4)])
pre('Horror Sudden Drop', w1=2, l1=1, cut=.6, res=.15, fd=.8, dr=.5, fm=0, o1e=1, e1d=.6, e2d=.6, e2s=.1, e2r=.3, mx=[('VEL', 'E1D', .4)])
# --- Ghostly ambiences
pre('Horror EVP', l1=0, nz=.7, nc=.3, fmode=1, cut=.5, res=.5, fd=.3, dr=.1, fm=0, lfc=.5, lr=.25, e2a=.3, e2r=.5, mx=[('WHL', 'RES', .3)])
pre('Horror Banshee', l1=0, nz=.3, nc=.2, cut=.55, res=.9, fd=.4, dr=.15, fm=0, lfc=.3, lr=.3, e2a=.3, e2r=.6, mx=[('NSE', 'CUT', .15), ('SY', 'CUT', .5)])
pre('Horror Cold Breath', w1=2, l1=.8, nz=.05, fmode=2, cut=.45, res=.3, fd=.3, dr=.1, fm=0, lfc=.5, lr=.2, e2a=.4, e2r=.5, mx=[('AT', 'NSE', .5)])
pre('Horror Apparition', w1=4, sh1=.4, w2=4, sh2=.6, f2=9, l2=.8, cut=.7, res=.2, fd=.4, dr=.15, fm=0, lr=.25, shm=.6, e2a=.3, e2r=.5, mx=[('SX', 'SH2', .5)])
pre('Horror Crypt', w1=2, l1=.7, cut=.25, res=.85, fd=.5, dr=.2, fm=0, e2a=.6, e2r=.7, mx=[('WHL', 'CUT', .5)])
pre('Horror Spirit Box', w1=4, sh1=.5, nz=.3, nc=.2, fmode=1, cut=.5, res=.5, fd=.4, dr=.2, fm=0, lfc=.5, lw=4, lr=.75, e2a=.05, e2r=.2, mx=[('NSE', 'FM1', .15)])
pre('Horror Chant', w1=4, sh1=.35, w2=1, c2=-12, f2=11, l2=.5, cut=.4, res=.3, fd=.5, dr=.25, fm=.4, e1a=.6, e1d=.8, e1s=.4, lr=.2, shm=.6, e2a=.35, e2r=.5, atd=3, ata=.5)
