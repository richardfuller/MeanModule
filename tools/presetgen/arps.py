import dsl
# arp mode: 1 Up, 2 Down, 3 Up+Down, 4 Random, 5 As Played; rate: 0 1/4, 1 1/8, 2 1/8T, 3 1/16, 4 1/16T, 5 1/32
def a(name, mode, rate, octs, gate): dsl.ARPS[name] = dict(arp=mode, arpr=rate, arpo=octs, arpg=gate)
a('Arp Saw Pluck', 1, 3, 2, .5);   a('Arp Rubb Tri', 3, 3, 1, .55); a('Arp Hollow', 1, 1, 2, .4);      a('Arp Soft Bounce', 2, 2, 2, .6)
a('Arp Brass Seq', 5, 1, 1, .7);   a('Arp Ghost', 4, 4, 2, .5);     a('Arp Snap Tri', 1, 3, 3, .3)
a('Arp Acid 303', 5, 3, 1, .6);    a('Arp Squelch', 3, 3, 2, .55);  a('Arp Bite Seq', 1, 3, 1, .5);     a('Arp Rez Pop', 4, 3, 2, .35)
a('Arp Liquid', 1, 4, 2, .65);     a('Arp Plastik', 2, 3, 2, .5)
a('Arp FM Bell', 1, 2, 2, .6);     a('Arp Ice Pick', 4, 3, 3, .3);  a('Arp Steel Seq', 3, 1, 1, .5);    a('Arp Tinker', 1, 3, 3, .45)
a('Arp Glass', 4, 4, 3, .35);      a('Arp Wire Pluck', 1, 3, 2, .5)
a('Arp Dark Stab', 5, 1, 1, .5);   a('Arp Drive Seq', 1, 3, 1, .5); a('Arp Nail Gun', 4, 3, 1, .3);     a('Arp Grit Bounce', 3, 3, 2, .55)
a('Arp Stutter', 5, 1, 1, .9);     a('Arp Mecha', 1, 3, 1, .5);     a('Arp Rave Stab', 5, 1, 1, .4)
a('Set Darkwave Drive 3 - Arp', 3, 3, 1, .5); a('Set Cybernetic 2 - Arp', 1, 4, 2, .5)

# Envelope floors for the Arp bank. Time knob values: decay/release 0.4 = 26 ms, 0.5 = 70 ms, 0.6 = 190 ms, 0.65 = 310 ms, 0.7 = 510 ms. The first draft used 0.4-0.5 decays and
# ~2 ms releases, so the notes were gone almost at once at any tempo; arp notes now ring for roughly a step or more (short, snappy ones a little less).
SNAPPY = {'Arp Snap Tri', 'Arp Nail Gun', 'Arp Ice Pick', 'Arp Glass', 'Arp Rez Pop'}
for _n in list(dsl.ARPS):
    dsl.FLOORS[_n] = dict(e2d=.56, e2r=.42, e2s=.1, e1d=.55) if _n in SNAPPY else dict(e2d=.64, e2r=.5, e2s=.12, e1d=.6)
