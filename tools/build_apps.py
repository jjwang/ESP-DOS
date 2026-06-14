#!/usr/bin/env python3
"""编译 ELF 命令工具"""
import os, subprocess, sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJ_DIR = os.path.dirname(SCRIPT_DIR)
APPS_DIR = os.path.join(PROJ_DIR, "apps")
OUT_DIR = os.path.join(SCRIPT_DIR, "..", "data", "bin")  # data/bin/ 用于SPIFFS镜像

# Xtensa 编译工具链
TOOLCHAIN = os.path.join(
    os.environ.get('USERPROFILE', 'C:\\Users\\NEC'),
    '.platformio', 'packages', 'toolchain-xtensa-esp-elf', 'bin'
)
CC = os.path.join(TOOLCHAIN, 'xtensa-esp32s3-elf-gcc.exe')
OBJCOPY = os.path.join(TOOLCHAIN, 'xtensa-esp32s3-elf-objcopy.exe')

CFLAGS = [
    '-nostdlib', '-ffreestanding', '-Os',
    '-mno-serialize-volatile', '-mabi=windowed',
    '-fPIC',
    '-I', os.path.join(PROJ_DIR, 'include'),
]

LDFLAGS = [
    '-nostartfiles', '-nodefaultlibs',
    '-Wl,-N,--gc-sections',
    '-Wl,-Ttext-segment=0x3F000000',
    '-Wl,--section-start=.text=0x3F000000',
    '-e', '_start',
]

def build_app(name):
    src = os.path.join(APPS_DIR, f'{name}.c')
    if not os.path.exists(src):
        print(f"  SKIP {name} (source not found)")
        return False

    elf = os.path.join(OUT_DIR, name)
    os.makedirs(OUT_DIR, exist_ok=True)

    cmd = [CC] + CFLAGS + [src, '-o', elf] + LDFLAGS
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  FAIL {name}: {result.stderr}")
        return False

    # strip to reduce size
    subprocess.run([OBJCOPY, '--strip-all', elf], capture_output=True)
    
    size = os.path.getsize(elf)
    print(f"  OK   {name} ({size} bytes)")

    # 生成 C 头文件 (嵌入固件)
    h_path = os.path.join(PROJ_DIR, 'include', 'fonts', f'elf_{name}.h')
    with open(elf, 'rb') as f:
        data = f.read()
    
    lines = [
        f'#ifndef __ELF_{name.upper()}_H__',
        f'#define __ELF_{name.upper()}_H__',
        f'#include <stdint.h>',
        f'',
        f'#define ELF_{name.upper()}_SIZE {size}',
        f'',
        f'static const uint8_t elf_{name}_data[] = {{',
    ]
    for i in range(0, len(data), 12):
        chunk = data[i:i+12]
        hex_str = ', '.join(f'0x{b:02X}' for b in chunk)
        lines.append(f'    {hex_str},')
    lines.append('};')
    lines.append('#endif')
    
    with open(h_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    
    print(f"  GEN  elf_{name}.h")
    return True

def main():
    print("编译 ELF 命令...")
    print(f"工具链: {CC}")
    print()
    
    apps = ['echo', 'hello', 'date', 'free', 'uname', 'df', 'edit']
    for app in apps:
        build_app(app)
    
    print(f"\n输出目录: {OUT_DIR}")

if __name__ == '__main__':
    main()
