# -*- coding: utf-8 -*-
import re

with open('kernel/shell_clean.c', 'r', encoding='utf-16') as f:
    clean = f.read()
with open('kernel/shell.c', 'r', encoding='utf-8') as f:
    current = f.read()

clean_strs = set()
for m in re.finditer('"([^"]*[\x80-\xFF][^"]*)"', clean):
    clean_strs.add(m.group(1))

curr_strs = {}
for m in re.finditer('"([^"]*[\x80-\xFF][^"]*)"', current):
    if m.group(1) not in clean_strs:
        curr_strs[m.group(1)] = m.group(0)

print('Clean: {}, Corrupted: {}'.format(len(clean_strs), len(curr_strs)))
for i, (bad, full) in enumerate(list(curr_strs.items())[:25]):
    # find best match
    best = None
    for clean in clean_strs:
        if len(clean) == len(bad):
            match = sum(1 for a,b in zip(clean, bad) if a==b and ord(a)<128)
            if match > 0 and (best is None or match > best[0]):
                best = (match, clean)
    if best:
        print('  {} -> {}'.format(repr(bad[:30]), repr(best[1][:30])))
