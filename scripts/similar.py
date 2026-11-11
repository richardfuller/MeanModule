"""Near-duplicate presets from out/audit60.csv (tests/preset_audit.c: octave-band shape, centroid, attack, amplitude movement of a held note).
usage: python scripts/similar.py [threshold]   prints pairs closer than the threshold (default 4.0), same-bank pairs first."""
import csv, math, os, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
rows = list(csv.DictReader(open(os.path.join(ROOT, 'out', 'audit60.csv'))))
thr = float(sys.argv[1]) if len(sys.argv) > 1 else 4.0
def vec(r): return [float(r['b%d' % i]) for i in range(10)] + [math.log2(max(float(r['centroid_hz']), 50)) * 6, float(r['amp_mod']) * 20, min(float(r['attack_ms']), 400) / 20]
V = [vec(r) for r in rows]; names = [r['name'] for r in rows]
bank = lambda n: n.split()[0]
pairs = []
for i in range(len(V)):
    for j in range(i + 1, len(V)):
        d = math.sqrt(sum((a - b) ** 2 for a, b in zip(V[i], V[j])))
        if d < thr: pairs.append((bank(names[i]) != bank(names[j]), d, names[i], names[j]))
pairs.sort()
for cross, d, a, b in pairs: print('%s %.1f  %s | %s' % ('cross-bank' if cross else 'same bank ', d, a, b))
print(len(pairs), 'pairs closer than', thr)
