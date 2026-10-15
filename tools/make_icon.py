#!/usr/bin/env python3
"""Generates Resources/icon.png (1024x1024) - the Artilody Soundsculp app icon."""
import math
import os
from PIL import Image, ImageDraw, ImageFilter

S = 1024
here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, "..", "Resources", "icon.png")


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def gradient_color(t):
    stops = [(0.0, (255, 140, 40)), (0.45, (214, 60, 160)), (0.75, (124, 77, 255)), (1.0, (0, 200, 230))]
    for i in range(len(stops) - 1):
        t0, c0 = stops[i]
        t1, c1 = stops[i + 1]
        if t <= t1:
            return lerp(c0, c1, (t - t0) / (t1 - t0))
    return stops[-1][1]


# background: rounded square with vertical gradient
bg = Image.new("RGBA", (S, S), (0, 0, 0, 0))
grad = Image.new("RGBA", (S, S))
gd = ImageDraw.Draw(grad)
for y in range(S):
    gd.line([(0, y), (S, y)], fill=lerp((22, 30, 56), (10, 14, 30), y / S) + (255,))
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle([40, 40, S - 40, S - 40], radius=200, fill=255)
bg.paste(grad, (0, 0), mask)

# gradient layer used for the "A" and the waveform
colour = Image.new("RGBA", (S, S))
cd = ImageDraw.Draw(colour)
for x in range(S):
    cd.line([(x, 0), (x, S)], fill=gradient_color(x / S) + (255,))

shape = Image.new("L", (S, S), 0)
sd = ImageDraw.Draw(shape)
# Stylised "A"
w = 92
sd.line([(250, 800), (512, 190)], fill=255, width=w)
sd.line([(512, 190), (774, 800)], fill=255, width=w)
sd.ellipse([512 - w / 2, 190 - w / 2, 512 + w / 2, 190 + w / 2], fill=255)
for x in (250, 774):
    sd.ellipse([x - w / 2, 800 - w / 2, x + w / 2, 800 + w / 2], fill=255)
# waveform across the crossbar
for i in range(23):
    x = 300 + i * 19
    amp = (math.sin(i * 0.9) * 0.5 + 0.5) * 120 * math.sin(math.pi * i / 22) + 18
    sd.rounded_rectangle([x - 6, 590 - amp, x + 6, 590 + amp], radius=6, fill=255)

glow = shape.filter(ImageFilter.GaussianBlur(18))
glow_img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
glow_img.paste(colour, (0, 0), glow.point(lambda v: int(v * 0.6)))
bg = Image.alpha_composite(bg, glow_img)
fg = Image.new("RGBA", (S, S), (0, 0, 0, 0))
fg.paste(colour, (0, 0), shape)
bg = Image.alpha_composite(bg, fg)

bg.save(out)
bg.resize((256, 256), Image.LANCZOS).save(os.path.join(here, "..", "Resources", "icon_256.png"))
bg.save(os.path.join(here, "..", "packaging", "windows", "icon.ico"),
        sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("wrote", out)
