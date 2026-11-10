"""Prints every preset in real units (the time, rate and cutoff knobs are exponential): python tools/presetgen/units.py [bank-prefix]"""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dsl, arps
for m in ('bank_bass', 'bank_lead', 'bank_drone', 'bank_arp', 'bank_para', 'bank_chaos', 'bank_horror', 'bank_sets'): __import__(m)
def tms(x): return 0.0005 * 20000 ** x * 1000          # env time, ms
def lhz(x): return 0.05 * 800 ** x                       # LFO rate, Hz
def hz(oct_): return 20 * 2 ** oct_
pref = sys.argv[1] if len(sys.argv) > 1 else ''
for name, (kw, mx) in dsl.FINAL.items():
    if not name.startswith(pref): continue
    g = lambda k: kw.get(k, dsl.DEF[k])
    c3 = hz(g('cut') * 9.8138 + g('kt') * (48 - 60) / 12)      # cutoff at C3 before the Grit macro and envelopes
    pk = hz(g('cut') * 9.8138 + g('kt') * (48 - 60) / 12 + g('fm') * 5)
    lfo = 'audio' if g('ls') == 8 else ('sync' if g('ls') else '%.2fHz' % lhz(g('lr')))
    arp = ''
    if g('arp'): arp = ' arp %s/%s gate %.2f' % ('UDMRA'[g('arp') - 1] if False else g('arp'), g('arpr'), g('arpg'))
    print('%-34s A%5.0f D%5.0f S%.2f R%5.0f | fA%5.0f fD%5.0f fS%.2f | cut@C3 %5.0f..%5.0f res%.2f | lfo %-8s | glide %.2fs%s' % (
        name, tms(g('e2a')), tms(g('e2d')), g('e2s'), tms(g('e2r')), tms(g('e1a')), tms(g('e1d')), g('e1s'), c3, pk, g('res'), lfo, 2 * g('gl') ** 2, arp))
