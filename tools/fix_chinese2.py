# Fix all corrupted Chinese strings in shell.c
# Uses shell_clean.c as reference for correct Chinese

import re

with open('kernel/shell_clean.c', 'r', encoding='utf-16') as f:
    clean = f.read()

with open('kernel/shell.c', 'rb') as f:
    data = f.read()

# Find all corrupted sequences (U+FFFD = EF BF BD)
# For each corruption, find the surrounding string context
# and look up the correct version from clean file

# Strategy: extract all string literals from clean file as reference
clean_strings = {}
for m in re.finditer('"([^"]*[^\x00-\x7F][^"]*)"', clean):
    s = m.group(1)
    # key by the first few Chinese chars
    key = ''.join(c for c in s if ord(c) > 0x7F)[:4]
    if key:
        clean_strings[key] = s

# Now fix corrupted strings in current file
txt = data.decode('utf-8', errors='replace')

# Known specific fixes (corrupted -> correct)
fixes = {
    '\ufffd ': '\u4e0d\u5b58\u5728',  # 不存在
    # These are context-specific, we need to handle each corruption
}

# Find all corrupted positions and fix them
corrupted_indices = [i for i, c in enumerate(txt) if c == '\ufffd']

print(f'Found {len(corrupted_indices)} corrupted positions')
for pos in corrupted_indices:
    start = max(0, pos - 20)
    end = min(len(txt), pos + 10)
    context = txt[start:end]
    # Extract the corrupted Chinese chars around this position
    # Try to match with clean strings
    fixed = False
    for key, cs in clean_strings.items():
        if key in context:
            # Calculate the correct replacement
            corr_part = context[20:20+len(cs)] if len(context) >= 20+len(cs) else ''
            if len(corr_part) == len(cs):
                txt = txt[:pos-len(cs)+len(key)] + cs + txt[pos+1:]
                fixed = True
                break
    if not fixed:
        print(f'  Cannot fix at pos {pos}: ...{repr(context)}...')

with open('kernel/shell.c', 'w', encoding='utf-8') as f:
    f.write(txt)
print('Done')
