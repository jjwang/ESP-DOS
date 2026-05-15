# -*- coding: utf-8 -*-
import re

def extract_strings(filename, encoding):
    with open(filename, 'r', encoding=encoding) as f:
        text = f.read()
    return re.findall(r'"([^"]*[\x80-\xFF][^"]*)"', text)

# Get all Chinese strings from clean file
clean_strs = extract_strings('kernel/shell_clean.c', 'utf-16')
current_strs = extract_strings('kernel/shell.c', 'utf-8')

# Build mapping from corrupted substring to clean string
fix_map = {}
for cs in current_strs:
    # a corrupted string has some non-Chinese chars mixed in
    # find a clean string that starts/ends similarly
    for clean in clean_strs:
        # if clean contains Chinese chars that also appear in corrupted
        overlap = sum(1 for c in clean if c in cs and ord(c) > 127)
        if overlap >= 4 and abs(len(clean) - len(cs)) < 5:
            fix_map[cs] = clean

with open('kernel/shell.c', 'r', encoding='utf-8') as f:
    content = f.read()

for bad, good in fix_map.items():
    content = content.replace('"' + bad + '"', '"' + good + '"')

print(f'Fixed {len(fix_map)} strings:')
for bad, good in list(fix_map.items())[:5]:
    print(f'  {bad[:20]} -> {good[:20]}')

with open('kernel/shell.c', 'w', encoding='utf-8') as f:
    f.write(content)
