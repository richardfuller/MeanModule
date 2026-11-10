import sys, re, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dsl
import arps
for m in ('bank_bass', 'bank_lead', 'bank_drone', 'bank_arp', 'bank_para', 'bank_chaos', 'bank_horror', 'bank_sets', 'bank_keys'):
    __import__(m)

R = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
p = os.path.join(R, 'src', 'dsp', 'mean_module_plugin.c')
s = open(p, newline='', encoding='utf-8').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')

rows = list(dsl.ROWS)

start = s.index('    /* ---- Init ---- */')
rows = ['    /* ---- Init ---- */', '    PRE("Init", {P_VOLUME, 0.7f}),'] + rows
arr = s.index('static const preset_t PRESETS[] = {')
end = s.index('\n};\n', arr)
s = s[:start] + '\n'.join(rows) + s[end:]
open(p, 'w', newline='', encoding='utf-8').write(s.replace('\n', nl))
print(len(dsl.NAMES), 'new presets; total rows', len([r for r in rows if r.lstrip().startswith('PRE(')]))
