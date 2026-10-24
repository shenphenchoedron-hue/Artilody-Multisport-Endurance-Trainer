#!/usr/bin/env python3
"""Generates the Workout Creator icon set from the Artilody Watts Trainer icon
(../assets/icons/app-icon.png) by adding a pencil badge.

Needs Pillow. Writes assets/icons/{app-icon.png, app-icon-256.png, app-icon.ico}
and packaging/linux/icons/<n>x<n>/artilody-workout-creator.png. On macOS also
AppIcon.icns (via iconutil).
"""
import math
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "..", "assets", "icons", "app-icon.png")
OUT = os.path.join(ROOT, "assets", "icons")


def rot(points, angle, cx, cy):
    a = math.radians(angle)
    c, s = math.cos(a), math.sin(a)
    return [(cx + (x - cx) * c - (y - cy) * s, cy + (x - cx) * s + (y - cy) * c) for x, y in points]


def make_master():
    base = Image.open(SRC).convert("RGBA")
    size = 1024
    base = base.resize((size, size), Image.LANCZOS)
    ss = 4                              # supersampling for smooth edges
    S = size * ss
    badge = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(badge)

    # badge: bottom-right circle with a white ring and a soft shadow
    r = 0.205 * S
    cx, cy = 0.775 * S, 0.775 * S
    for i in range(10, 0, -1):          # shadow
        a = int(10 * (11 - i))
        d.ellipse([cx - r - i * ss * 1.6, cy - r - i * ss * 1.6 + 10 * ss,
                   cx + r + i * ss * 1.6, cy + r + i * ss * 1.6 + 10 * ss], fill=(5, 10, 40, a // 4))
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 255, 255, 255))
    ri = r * 0.86
    # vertical gradient clipped to the disc
    grad = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gd = ImageDraw.Draw(grad)
    top, bottom = int(cy - ri), int(cy + ri)
    for y in range(top, bottom + 1):
        t = (y - top) / max(1, bottom - top)
        gd.line([(cx - ri, y), (cx + ri, y)], fill=(255, int(176 - 80 * t), int(46 - 26 * t), 255))
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).ellipse([cx - ri, cy - ri, cx + ri, cy + ri], fill=255)
    badge.paste(grad, (0, 0), mask)

    # pencil (drawn horizontally, rotated -45°)
    L = ri * 1.25                       # total length
    w = ri * 0.30                       # body width
    x0 = cx - L / 2
    y0 = cy
    tip = L * 0.24
    eraser = L * 0.16
    body = [(x0 + tip, y0 - w / 2), (x0 + L - eraser, y0 - w / 2), (x0 + L - eraser, y0 + w / 2), (x0 + tip, y0 + w / 2)]
    cap = [(x0 + L - eraser + ri * 0.03, y0 - w / 2), (x0 + L, y0 - w / 2), (x0 + L, y0 + w / 2), (x0 + L - eraser + ri * 0.03, y0 + w / 2)]
    cone = [(x0, y0), (x0 + tip, y0 - w / 2), (x0 + tip, y0 + w / 2)]
    lead = [(x0, y0), (x0 + tip * 0.38, y0 - w * 0.19), (x0 + tip * 0.38, y0 + w * 0.19)]
    ang = -45
    d2 = ImageDraw.Draw(badge)
    d2.polygon(rot(body, ang, cx, cy), fill=(255, 255, 255, 255))
    d2.polygon(rot(cap, ang, cx, cy), fill=(255, 255, 255, 255))
    d2.polygon(rot(cone, ang, cx, cy), fill=(255, 236, 210, 255))
    d2.polygon(rot(lead, ang, cx, cy), fill=(40, 45, 90, 255))

    badge = badge.resize((size, size), Image.LANCZOS)
    return Image.alpha_composite(base, badge)


def main():
    master = make_master()
    os.makedirs(OUT, exist_ok=True)
    master.save(os.path.join(OUT, "app-icon.png"))
    master.resize((256, 256), Image.LANCZOS).save(os.path.join(OUT, "app-icon-256.png"))
    master.save(os.path.join(OUT, "app-icon.ico"),
                sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    for n in (48, 64, 128, 256, 512):
        d = os.path.join(ROOT, "packaging", "linux", "icons", "%dx%d" % (n, n))
        os.makedirs(d, exist_ok=True)
        master.resize((n, n), Image.LANCZOS).save(os.path.join(d, "artilody-workout-creator.png"))
    if sys.platform == "darwin" and shutil.which("iconutil"):
        tmp = tempfile.mkdtemp()
        iconset = os.path.join(tmp, "AppIcon.iconset")
        os.makedirs(iconset)
        for n in (16, 32, 128, 256, 512):
            master.resize((n, n), Image.LANCZOS).save(os.path.join(iconset, "icon_%dx%d.png" % (n, n)))
            master.resize((2 * n, 2 * n), Image.LANCZOS).save(os.path.join(iconset, "icon_%dx%d@2x.png" % (n, n)))
        subprocess.check_call(["iconutil", "-c", "icns", iconset, "-o", os.path.join(OUT, "AppIcon.icns")])
        shutil.rmtree(tmp)
    print("icons written to", OUT)


if __name__ == "__main__":
    main()
