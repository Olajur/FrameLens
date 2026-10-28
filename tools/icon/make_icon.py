"""Generates src/app/framelens.ico: the FrameLens icon (three green bars on a dark rounded square),
the same design the app draws at runtime for its window and tray icon (MakeIcon in framelens_main.cpp).

    python tools/icon/make_icon.py
"""
import math
import struct
from pathlib import Path

SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
BG = (0x14, 0x14, 0x14)
GREEN = (0x4C, 0xD9, 0x4C)


def pixels(s):
    """Rows top-to-bottom of (r, g, b, a), with 2x2 supersampled edges."""
    r = s * 0.2
    heights = [0.32, 0.52, 0.72]
    out = []
    for y in range(s):
        row = []
        for x in range(s):
            cover = 0
            for sy in (0.25, 0.75):
                for sx in (0.25, 0.75):
                    px, py = x + sx, y + sy
                    cx = min(max(px, r), s - r)
                    cy = min(max(py, r), s - r)
                    cover += math.hypot(px - cx, py - cy) <= r
            a = round(255 * cover / 4)
            col = BG
            for b, h in enumerate(heights):
                x0, x1 = int(s * (0.18 + b * 0.23)), int(s * (0.18 + b * 0.23 + 0.17))
                y1 = int(s * 0.84)
                y0 = int(y1 - s * h)
                if x0 <= x < x1 and y0 <= y < y1:
                    col = GREEN
            row.append((*col, a))
        out.append(row)
    return out


def bmp_entry(s):
    """32-bit BMP icon image (BITMAPINFOHEADER + BGRA bottom-up + AND mask)."""
    px = pixels(s)
    header = struct.pack('<IiiHHIIiiII', 40, s, s * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    color = b''.join(bytes((b, g, r, a)) for row in reversed(px) for (r, g, b, a) in row)
    mask_row = ((s + 31) // 32) * 4
    mask = bytes(mask_row * s)   # all zero: alpha channel decides transparency
    return header + color + mask


def main():
    images = [bmp_entry(s) for s in SIZES]
    out = struct.pack('<HHH', 0, 1, len(SIZES))
    offset = 6 + 16 * len(SIZES)
    for s, data in zip(SIZES, images):
        out += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    out += b''.join(images)
    target = Path(__file__).resolve().parents[2] / 'src' / 'app' / 'framelens.ico'
    target.write_bytes(out)
    print(f'wrote {target} ({len(out)} bytes, sizes {SIZES})')


if __name__ == '__main__':
    main()
