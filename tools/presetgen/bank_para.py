from dsl import *

section('Para')
# --- Perfect fifths and fourths
pre('Para Fifth Stab', w1=1, w2=4, sh2=.5, c2=7, f2=9, l1=.8, l2=.7, cut=.33, res=.25, fd=.55, dr=.25, fm=.6, e1d=.4, e2d=.45, e2s=.12, e2r=.15, mx=[('VEL', 'E1D', .4)])
pre('Para Hollow Fourth', w1=2, w2=2, c2=5, f2=2, l1=.85, l2=.75, cut=.5, res=.2, fd=.4, dr=.1, fm=0, e2d=.55, e2s=.25, e2r=.2, mx=[('AEN', 'CUT', .4), ('WHL', 'DRV', .5)])
pre('Para Rave Chord', w1=1, w2=1, c2=7, f2=6, l1=.9, l2=.9, cut=.5, res=.25, fd=.7, dr=.3, fm=.3, e1d=.4, e2d=.45, e2s=.3, e2r=.15, atd=3, ata=.6)
pre('Para Fifth Drone', w1=4, sh1=.4, w2=4, sh2=.6, c2=7, f2=3, l1=.8, l2=.7, cut=.45, res=.25, fd=.5, dr=.2, fm=.5, e1a=.7, e1d=.85, e1s=.4, lr=.15, shm=.6, e2a=.3, e2r=.5, mx=[('SY', 'CUT', .5)])
pre('Para Acid Fifth', w1=1, w2=2, c2=7, f2=2, l2=.7, fmode=1, cut=.38, res=.65, fd=.65, dr=.3, fm=.7, e1d=.42, e2d=.5, e2s=.4, e2r=.12, mx=[('VEL', 'ENV', .5)])
pre('Para Pluck Fifth', w1=4, sh1=.5, w2=1, c2=7, f2=2, l1=.8, l2=.7, cut=.45, res=.2, fd=.45, dr=.2, fm=.4, e1d=.35, e2d=.4, e2s=.06, e2r=.1, mx=[('KEY', 'E2D', -.3)])
# --- Minor thirds and sixths
pre('Para Sad Pluck', w1=2, w2=4, sh2=.4, c2=3, f2=2, l1=.85, l2=.6, cut=.5, res=.2, fd=.4, dr=.1, fm=.4, e1d=.4, e2d=.45, e2s=.06, e2r=.2, lr=.3, mx=[('LFO', 'CUT', .15)])
pre('Para Minor Grit', w1=4, sh1=.5, w2=4, sh2=.5, c2=3, f2=3, l2=.8, cut=.5, res=.3, fd=.8, dr=.75, fm=.3, e1d=.4, e2d=.45, e2s=.25, e2r=.12, mx=[('SX', 'DRV', .5)])
pre('Para Weep', w1=2, w2=2, c2=3, f2=3, l2=.75, cut=.6, res=.2, fd=.4, dr=.15, fm=.2, o1e=.1, e1a=.3, e1d=.5, e2a=.1, e2d=.5, e2s=.5, e2r=.4, mx=[('VEL', 'ENV', .5)])
pre('Para Minor Acid', w1=1, w2=4, sh2=.5, c2=3, f2=2, l2=.7, cut=.33, res=.65, fd=.65, dr=.3, fm=.7, e1d=.42, e2d=.5, e2s=.4, e2r=.12, mx=[('AT', 'ENV', .5)])
# --- Dissonant and tritones
pre("Para Devil's Interval", w1=1, w2=1, c2=6, f2=3, l2=.8, cut=.4, res=.3, fd=.55, dr=.3, fm=0, lr=.12, e2a=.3, e2r=.5, mx=[('LFO', 'PCH', .03), ('WHL', 'LFR', .5)])
pre('Para Alarm', w1=4, sh1=.5, w2=2, c2=6, f2=2, l2=.7, fmode=2, cut=.45, res=.3, fd=.5, dr=.25, fm=0, e2d=.3, e2s=.3, e2r=.1, vela=.5)
pre('Para Scrape', w1=4, sh1=.5, w2=4, sh2=.5, c2=1, f2=0, l2=.8, cut=.5, res=.4, fd=.6, dr=.3, fm=0, lfc=.3, ls=8, lw=1, lr=.5, e2a=.1, mx=[('AT', 'LFR', .4)])
pre('Para Cluster', w1=1, w2=1, c2=1, f2=0, l2=.85, cut=.4, res=.25, fd=.8, dr=1, fm=.3, e1d=.4, e2d=.45, e2s=.3, e2r=.1, mx=[('SY', 'CUT', .5)])
pre('Para Tritone Drop', w1=4, sh1=.5, w2=2, c2=6, f2=0, l2=.7, cut=.65, res=.2, fd=.5, dr=.2, fm=0, o1e=.5, o2e=.5, e1d=.5, e2d=.5, e2s=.1, e2r=.15, mx=[('VEL', 'E1D', .4)])
pre('Para Grit Chime', w1=2, w2=2, c2=6, f2=0, l2=.7, xm=.25, cut=.8, res=.15, fd=.6, dr=.3, fm=0, e2d=.6, e2s=.2, e2r=.4, mx=[('WHL', 'FM1', .4)])
pre('Para Anxiety', w1=4, sh1=.5, w2=1, c2=6, f2=0, l2=.7, cut=.55, res=.3, fd=.5, dr=.25, fm=0, lw=1, lr=.7, e2a=.1, mx=[('LFO', 'AMP', .5), ('AT', 'LFR', .5)])
# --- Detuned unisons and octaves
pre('Para Massive Saw', w1=1, w2=1, f2=25, l2=.9, cut=.4, res=.25, fd=.6, dr=.3, fm=.6, e1d=.45, e2d=.5, e2s=.3, e2r=.2, mx=[('VEL', 'ENV', .5)])
pre('Para Sub Octave', w1=1, w2=4, sh2=.5, c2=-12, f2=0, l2=.85, cut=.45, res=.2, fd=.6, dr=.5, fm=.3, e1d=.4, e2d=.45, e2s=.4, e2r=.12, mx=[('WHL', 'DRV', .5)])
pre('Para Phase Pulse', w1=4, sh1=.4, w2=4, sh2=.6, f2=9, l2=.8, cut=.6, res=.2, fd=.4, dr=.15, fm=0, lr=.2, e2a=.25, e2r=.5, mx=[('LFO', 'SH1', .6), ('SX', 'SH2', .5)])
pre('Para Octave Pluck', w1=2, w2=2, c2=12, f2=2, l2=.7, cut=.5, res=.2, fd=.4, dr=.1, fm=.4, e1d=.4, e2d=.45, e2s=.06, e2r=.12, velc=.6)
pre('Para Sync Unison', w1=4, sh1=.5, w2=4, sh2=.5, f2=18, l2=.8, cut=.55, res=.3, fd=.6, dr=.3, fm=.2, e2a=.05, e2d=.35, e2s=.35, e2r=.1, lw=1, lr=.75, lg=1, mx=[('WHL', 'LFR', .4)])
pre('Para Thick Acid', w1=1, w2=1, f2=14, l2=.85, cut=.3, res=.7, fd=.7, dr=.3, fm=.8, e1d=.42, e2d=.5, e2s=.55, e2r=.1, mx=[('VEL', 'ENV', .5)])
