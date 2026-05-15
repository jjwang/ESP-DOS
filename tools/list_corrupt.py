with open('kernel/shell.c', 'r', encoding='utf-8', errors='replace') as f:
    lines = f.readlines()

with open('corrupted_lines.txt', 'w', encoding='utf-8') as out:
    for i, line in enumerate(lines):
        if '\ufffd' in line:
            out.write(f'{i+1}: {line.rstrip()[:100]}\n')

print('Done - check corrupted_lines.txt')
