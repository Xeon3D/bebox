#!/usr/bin/env python3
# BeBox emulator - draws the preferences' page and tab icons.
# Copyright 2026, the BeBox emulator authors. MIT License.
#
# Isometric icons in the manner of BeOS's: objects built from boxes,
# cylinders and spheres seen from above and to the right, a light top
# face, a lighter left and a darker right side with soft gradients, dark
# outlines, and a soft shadow towards the lower right. Rendered with a
# scanline rasterizer at 4x and downsampled; written as PNG (16 to 64
# pixels, for the Qt front end) and ICO (16-48, for the Windows one).
# No libraries needed:
#
#   python make_page_icons.py OUTPUT_FOLDER [NAME...]

import math
import os
import struct
import sys
import zlib

SUPERSAMPLE = 4
OUTLINE = (24, 22, 28)


# #pragma mark - colors


def hex_color(text):
    value = int(text.lstrip('#'), 16)
    return ((value >> 16) & 255, (value >> 8) & 255, value & 255)


def shade(color, factor):
    return tuple(max(0, min(255, int(c * factor))) for c in color)


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


# #pragma mark - the scene: 3D parts made of projected polygons


def project(point):
    x, y, z = point
    return ((x - y) * 0.8660254, (x + y) * 0.5 - z)


class Polygon:
    """A filled polygon in scene coordinates (before fitting), with a
    vertical gradient from 'top' to 'bottom' color, an optional outline."""

    def __init__(self, points, top, bottom=None, outline=None, width=0.0):
        self.points = points
        self.top = top
        self.bottom = bottom if bottom is not None else top
        self.outline = outline
        self.width = width


class Scene:
    def __init__(self):
        self.polygons = []		# drawn in order
        self.silhouettes = []	# convex hull outlines, drawn after each part

    def add(self, polygon):
        self.polygons.append(polygon)

    def outline(self, points3d, width=1.0):
        hull = convex_hull([project(p) for p in points3d])
        self.add(Polygon(hull, None, outline=OUTLINE, width=width))

    # Boxes: the top, the left side (y = y1) and the right side (x = x1).
    def box(self, x0, y0, z0, x1, y1, z1, color, decals=(), outline=True,
            edge=True):
        color = hex_color(color) if isinstance(color, str) else color
        top = [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
        left = [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)]
        right = [(x1, y1, z1), (x1, y0, z1), (x1, y0, z0), (x1, y1, z0)]
        edges = shade(color, 0.45) if edge else None
        faces = {
            'top': (top, shade(color, 1.30), shade(color, 1.12)),
            'left': (left, shade(color, 1.02), shade(color, 0.82)),
            'right': (right, shade(color, 0.70), shade(color, 0.54)),
        }
        for name in ('top', 'left', 'right'):
            points, a, b = faces[name]
            self.add(Polygon([project(p) for p in points], a, b, edges, 0.6))
        for decal in decals:
            decal(self, (x0, y0, z0, x1, y1, z1))
        if outline:
            self.outline([(x, y, z) for x in (x0, x1) for y in (y0, y1)
                          for z in (z0, z1)])

    # Cylinders standing up: the side in shaded strips, then the top.
    def cylinder(self, cx, cy, z0, z1, r, color, top=None, outline=True,
                 steps=32):
        color = hex_color(color) if isinstance(color, str) else color
        light = normalize((0.25, 0.85, 0.45))
        ring = [(cx + r * math.cos(2 * math.pi * i / steps),
                 cy + r * math.sin(2 * math.pi * i / steps))
                for i in range(steps)]
        for i in range(steps):
            a = 2 * math.pi * (i + 0.5) / steps
            normal = (math.cos(a), math.sin(a), 0)
            # the side the viewer sees faces +x and +y
            if normal[0] + normal[1] < -0.05:
                continue
            brightness = 0.55 + 0.55 * max(0, dot(normal, light))
            p, q = ring[i], ring[(i + 1) % steps]
            quad = [(p[0], p[1], z1), (q[0], q[1], z1), (q[0], q[1], z0),
                    (p[0], p[1], z0)]
            self.add(Polygon([project(v) for v in quad],
                             shade(color, brightness),
                             shade(color, brightness * 0.85)))
        top_color = hex_color(top) if isinstance(top, str) else top
        top_color = top_color or shade(color, 1.25)
        self.add(Polygon([project((p[0], p[1], z1)) for p in ring],
                         shade(top_color, 1.08), shade(top_color, 0.95),
                         shade(color, 0.45), 0.6))
        if outline:
            self.outline([(p[0], p[1], z) for p in ring for z in (z0, z1)])

    # Spheres: a disc with a highlight towards the upper left.
    def sphere(self, cx, cy, cz, r, color, steps=40):
        color = hex_color(color) if isinstance(color, str) else color
        sx, sy = project((cx, cy, cz))
        rr = r * 1.0
        circle = [(sx + rr * math.cos(2 * math.pi * i / steps),
                   sy + rr * math.sin(2 * math.pi * i / steps))
                  for i in range(steps)]
        self.add(Polygon(circle, shade(color, 1.05), shade(color, 0.55),
                         OUTLINE, 1.0))
        for t, size in ((0.25, 0.75), (0.5, 0.5), (0.75, 0.28)):
            hx = sx - rr * 0.3 * t
            hy = sy - rr * 0.35 * t
            spot = [(hx + rr * size * math.cos(2 * math.pi * i / steps),
                     hy + rr * size * math.sin(2 * math.pi * i / steps))
                    for i in range(steps)]
            c = mix(color, (255, 255, 255), 0.18 + 0.3 * t)
            self.add(Polygon(spot, c, shade(c, 0.9)))

    # A flat shape lying on a box face; 'rect' in the face's (u, v), 0..1.
    def on_face(self, box, face, rect, color, gradient=0.85, raised=0.0,
                edge=None):
        x0, y0, z0, x1, y1, z1 = box
        u0, v0, u1, v1 = rect

        def at(u, v):
            if face == 'top':
                return (x0 + (x1 - x0) * u, y1 - (y1 - y0) * v,
                        z1 + raised)
            if face == 'left':
                return (x0 + (x1 - x0) * u, y1 + raised,
                        z1 - (z1 - z0) * v)
            return (x1 + raised, y1 - (y1 - y0) * u, z1 - (z1 - z0) * v)

        color = hex_color(color) if isinstance(color, str) else color
        points = [at(u0, v0), at(u1, v0), at(u1, v1), at(u0, v1)]
        self.add(Polygon([project(p) for p in points], color,
                         shade(color, gradient), edge,
                         0.5 if edge else 0))

    def ellipse_on_top(self, cx, cy, z, r, color, start=0, end=360,
                       inner=0.0, steps=48):
        color = hex_color(color) if isinstance(color, str) else color
        n = max(2, int(steps * (end - start) / 360))
        outer = [(cx + r * math.cos(math.radians(start + (end - start)
                                                 * i / n)),
                  cy + r * math.sin(math.radians(start + (end - start)
                                                 * i / n)), z)
                 for i in range(n + 1)]
        if inner > 0:
            inside = [(cx + inner * math.cos(math.radians(start
                                                          + (end - start)
                                                          * i / n)),
                       cy + inner * math.sin(math.radians(start
                                                          + (end - start)
                                                          * i / n)), z)
                      for i in range(n, -1, -1)]
        else:
            inside = [(cx, cy, z)] if end - start < 360 else []
        self.add(Polygon([project(p) for p in outer + inside], color,
                         shade(color, 0.92)))

    def line(self, a, b, width, color):
        color = hex_color(color) if isinstance(color, str) else color
        self.add(Polygon(thick_line(project(a), project(b), width), color))


def normalize(v):
    length = math.sqrt(sum(c * c for c in v))
    return tuple(c / length for c in v)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def convex_hull(points):
    points = sorted(set(points))
    if len(points) <= 2:
        return points

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower, upper = [], []
    for p in points:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(points):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def thick_line(a, b, width):
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy) or 1
    nx, ny = -dy / length * width / 2, dx / length * width / 2
    return [(a[0] + nx, a[1] + ny), (b[0] + nx, b[1] + ny),
            (b[0] - nx, b[1] - ny), (a[0] - nx, a[1] - ny)]


# #pragma mark - the icons (scene units: about 0-10)


def machine(s):
    # a BeBox-like tower: two light pipes on its front, a floppy slot
    def front(scene, box):
        scene.on_face(box, 'left', (0.62, 0.08, 0.92, 0.12), '#0d0f14')
        scene.on_face(box, 'left', (0.14, 0.10, 0.24, 0.90), '#14180f')
        scene.on_face(box, 'left', (0.14, 0.42, 0.24, 0.90), '#8fe03a',
                      0.7)
        scene.on_face(box, 'left', (0.36, 0.10, 0.46, 0.90), '#1a140a')
        scene.on_face(box, 'left', (0.36, 0.64, 0.46, 0.90), '#f2a400',
                      0.7)
    s.box(0, 0, 0, 5, 7, 10, '#3e4a63', [front])


def rom(s):
    # a chip with pins on its long sides and a gold label
    for i in range(5):
        x = 0.8 + i * 1.6
        s.box(x, -0.8, -0.6, x + 0.7, 0.2, 0.6, '#c3c8d0', outline=False)
        s.box(x, 5.0, -0.6, x + 0.7, 6.0, 0.6, '#c3c8d0', outline=False)

    def label(scene, box):
        scene.on_face(box, 'top', (0.25, 0.25, 0.75, 0.75), '#e8c95e', 0.8)
        scene.ellipse_on_top(0.6, 2.6, box[5], 0.45, '#191b20')
    s.box(0, 0, 0, 8.6, 5.2, 1.4, '#30343c', [label])


def display(s):
    # a monitor on its foot: the screen faces the lower left
    s.box(2.0, 2.0, 0, 6.0, 6.0, 0.8, '#cfc6ad')
    s.box(3.3, 3.3, 0.8, 4.7, 4.7, 2.0, '#bdb397')

    def screen(scene, box):
        scene.on_face(box, 'left', (0.08, 0.10, 0.92, 0.88), '#1d2633')
        scene.on_face(box, 'left', (0.13, 0.15, 0.87, 0.83), '#3b8be6',
                      0.6)
        scene.on_face(box, 'left', (0.13, 0.15, 0.50, 0.40), '#7cb6f5',
                      0.85)
    s.box(0.5, 4.2, 2.0, 7.5, 7.8, 8.0, '#d8cfb6', [screen])


def keyboard(s):
    def keys(scene, box):
        for row in range(4):
            for col in range(10 - (row == 3) * 7):
                if row == 3:
                    rect = (0.28, 0.80, 0.72, 0.92)
                else:
                    rect = (0.05 + col * 0.093, 0.10 + row * 0.23,
                            0.12 + col * 0.093, 0.27 + row * 0.23)
                scene.on_face(box, 'top', rect, '#6c7380', 0.9)
    s.box(0, 0, 0, 11, 6.0, 1.8, '#dcdfe4', [keys])


def joystick(s):
    s.box(0, 0, 0, 7, 7, 1.6, '#3b404a')
    s.cylinder(1.4, 5.6, 1.6, 2.1, 0.7, '#d63a2e')
    s.cylinder(3.5, 3.5, 1.6, 6.5, 0.35, '#2b2f36', outline=False)
    s.sphere(3.5, 3.5, 7.3, 1.25, '#e0402f')


def sound(s):
    # a speaker cabinet: woofer and tweeter on the front
    def cones(scene, box):
        x0, y0, z0, x1, y1, z1 = box
        for cz, r, ring in ((6.6, 1.0, '#202227'), (3.0, 2.0, '#202227')):
            for radius, color in ((r, ring), (r * 0.78, '#7c828c'),
                                  (r * 0.25, '#2b2f36')):
                points = [(x0 + 2.4 + radius * math.cos(a), y1 + 0.01,
                           cz + radius * math.sin(a))
                          for a in [2 * math.pi * i / 36 for i in range(36)]]
                scene.add(Polygon([project(p) for p in points],
                                  hex_color(color),
                                  shade(hex_color(color), 0.8)))
    s.box(0, 0, 0, 4.8, 4.0, 9.0, '#8a5a32', [cones])


def network(s):
    # a globe on a stand, a cable plugged in
    s.cylinder(4, 4, 0, 0.8, 2.2, '#3b404a')
    s.cylinder(4, 4, 0.8, 2.2, 0.35, '#6c7380', outline=False)
    s.sphere(4, 4, 5.4, 3.4, '#2f78d0')
    sx, sy = project((4, 4, 5.4))
    # land: a few original blobs
    for blob in (((-1.9, -1.0), 1.0, 0.7), ((0.6, -1.9), 0.8, 0.5),
                 ((0.9, 0.6), 1.3, 0.9), ((-0.8, 1.6), 0.7, 0.45)):
        (bx, by), rx, ry = blob
        points = [(sx + bx + rx * math.cos(a) * (1 + 0.2 * math.sin(3 * a)),
                   sy + by + ry * math.sin(a) * (1 + 0.15 * math.cos(2 * a)))
                  for a in [2 * math.pi * i / 24 for i in range(24)]]
        s.add(Polygon(points, hex_color('#5cbf4a'), hex_color('#3f9134')))
    s.add(Polygon([(sx + 3.4 * math.cos(2 * math.pi * i / 40),
                    sy + 3.4 * math.sin(2 * math.pi * i / 40))
                   for i in range(40)], None, outline=OUTLINE, width=1.0))


def ports(s):
    # a D-sub plug, its cable housing behind it
    s.box(2.8, -3.0, 1.0, 5.6, 0.8, 3.0, '#3a3f48')

    def pins(scene, box):
        scene.on_face(box, 'left', (0.12, 0.18, 0.88, 0.82), '#20232a')
        for row, count, start in ((0, 5, 0.18), (1, 4, 0.26)):
            for i in range(count):
                u = start + i * 0.16
                v = 0.30 + row * 0.30
                scene.on_face(box, 'left', (u, v, u + 0.08, v + 0.15),
                              '#e8c95e')
    s.box(0.8, 0.8, 0.4, 7.6, 6.2, 3.6, '#c3c8d0', [pins])


def storage(s, connector=None):
    # a hard disk: a flat metal case with a label, the connector behind
    def label(scene, box):
        scene.on_face(box, 'top', (0.12, 0.15, 0.62, 0.85), '#f0f1f3', 0.9)
        scene.on_face(box, 'top', (0.16, 0.25, 0.58, 0.32), '#3b8be6')
        scene.on_face(box, 'top', (0.16, 0.40, 0.48, 0.45), '#9aa1ad')
        scene.on_face(box, 'top', (0.16, 0.52, 0.52, 0.57), '#9aa1ad')
        if connector:
            scene.on_face(box, 'right', (0.15, 0.30, 0.85, 0.70), connector)
        scene.on_face(box, 'left', (0.70, 0.35, 0.80, 0.60), '#8fe03a')
    s.box(0, 0, 0, 9, 6.5, 2.8, '#a4abb6', [label])


def scsi(s):
    storage(s, '#d8b23a')


def ide(s):
    storage(s, '#3b78c8')


def floppy(s):
    def parts(scene, box):
        scene.on_face(box, 'top', (0.30, 0.0, 0.72, 0.40), '#c9ced6', 0.85)
        scene.on_face(box, 'top', (0.52, 0.06, 0.62, 0.34), '#2b2f36')
        scene.on_face(box, 'top', (0.16, 0.55, 0.84, 1.0), '#f6f6f2', 0.9)
        scene.on_face(box, 'top', (0.22, 0.65, 0.78, 0.69), '#9aa1ad')
        scene.on_face(box, 'top', (0.22, 0.76, 0.66, 0.80), '#9aa1ad')
    s.box(0, 0, 0, 8, 8, 1.1, '#2f64b3', [parts])


def board(s):
    s.box(0, 0, 0, 10, 7, 0.9, '#2a8a4c')
    s.box(1.0, 1.0, 0.9, 4.0, 4.0, 2.0, '#25282e')
    s.box(5.5, 1.0, 0.9, 8.5, 2.6, 1.8, '#25282e')
    s.box(5.5, 3.6, 0.9, 8.5, 5.2, 1.8, '#25282e')
    s.box(0.6, 5.6, 0.9, 9.4, 6.6, 1.3, '#d8b23a')


def performance(s):
    # a dial: green to red around its face, the needle in the red
    s.cylinder(4, 4, 0, 1.4, 4.0, '#3b404a', top='#eceef1')
    for start, end, color in ((150, 230, '#8fe03a'), (230, 300, '#f2c400'),
                              (300, 360, '#e0503a'), (0, 30, '#e0503a')):
        s.ellipse_on_top(4, 4, 1.42, 3.4, color, start, end, inner=2.6)
    s.line((4, 4, 1.45), (4 + 3.0 * math.cos(math.radians(330)),
                          4 + 3.0 * math.sin(math.radians(330)), 1.45),
           0.45, '#202227')
    s.cylinder(4, 4, 1.4, 1.7, 0.5, '#202227', outline=False)


def leds(s):
    def lit(color, level):
        def decal(scene, box):
            scene.on_face(box, 'left', (0.22, 0.04, 0.78, 0.96), '#14161a')
            scene.on_face(box, 'left', (0.22, 1 - level, 0.78, 0.96), color,
                          0.75)
        return decal
    s.box(0, 0, 0, 1.6, 1.6, 10, '#2b2f36', [lit('#8fe03a', 0.62)])
    s.box(3.4, -3.4, 0, 5.0, -1.8, 10, '#2b2f36', [lit('#f2a400', 0.3)])


ICONS = {
    'machine': machine, 'rom': rom, 'display': display, 'input': keyboard,
    'keyboard': keyboard, 'joystick': joystick,
    'sound': sound, 'network': network, 'ports': ports, 'serial': ports,
    'storage': storage, 'scsi': scsi, 'ide': ide, 'floppy': floppy,
    'board': board, 'performance': performance, 'leds': leds,
}


# #pragma mark - rasterizing


def fit(polygons, size):
    """Scene coordinates to pixels: the object in the upper left, room for
    the shadow at the lower right."""
    xs = [p[0] for poly in polygons for p in poly.points]
    ys = [p[1] for poly in polygons for p in poly.points]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    margin = size * 0.02
    room = size * 0.92 - margin
    scale = room / max(x1 - x0, y1 - y0)
    ox = margin + (room - (x1 - x0) * scale) / 2 - x0 * scale
    oy = margin + (room - (y1 - y0) * scale) / 2 - y0 * scale
    return scale, ox, oy


def fill(canvas, mask, size, points, top, bottom):
    ys = [p[1] for p in points]
    y_min, y_max = min(ys), max(ys)
    if y_max - y_min < 1e-6:
        return
    n = len(points)
    for row in range(max(0, int(y_min)), min(size, int(y_max) + 1)):
        y = row + 0.5
        crossings = []
        for i in range(n):
            ax, ay = points[i]
            bx, by = points[(i + 1) % n]
            if (ay > y) != (by > y):
                crossings.append(ax + (y - ay) * (bx - ax) / (by - ay))
        crossings.sort()
        t = (y - y_min) / (y_max - y_min)
        color = mix(top, bottom, t)
        base = row * size
        for i in range(0, len(crossings) - 1, 2):
            start = max(0, int(math.ceil(crossings[i] - 0.5)))
            end = min(size, int(math.floor(crossings[i + 1] - 0.5)) + 1)
            for col in range(start, end):
                canvas[base + col] = color
                mask[base + col] = 1


def outline_polygon(canvas, mask, size, points, color, width):
    n = len(points)
    for i in range(n):
        a, b = points[i], points[(i + 1) % n]
        fill(canvas, mask, size, thick_line(a, b, width), color, color)
        # round joints
        joint = [(a[0] + width / 2 * math.cos(2 * math.pi * k / 8),
                  a[1] + width / 2 * math.sin(2 * math.pi * k / 8))
                 for k in range(8)]
        fill(canvas, mask, size, joint, color, color)


def blur(values, size, radius):
    if radius < 1:
        return values
    out = values
    for horizontal in (True, False):
        result = [0.0] * (size * size)
        for line in range(size):
            total = 0.0
            window = []
            index = (lambda k: line * size + k) if horizontal \
                else (lambda k: k * size + line)
            prefix = [0.0]
            for k in range(size):
                prefix.append(prefix[-1] + out[index(k)])
            for k in range(size):
                lo = max(0, k - radius)
                hi = min(size, k + radius + 1)
                result[index(k)] = (prefix[hi] - prefix[lo]) / (2 * radius
                                                                 + 1)
        out = result
    return out


def render(make, size):
    scene = Scene()
    make(scene)
    big = size * SUPERSAMPLE
    scale, ox, oy = fit(scene.polygons, big)
    canvas = [(0, 0, 0)] * (big * big)
    mask = [0] * (big * big)
    line_width = max(1.0, size / 32.0) * SUPERSAMPLE
    for polygon in scene.polygons:
        points = [(x * scale + ox, y * scale + oy) for x, y in polygon.points]
        if polygon.top is not None:
            fill(canvas, mask, big, points, polygon.top, polygon.bottom)
        if polygon.outline is not None and polygon.width > 0:
            outline_polygon(canvas, mask, big, points, polygon.outline,
                            line_width * polygon.width)

    # the shadow: the shape moved to the lower right and blurred
    shift = int(big * 0.045)
    moved = [0.0] * (big * big)
    for y in range(big - shift):
        for x in range(big - shift):
            if mask[y * big + x]:
                moved[(y + shift) * big + x + shift] = 1.0
    shadow = blur(moved, big, int(big * 0.03))

    pixels = []
    s2 = SUPERSAMPLE * SUPERSAMPLE
    for y in range(size):
        row = []
        for x in range(size):
            r = g = b = 0.0
            covered = 0
            shadow_alpha = 0.0
            for sy in range(SUPERSAMPLE):
                base = (y * SUPERSAMPLE + sy) * big + x * SUPERSAMPLE
                for sx in range(SUPERSAMPLE):
                    i = base + sx
                    if mask[i]:
                        c = canvas[i]
                        r += c[0]
                        g += c[1]
                        b += c[2]
                        covered += 1
                    else:
                        shadow_alpha += shadow[i] * 0.42
            alpha = (covered + shadow_alpha) / s2
            if alpha <= 0:
                row.append((0, 0, 0, 0))
                continue
            # the shadow is black: it adds coverage, no color
            total = covered + shadow_alpha
            row.append((int(r / total + 0.5), int(g / total + 0.5),
                        int(b / total + 0.5), min(255, int(alpha * 255
                                                           + 0.5))))
        pixels.append(row)
    return pixels


# #pragma mark - files


def png(rows):
    raw = bytearray()
    for row in rows:
        raw.append(0)
        for pixel in row:
            raw += bytes(pixel)

    def chunk(kind, body):
        return (struct.pack('>I', len(body)) + kind + body
                + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff))

    size = len(rows)
    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0,
                                         0))
            + chunk(b'IDAT', zlib.compress(bytes(raw), 9))
            + chunk(b'IEND', b''))


def ico(images):
    """'images': (size, rows) pairs, stored as 32-bit DIBs."""
    entries = []
    for size, rows in images:
        header = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, 0,
                             0, 0, 0, 0)
        data = bytearray()
        for row in reversed(rows):
            for r, g, b, a in row:
                data += bytes((b, g, r, a))
        data += bytes(((size + 31) // 32) * 4 * size)
        entries.append((size, header + bytes(data)))
    output = bytearray(struct.pack('<HHH', 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    for size, image in entries:
        output += struct.pack('<BBBBHHII', size, size, 0, 0, 1, 32,
                              len(image), offset)
        offset += len(image)
    for _, image in entries:
        output += image
    return bytes(output)


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else 'icons'
    names = sys.argv[2:] or list(ICONS)
    os.makedirs(folder, exist_ok=True)
    for name in names:
        make = ICONS[name]
        images = {size: render(make, size) for size in (16, 20, 24, 32, 48,
                                                        64)}
        for size in (16, 24, 32, 48, 64):
            with open(os.path.join(folder, '%s-%d.png' % (name, size)),
                      'wb') as f:
                f.write(png(images[size]))
        with open(os.path.join(folder, name + '.ico'), 'wb') as f:
            f.write(ico([(s, images[s]) for s in (16, 20, 24, 32, 48)]))


if __name__ == '__main__':
    main()
