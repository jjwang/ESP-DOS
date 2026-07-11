# ESP-DOS Wokwi Simulator
#
# 准备:
#   1. 安装 Python 依赖: pip install esptool
#   2. 编译固件: pio run -e esp32-s3-dev
#   3. 准备 SPIFFS: python tools/build_apps.py && python tools/mkspiffs.py
#
# 构建模拟器镜像:
#   python prepare_wokwi.py
#
# 然后上传到 https://wokwi.com/projects/new
# 选择 "ESP32-S3-DevKitC-1"，上传生成的 firmware.bin
#
# 注意:
# - Wokwi 不支持 TCA8418 键盘模拟，请使用串口输入
# - ST7789 屏幕通过 Wokwi 的内置 LCD 模拟
# - SPIFFS 需要作为 flash 的一部分上传

import os, sys, subjson, shutil

PROJECT = r'C:\pj\opencrab\ESP-DOS'
OUTPUT = os.path.join(PROJECT, 'wokwi')

def main():
    os.makedirs(OUTPUT, exist_ok=True)
    
    # 1. firmware.bin (merged flash binary)
    fw_src = os.path.join(PROJECT, '.pio', 'build', 'esp32-s3-dev', 'firmware.bin')
    if os.path.isfile(fw_src):
        shutil.copy2(fw_src, os.path.join(OUTPUT, 'firmware.bin'))
        print(f'firmware.bin: {os.path.getsize(fw_src)} bytes')
    else:
        print(f'firmware.bin not found at {fw_src}')
        print('Run: pio run -e esp32-s3-dev')
    
    # 2. SPIFFS binary
    spiffs_src = os.path.join(PROJECT, '.pio', 'build', 'esp32-s3-dev', 'littlefs.bin')
    if not os.path.isfile(spiffs_src):
        spiffs_src = os.path.join(PROJECT, '.pio', 'build', 'esp32-s3-dev', 'spiffs.bin')
    if os.path.isfile(spiffs_src):
        shutil.copy2(spiffs_src, os.path.join(OUTPUT, 'spiffs.bin'))
        print(f'spiffs.bin: {os.path.getsize(spiffs_src)} bytes')
    else:
        print('SPIFFS image not found')
    
    print(f'\nFiles ready in {OUTPUT}')
    print('Upload firmware.bin + spiffs.bin to Wokwi')

if __name__ == '__main__':
    main()
