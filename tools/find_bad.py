with open('kernel/shell.c','rb') as f:
    data = f.read()
count = data.count(b'\xef\xbf\xbd')
print(f'Found {count} corrupted characters')
