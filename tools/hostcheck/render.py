#!/usr/bin/env python3
"""Turns the recorded draw calls (shots/*.txt) back into PNG screens.

The host build writes one primitive per line:

    fillScreen #rrggbb
    fillRect x y w h #rrggbb      rect/rrect/frrect/circle/disc/line/px ...
    text x y size lineHeight #rrggbb string

Nothing here is a simulator: it is a replay of exactly the calls the firmware
makes, at 2x scale, so the layout can be reviewed without the panel attached.
"""
import os
import re
import sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
W, H, SCALE = 320, 240, 2

# The panel fonts are not available on the host, so each recorded line height
# is mapped to the closest DejaVu face.
FACE_BY_HEIGHT = {
    0: (10, False),   # built-in 5x7 bitmap font
    14: (13, False),  # FreeSans9pt / FreeSansBold9pt
    18: (16, False),  # FreeSans12pt
    25: (23, False),  # FreeSansBold18pt
    33: (30, True),   # FreeSansBold24pt
}
FONT_CACHE = {}


def face(line_height, scale=1.0):
    key = (line_height, scale)
    if key not in FONT_CACHE:
        size, _ = FACE_BY_HEIGHT.get(line_height, (13, False))
        size = max(6, int(size * scale))
        bold = line_height >= 14  # the firmware uses bold faces for headings
        path = "/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf" % ("-Bold" if bold else "")
        FONT_CACHE[key] = ImageFont.truetype(path, size)
    return FONT_CACHE[key]


def parse_color(tok):
    tok = tok.lstrip("#")
    return tuple(int(tok[i:i + 2], 16) for i in (0, 2, 4))


def render(path):
    img = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(img)
    for raw in open(path, encoding="utf-8", errors="replace"):
        line = raw.rstrip("\n")
        if not line:
            continue
        parts = line.split(" ")
        op = parts[0]
        try:
            if op == "fillScreen":
                d.rectangle([0, 0, W - 1, H - 1], fill=parse_color(parts[1]))
            elif op == "fillRect":
                x, y, w, h = map(int, parts[1:5])
                d.rectangle([x, y, x + w - 1, y + h - 1], fill=parse_color(parts[5]))
            elif op == "rect":
                x, y, w, h = map(int, parts[1:5])
                d.rectangle([x, y, x + w - 1, y + h - 1], outline=parse_color(parts[5]))
            elif op == "rrect":
                x, y, w, h, r = map(int, parts[1:6])
                d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=r, outline=parse_color(parts[6]))
            elif op == "frrect":
                x, y, w, h, r = map(int, parts[1:6])
                d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=r, fill=parse_color(parts[6]))
            elif op == "circle":
                x, y, r = map(int, parts[1:4])
                d.ellipse([x - r, y - r, x + r, y + r], outline=parse_color(parts[4]))
            elif op == "disc":
                x, y, r = map(int, parts[1:4])
                d.ellipse([x - r, y - r, x + r, y + r], fill=parse_color(parts[4]))
            elif op == "line":
                x0, y0, x1, y1 = map(int, parts[1:5])
                d.line([x0, y0, x1, y1], fill=parse_color(parts[5]))
            elif op == "ftri":
                vals = list(map(int, parts[1:7]))
                d.polygon([(vals[0], vals[1]), (vals[2], vals[3]), (vals[4], vals[5])], fill=parse_color(parts[7]))
            elif op == "px":
                x, y = int(parts[1]), int(parts[2])
                d.point((x, y), fill=parse_color(parts[3]))
            elif op == "text":
                x, y = int(parts[1]), int(parts[2])
                size = int(parts[3])
                lh = int(parts[4])
                color = parse_color(parts[5])
                s = " ".join(parts[6:])
                if lh == 0:
                    # Built-in 5x7 font: drawn from the top left corner.
                    d.text((x, y), s, font=face(10, size * 0.85), fill=color)
                    continue
                # GFX fonts are positioned by their baseline, PIL by the top of
                # the line box, so shift the text up by the ascent.
                size_px = FACE_BY_HEIGHT.get(lh, (13, False))[0] * size
                d.text((x, y - int(size_px * 0.8)), s, font=face(lh, size), fill=color)
        except (ValueError, IndexError):
            continue
    return img


def sheet(names, out_path):
    cols = 4
    pad = 16
    label_h = 22
    rows = (len(names) + cols - 1) // cols
    tile_w, tile_h = W * SCALE, H * SCALE
    img = Image.new("RGB", (cols * tile_w + (cols + 1) * pad, rows * (tile_h + label_h) + (rows + 1) * pad),
                    (18, 18, 22))
    d = ImageDraw.Draw(img)
    lbl = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 18)
    for i, name in enumerate(names):
        shot = os.path.join(HERE, "shots", name + ".txt")
        if not os.path.exists(shot):
            continue
        png = render(shot).resize((tile_w, tile_h), Image.NEAREST)
        cx = pad + (i % cols) * (tile_w + pad)
        cy = pad + (i // cols) * (tile_h + label_h + pad)
        img.paste(png, (cx, cy))
        d.rectangle([cx - 1, cy - 1, cx + tile_w, cy + tile_h], outline=(70, 70, 80))
        d.text((cx, cy + tile_h + 2), name.replace("-", " "), font=lbl, fill=(230, 230, 235))
    img.save(out_path)
    print("wrote", out_path, img.size)


def main():
    os.makedirs(os.path.join(HERE, "png"), exist_ok=True)
    args = sys.argv[1:]
    names = [n for n in args if not n.endswith(".png")]
    if not names:
        names = sorted(f[:-4] for f in os.listdir(os.path.join(HERE, "shots")) if f.endswith(".txt"))
    for n in names:
        shot = os.path.join(HERE, "shots", n + ".txt")
        if not os.path.exists(shot):
            continue
        render(shot).save(os.path.join(HERE, "png", n + ".png"))
    if args and args[-1].endswith(".png"):
        sheet(sorted(names), args[-1])
        return
    print("rendered", len(names), "shots")


if __name__ == "__main__":
    main()
