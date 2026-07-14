#!/usr/bin/env python3
"""一键构建: 编译 ELF 命令 + 打包 SPIFFS 镜像 + 烧录"""
import os, subprocess, sys, shutil

PROJ_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TOOLCHAIN = os.path.join(
    os.environ.get('USERPROFILE', 'C:\\Users\\NEC'),
    '.platformio', 'packages', 'toolchain-xtensa-esp-elf', 'bin'
)
MKSZ = 'C:\\Users\\NEC\\.platformio\\packages\\tool-mkspiffs\\mkspiffs.exe'
ESPPORT = os.environ.get('ESP_PORT', 'COM3')

CC = os.path.join(TOOLCHAIN, 'xtensa-esp32s3-elf-gcc.exe')

def build_elfs():
    """编译 ELF 命令到 data/bin/"""
    app_dir = os.path.join(PROJ_DIR, 'apps')
    out_dir = os.path.join(PROJ_DIR, 'data', 'bin')
    os.makedirs(out_dir, exist_ok=True)

    CFLAGS = ['-nostdlib', '-ffreestanding', '-Os',
              '-mno-serialize-volatile', '-mabi=windowed',
              '-I', os.path.join(PROJ_DIR, 'include')]
    LDFLAGS = ['-nostdlib', '-T', os.path.join(app_dir, 'command.ld'),
               '-Wl,-N,--gc-sections']

    apps = [f.replace('.c', '') for f in os.listdir(app_dir) if f.endswith('.c')]
    if not apps:
        print("没有找到应用源文件")
        return False

    ok = 0
    for name in apps:
        src = os.path.join(app_dir, f'{name}.c')
        elf = os.path.join(out_dir, name)
        r = subprocess.run([CC] + CFLAGS + [src, '-o', elf] + LDFLAGS,
                          capture_output=True, text=True)
        if r.returncode != 0:
            print(f"  FAIL {name}: {r.stderr}")
        else:
            subprocess.run([os.path.join(TOOLCHAIN, 'xtensa-esp32s3-elf-strip.exe'),
                          '--strip-all', elf], capture_output=True)
            sz = os.path.getsize(elf)
            print(f"  OK   {name} ({sz} bytes)")
            ok += 1
    print(f"  共 {ok}/{len(apps)} 个应用编译成功")
    return ok > 0

def build_spiffs():
    """生成 SPIFFS 镜像"""
    data_dir = os.path.join(PROJ_DIR, 'data')
    img_path = os.path.join(PROJ_DIR, 'spiffs.bin')
    
    if not os.path.exists(data_dir):
        print("data/ 目录不存在")
        return False
    
    if not os.path.exists(MKSZ):
        print(f"mkspiffs 未找到: {MKSZ}")
        return False

    # 计算 data 目录大小
    total = 0
    for root, dirs, files in os.walk(data_dir):
        for f in files:
            total += os.path.getsize(os.path.join(root, f))
    
    # SPIFFS 分区: 从 0x410000 开始, 大小 ~12MB
    # 使用 0x200000 (2MB) 足够装所有 ELF
    img_size = max(total + 65536, 0x100000)
    
    cmd = [MKSZ, '-c', data_dir, '-b', '4096', '-p', '256', '-s', hex(img_size), img_path]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(f"SPIFFS 生成失败: {r.stderr}")
        return False
    
    print(f"SPIFFS 镜像: {img_path} ({img_size/1024:.0f} KB)")
    return True

def flash_all():
    """烧录固件 + SPIFFS"""
    fw = os.path.join(PROJ_DIR, '.pio', 'build', 'esp32-s3-dev', 'firmware.bin')
    spiffs = os.path.join(PROJ_DIR, 'spiffs.bin')
    
    if not os.path.exists(fw):
        print(f"固件未找到: {fw}")
        return False
    
    esptool = 'C:\\Users\\NEC\\.platformio\\packages\\tool-esptoolpy\\esptool.py'
    
    apps = [
        [sys.executable, esptool, '--chip', 'esp32s3', '-p', ESPPORT, '-b', '460800',
         'write_flash', '0x00000', os.path.join(PROJ_DIR, '.pio', 'build', 'esp32-s3-dev', 'bootloader.bin')],
        [sys.executable, esptool, '--chip', 'esp32s3', '-p', ESPPORT, '-b', '460800',
         'write_flash', '0x8000', os.path.join(PROJ_DIR, '.pio', 'build', 'esp32-s3-dev', 'partitions.bin')],
        [sys.executable, esptool, '--chip', 'esp32s3', '-p', ESPPORT, '-b', '460800',
         'write_flash', '0x10000', fw],
    ]

    if os.path.exists(spiffs):
        apps.append(
            [sys.executable, esptool, '--chip', 'esp32s3', '-p', ESPPORT, '-b', '460800',
             'write_flash', '0x410000', spiffs]
        )

    for app in apps:
        print(f"  烧录 {app[-1]}...")
        r = subprocess.run(app, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"  失败: {r.stderr}")
            return False
    
    print("烧录完成!")
    return True

if __name__ == '__main__':
    print("=" * 50)
    print("ESP-DOS 完整构建")
    print("=" * 50)
    
    if not build_elfs():
        print("ELF 编译失败")
        sys.exit(1)
    print()
    
    print("构建 SPIFFS 镜像...")
    if not build_spiffs():
        print("SPIFFS 构建失败, 跳过")

    if '--noburn' in sys.argv:
        print("\n跳过烧录")
        sys.exit(0)
    
    print()
    print("烧录中...")
    flash_all()
