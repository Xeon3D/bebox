#!/usr/bin/env python3
# BeBox emulator for Windows - draws the application icon (BeBox.ico).
# Copyright 2026, the BeBox emulator authors. MIT License.
#
# A dark tower case with the front panel's two light pipes (lime and
# amber), at 16, 24, 32, 48, 64 and 256 pixels, antialiased by sampling.
# No libraries needed:
#
#   python make_icon.py BeBox.ico

import struct
import sys
import zlib


def rounded_box(x, y, left, top, right, bottom, radius):
    """Whether (x, y) is inside the box with rounded corners."""
    if x < left or x > right or y < top or y > bottom:
        return False
    cx = min(max(x, left + radius), right - radius)
    cy = min(max(y, top + radius), bottom - radius)
    return (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2


def shade(u, v):
    """The color (r, g, b, a) at (u, v) in 0..1 icon space, or None."""
    # the case: slightly narrower at the back for depth
    if rounded_box(u, v, 0.20, 0.04, 0.80, 0.96, 0.06):
        # the front bezel: a lighter band
        light = 0.13 + 0.10 * (1 - v)
        color = [light * 255, light * 255, (light + 0.03) * 255, 255]
        # two light pipes
        for left, lit, unlit in ((0.30, (150, 255, 40), (20, 40, 12)),
                                 (0.62, (255, 170, 0), (40, 28, 6))):
            if rounded_box(u, v, left, 0.12, left + 0.08, 0.86, 0.02):
                # segments, lit from the bottom up
                segment = int((0.86 - v) / 0.74 * 8)
                level = 6 if left < 0.5 else 3
                c = lit if segment < level else unlit
                glow = 0.75 + 0.25 * (1 - abs((u - left - 0.04) / 0.04))
                color = [c[0] * glow, c[1] * glow, c[2] * glow, 255]
        # a highlight along the top edge
        if v < 0.08:
            color = [min(255, x + 40) for x in color[:3]] + [255]
        return color
    # shadow to the right and below
    if rounded_box(u, v, 0.24, 0.08, 0.84, 1.0, 0.06):
        return [0, 0, 0, 70]
    return None


def render(size):
    samples = 4
    pixels = []
    for y in range(size):
        row = []
        for x in range(size):
            total = [0.0, 0.0, 0.0, 0.0]
            for sy in range(samples):
                for sx in range(samples):
                    u = (x + (sx + 0.5) / samples) / size
                    v = (y + (sy + 0.5) / samples) / size
                    c = shade(u, v)
                    if c is None:
                        continue
                    a = c[3] / 255.0
                    total[0] += c[0] * a
                    total[1] += c[1] * a
                    total[2] += c[2] * a
                    total[3] += a
            n = samples * samples
            alpha = total[3] / n
            if alpha > 0:
                rgb = [min(255, int(t / total[3] + 0.5)) for t in total[:3]]
            else:
                rgb = [0, 0, 0]
            row.append((rgb[0], rgb[1], rgb[2], int(alpha * 255 + 0.5)))
        pixels.append(row)
    return pixels


def bmp_entry(size, pixels):
    """A 32-bit DIB for an icon directory entry (bottom up, AND mask)."""
    header = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, 0,
                         0, 0, 0, 0)
    data = bytearray()
    for row in reversed(pixels):
        for r, g, b, a in row:
            data += bytes((b, g, r, a))
    mask_stride = ((size + 31) // 32) * 4
    data += bytes(mask_stride * size)
    return header + bytes(data)


def png_entry(size, pixels):
    raw = bytearray()
    for row in pixels:
        raw.append(0)
        for r, g, b, a in row:
            raw += bytes((r, g, b, a))

    def chunk(kind, body):
        return (struct.pack('>I', len(body)) + kind + body
                + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff))

    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0,
                                         0, 0))
            + chunk(b'IDAT', zlib.compress(bytes(raw), 9))
            + chunk(b'IEND', b''))


def main():
    sizes = [16, 24, 32, 48, 64, 256]
    images = []
    for size in sizes:
        pixels = render(size)
        images.append(png_entry(size, pixels) if size >= 256
                      else bmp_entry(size, pixels))
    output = bytearray(struct.pack('<HHH', 0, 1, len(sizes)))
    offset = 6 + 16 * len(sizes)
    for size, image in zip(sizes, images):
        output += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1,
                              32, len(image), offset)
        offset += len(image)
    for image in images:
        output += image
    with open(sys.argv[1] if len(sys.argv) > 1 else 'BeBox.ico', 'wb') as f:
        f.write(output)


if __name__ == '__main__':
    main()
