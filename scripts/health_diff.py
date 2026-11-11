"""Compares out/health.csv (from scripts/health.sh) with the committed snapshot tests/golden/health.csv and prints what moved, so an unintended change to a preset shows up.
usage: python scripts/health_diff.py [--update]   (--update accepts the current numbers as the new snapshot)"""
import csv, os, shutil, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
CUR, GOLD = os.path.join(ROOT, 'out', 'health.csv'), os.path.join(ROOT, 'tests', 'golden', 'health.csv')
if '--update' in sys.argv:
    shutil.copy(CUR, GOLD); print('snapshot updated:', GOLD); sys.exit(0)
if not os.path.exists(GOLD):
    print('no snapshot yet; run with --update to create tests/golden/health.csv'); sys.exit(0)
rd = lambda p: {r['name']: r for r in csv.DictReader(open(p))}
cur, gold = rd(CUR), rd(GOLD)
# column -> allowed change
tol = {'clip_pct': 1.0, 'crest_db': 1.5, 'rel_ms': None, 'tail_s': None, 'tempo_ratio': 0.2, 'wheel_db': 1.5, 'at_db': 1.5, 'stx_db': 1.5, 'sty_db': 1.5, 'cpu_avg_us': None}
def moved(col, a, b):
    a, b = float(a), float(b)
    if col in ('rel_ms', 'tail_s'): return abs(a - b) > max(0.3 if col == 'tail_s' else 10, 0.3 * abs(b))
    if col == 'cpu_avg_us': return abs(a - b) > max(8, 0.4 * abs(b))
    return abs(a - b) > tol[col]
n = 0
for name in sorted(set(cur) | set(gold)):
    if name not in gold: print('NEW      ', name); n += 1; continue
    if name not in cur: print('REMOVED  ', name); n += 1; continue
    ch = [f'{c} {gold[name][c]} -> {cur[name][c]}' for c in tol if moved(c, cur[name][c], gold[name][c])]
    fa, fb = set(gold[name]['flags'].split()), set(cur[name]['flags'].split())
    if fa != fb: ch.append('flags %s -> %s' % (sorted(fa), sorted(fb)))
    if ch: print('CHANGED  ', name, '|', '; '.join(ch)); n += 1
print(n, 'presets differ from the snapshot' if n else 'no differences from the snapshot')
