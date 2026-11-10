"""Writes targets.csv: the spectral centroid (Hz, one held C3) each preset should at least reach. The cutoff knob is exponential, so these are measured, not guessed.
usage: python tools/presetgen/targets.py > targets.csv"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dsl, arps
for m in ('bank_bass', 'bank_lead', 'bank_drone', 'bank_arp', 'bank_para', 'bank_chaos', 'bank_horror', 'bank_sets'): __import__(m)
# Only the dullest presets are lifted (the plain default saw patch measures 225 Hz, so ~200-380 is normal and nothing is forced wide open). Scale: the power-weighted centroid of one held C3 is dominated by the fundamental (131 Hz). The plain default saw patch ("Init") measures 225 Hz; 400+ is clearly bright, 600+ very bright.
BANK = dict(Bass=230, Lead=330, Drone=200, Arp=380, Para=300, Chaos=300, Horror=280)
ROLE = dict(Bass=230, Sub=150, Lead=330, Stab=320, Arp=380, Drone=200, Pad=260, Texture=280, SFX=300, Pluck=360, Chime=360, Sweep=200)
DARK = {  # dark on purpose
 'Bass Sub 1': 150, 'Bass Sub 2': 150, 'Bass Log Tri': 170, 'Bass Ghost Sub': 130, 'Bass Deep FM': 220, 'Bass Square Peg': 230, 'Bass Thump': 130, 'Bass Rezzy': 200,
 'Bass Growl': 300, 'Bass Slow Mod': 220, 'Bass Broken Tape': 220, 'Bass Grit Wobble': 230,
 'Lead Glide Tri': 220, 'Lead Theremin': 200, 'Lead Flute Mod': 260, 'Lead Vow': 350, 'Lead Scream': 300,
 'Drone Deep Void': 120, 'Drone Tectonic': 160, 'Drone Abyss': 120, 'Drone Dark Wash': 200, 'Drone Hollow Earth': 100, 'Drone Ground Swell': 160, 'Drone Cold Wind': 300,
 'Drone Dust Storm': 300, 'Drone Heartbeat': 150, 'Drone Breather': 160, 'Drone Engine Room': 160,
 'Para Sub Octave': 200, 'Chaos Mothership': 120,
 'Horror Crypt': 100, 'Horror Creeping': 180, 'Horror Dusty Piano': 180, 'Horror Dark Water': 160, 'Horror Door Slam': 250, 'Horror Chant': 220, 'Horror Pendulum': 300,
 'Horror Ghost Organ': 260, 'Horror Music Box': 500, 'Horror Cursed Bell': 450, 'Horror Sudden Drop': 220,
}
for name in dsl.FINAL:
    if name.startswith('Init'): continue
    if name in DARK: t = DARK[name]
    elif name.startswith('Set '): t = ROLE.get(name.split(' - ')[-1], 300)
    else: t = BANK[name.split()[0]]
    print('%s,%d' % (name, t))
