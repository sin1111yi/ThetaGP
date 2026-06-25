#!/usr/bin/env python3
"""Extract glyph data from LVGL font C file into AaFont format."""
import re, sys

def parse_font(path):
    with open(path) as f:
        content = f.read()

    # Extract glyph_bitmap hex bytes
    bitmap = []
    bm = re.search(r'glyph_bitmap\[\]\s*=\s*\{', content)
    if bm:
        start = bm.end()
        depth = 1
        i = start
        while i < len(content) and depth > 0:
            if content[i] == '{': depth += 1
            elif content[i] == '}': depth -= 1
            i += 1
        bitmap_text = content[start:i-1]
        for m in re.finditer(r'0x([0-9a-fA-F]+)', bitmap_text):
            bitmap.append(int(m.group(1), 16))

    # Extract glyph_dsc entries
    glyphs = [None]  # index 0 reserved
    dsc = re.search(r'glyph_dsc\[\]\s*=\s*\{', content)
    if dsc:
        start = dsc.end()
        depth = 1
        i = start
        while i < len(content) and depth > 0:
            if content[i] == '{': depth += 1
            elif content[i] == '}': depth -= 1
            i += 1
        dsc_text = content[start:i-1]

        for entry in re.finditer(r'\{([^}]+)\}', dsc_text):
            fields = entry.group(1)
            # Parse designated initializers: .bitmap_index = N, .box_w = N, .box_h = N, .ofs_x = N, .ofs_y = N, .adv_w = N
            vals = {}
            for m in re.finditer(r'\.(\w+)\s*=\s*(-?\d+)', fields):
                vals[m.group(1)] = int(m.group(2))
            glyphs.append({
                'bitmap_index': vals.get('bitmap_index', 0),
                'adv_w': vals.get('adv_w', 0),
                'box_w': vals.get('box_w', 0),
                'box_h': vals.get('box_h', 0),
                'ofs_x': vals.get('ofs_x', 0),
                'ofs_y': vals.get('ofs_y', 0),
            })

    # Extract line_height and base_line from lv_font_t struct
    lh = re.search(r'\.line_height\s*=\s*(\d+)', content)
    bl = re.search(r'\.base_line\s*=\s*(\d+)', content)
    line_height = int(lh.group(1)) if lh else 11
    base_line = int(bl.group(1)) if bl else 2

    return bitmap, glyphs, line_height, base_line

if len(sys.argv) < 4:
    print(f"Usage: {sys.argv[0]} <lv_font.c> <output.cpp> <var_name>")
    sys.exit(1)

path, out_path, var_name = sys.argv[1], sys.argv[2], sys.argv[3]
bitmap, glyphs, lh, bl = parse_font(path)

with open(out_path, 'w') as f:
    f.write('#include "aa_font.h"\n')
    f.write('namespace ThetaGP::Drivers::Device::DisplayDrv {\n\n')

    # Bitmap
    f.write(f'static const uint8_t {var_name}_bitmap[] = {{\n')
    for i in range(0, len(bitmap), 16):
        f.write('  ' + ','.join(f'0x{b:02x}' for b in bitmap[i:i+16]) + ',\n')
    f.write('};\n\n')

    # Glyph descriptors
    f.write(f'static const GlyphDesc {var_name}_glyphs[] = {{\n')
    for g in glyphs:
        if g is None:
            f.write('  {0,0,0,0,0,0},\n')
        else:
            f.write(f'  {{{g["bitmap_index"]},{g["adv_w"]},{g["box_w"]},{g["box_h"]},{g["ofs_x"]},{g["ofs_y"]}}},\n')
    f.write('};\n\n')

    f.write(f'static const AaFont s_{var_name}({var_name}_bitmap, {var_name}_glyphs, {lh}, {bl});\n')
    f.write(f'const AaFont &{var_name} = s_{var_name};\n')
    f.write('\n} // namespace\n')

print(f"Wrote {out_path}: {len(bitmap)} bitmap bytes, {len(glyphs)} glyphs")
