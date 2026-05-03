#!/usr/bin/env python3
"""
字库生成工具 v5
- 英文: u8g2_font_6x12_tf (Misc-Fixed 6x12 BDF) 左移1像素填满格子
- 中文: FreeType 渲染 SimSun 12x12
"""

import freetype, os
from bdf_data import DATA

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "..", "include", "fonts")

ASCII_CHARS = "".join(chr(i) for i in range(0x20, 0x7F))

CJK_START   = 0x4E00
CJK_END     = 0x9FFF
EMOJI_START = 0x1F300
EMOJI_END   = 0x1F9FF

# ===== 英文: 本地BDF数据 =====

def parse_bdf_local():
    glyphs = {}
    for i, line in enumerate(DATA):
        code = 0x20 + i
        vals = [int(x, 16) for x in line.split()]
        leftmost = 6
        rightmost = -1
        for v in vals:
            for col in range(6):
                if v & (0x80 >> col):
                    if col < leftmost: leftmost = col
                    if col > rightmost: rightmost = col
        actual_w = rightmost - leftmost + 1 if rightmost >= leftmost else 0
        if actual_w <= 0 or actual_w >= 6:
            shift = 0
        else:
            shift = (6 - actual_w) // 2 - leftmost
        shifted = []
        for v in vals:
            if shift > 0:
                sv = (v << shift) & 0xFF
            elif shift < 0:
                sv = (v >> (-shift)) & 0xFF
            else:
                sv = v
            shifted.append(sv)
        glyphs[code] = shifted
    return glyphs

# ===== 中文: FreeType渲染 =====

def face_from(fname, px):
    try:
        d = os.environ.get('WINDIR', 'C:\\Windows') + '\\Fonts'
        face = freetype.Face(d + '\\' + fname)
        face.set_pixel_sizes(0, px)
        return face
    except:
        return None

def render_cn(face, ch, cell_w, cell_h, bits, y_base):
    face.load_char(ch, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
    bm = face.glyph.bitmap
    bw, bh, buf, pitch = bm.width, bm.rows, bm.buffer, bm.pitch
    left, top = face.glyph.bitmap_left, face.glyph.bitmap_top
    start_y = y_base - top

    rows = []
    for y in range(cell_h):
        v = 0
        by = y - start_y
        for x in range(cell_w):
            bx = x - left
            px = 0
            if 0 <= bx < bw and 0 <= by < bh:
                idx = by * pitch + bx // 8
                if idx < len(buf):
                    px = (buf[idx] >> (7 - bx % 8)) & 1
            if px:
                v |= (1 << (bits - 1 - x))
        rows.append(v)
    return rows

def collect_valid(face, start, end, progress_label=""):
    chars = []
    total = end - start + 1
    for i, code in enumerate(range(start, end + 1)):
        if i % 1000 == 0:
            print(f"  {progress_label}: {i}/{total}", end="\r")
        ch = chr(code)
        try:
            if face.get_char_index(ch) == 0:
                continue
            rows = render_cn(face, ch, 12, 12, 16, 10)
            chars.append((code, rows))
        except:
            pass
    print(f"  {progress_label}: {len(chars)}/{total}")
    return chars

# ===== 生成 =====

def gen_ascii():
    print("生成英文 6x12 字库...")
    glyphs = parse_bdf_local()

    rows_list = []
    for ch in ASCII_CHARS:
        bmp = glyphs.get(ord(ch), [0]*12)
        rows = bmp
        rows_list.append(rows)

    out = [
        "#ifndef __FONT_6X12_H__",
        "#define __FONT_6X12_H__",
        "#include <stdint.h>",
        "",
        "#define FONT_6X12_COUNT 95",
        "#define FONT_6X12_START 0x20",
        "#define FONT_6X12_END   0x7E",
        "#define FONT_6X12_W     6",
        "#define FONT_6X12_H     12",
        "",
        "static const uint8_t font_6x12[][12] = {",
    ]

    for i, rows in enumerate(rows_list):
        fixed = [r & 0xFC for r in rows]
        s = ", ".join(f"0x{r:02X}" for r in fixed)
        ch = ASCII_CHARS[i]
        rp = f"'{ch}'" if ch not in '\\"\'' else repr(ch)
        out.append(f"    {{ {s} }},  /* {rp} (0x{ord(ch):02X}) */")

    out.append("};")
    out.append("#endif")

    p = os.path.join(OUTPUT_DIR, "font_6x12.h")
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    with open(p, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out))
    print(f"  OK ({len(rows_list)} chars)")

def gen_cn():
    print("生成中文 12x12 字库...")
    face = (face_from('simsun.ttc', 12) or face_from('msyh.ttc', 12) or
            face_from('simfang.ttf', 12) or face_from('simhei.ttf', 12))
    if not face:
        print("错误: 未找到中文字体")
        return False

    entries = collect_valid(face, CJK_START, CJK_END, "CJK")

    emoji_face = (face_from('seguiemj.ttf', 12) or face_from('seguiemj.ttc', 12))
    if emoji_face:
        emoji_entries = collect_valid(emoji_face, EMOJI_START, EMOJI_END, "Emoji")
        entries.extend(emoji_entries)
    else:
        print("  未找到表情字体, 跳过")

    entries.sort(key=lambda x: x[0])

    out = [
        "#ifndef __FONT_CN_12X12_H__",
        "#define __FONT_CN_12X12_H__",
        "#include <stdint.h>",
        "",
        f"#define FONT_CN_COUNT  {len(entries)}",
        "#define FONT_CN_W      12",
        "#define FONT_CN_H      12",
        "",
        "typedef struct {",
        "    uint32_t unicode;",
        "    uint16_t index;",
        "} font_cn_entry_t;",
        "",
        "static const uint16_t font_cn_data[][12] = {",
    ]

    for i, (code, rows) in enumerate(entries):
        s = ", ".join(f"0x{r:04X}" for r in rows)
        out.append(f"    {{ {s} }},  /* U+{code:04X} */")

    out.append("};")
    out.append("")
    out.append("static const font_cn_entry_t font_cn_map[] = {")

    for i, (code, _) in enumerate(entries):
        out.append(f"    {{ 0x{code:04X}, {i} }},")

    out.append("};")
    out.append("#endif")

    p = os.path.join(OUTPUT_DIR, "font_cn_12x12.h")
    with open(p, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out))
    print(f"  OK ({len(entries)} chars)")
    return True

if __name__ == '__main__':
    print("=" * 50)
    print("OpenCrab 字库生成工具 v5")
    print("英文: u8g2_font_6x12_tf (BDF)")
    print("中文: FreeType SimSun 12x12")
    print("=" * 50)
    gen_ascii()
    print()
    gen_cn()
    print()
    print("完成!")
