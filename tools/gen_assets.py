#!/usr/bin/env python3
"""
Gera TODAS as texturas e sprites do jogo de forma procedural (sem arquivos externos,
sem licencas). Saidas:
  gfx/*.png                 -> imagens editaveis
  gfx/_preview.png          -> folha de contato para conferir visualmente
  source/gfx_data.c / .h    -> arrays A1RGB5 prontos para glTexImage2D (GL_RGBA)

Uso:  python3 tools/gen_assets.py        (requer Pillow e numpy)
Para trocar uma arte: edite/substitua o PNG em gfx/ e rode  python3 tools/gen_assets.py --from-png
"""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GFX = os.path.join(ROOT, "gfx")
SRC = os.path.join(ROOT, "source")
os.makedirs(GFX, exist_ok=True)
S = 4  # supersampling dos sprites

# ------------------------------------------------------------------ ruido tileavel
def pnoise(n, cells, seed):
    r = np.random.default_rng(seed)
    g = r.random((cells, cells))
    xs = np.arange(n) * cells / n
    i0 = np.floor(xs).astype(int); f = xs - i0; f = f * f * (3 - 2 * f); i1 = (i0 + 1) % cells
    a = g[np.ix_(i0, i0)]; b = g[np.ix_(i0, i1)]; c = g[np.ix_(i1, i0)]; d = g[np.ix_(i1, i1)]
    fx = f[None, :]; fy = f[:, None]
    return a * (1 - fx) * (1 - fy) + b * fx * (1 - fy) + c * (1 - fx) * fy + d * fx * fy

def fbm(n, base, octs, seed):
    tot = 0; amp = 1; norm = 0
    for o in range(octs):
        tot = tot + amp * pnoise(n, base * 2 ** o, seed + o * 17); norm += amp; amp *= 0.5
    return tot / norm

def mix(c0, c1, t):
    c0 = np.array(c0, float); c1 = np.array(c1, float)
    return c0[None, None, :] * (1 - t[..., None]) + c1[None, None, :] * t[..., None]

def to_img(a):
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), "RGB").convert("RGBA")

def wrap_draw(img, fn):
    """chama fn(draw, dx, dy) para as 9 copias deslocadas -> desenho tileavel"""
    w, h = img.size
    d = ImageDraw.Draw(img)
    for dy in (-h, 0, h):
        for dx in (-w, 0, w):
            fn(d, dx, dy)

def speckle(a, seed, amt=10):
    r = np.random.default_rng(seed)
    return a + (r.random(a.shape[:2])[..., None] - 0.5) * amt

# ------------------------------------------------------------------ texturas de terreno (64x64, tileaveis)
def tex_grass(seed=1, dry=False):
    n = 64
    v = fbm(n, 4, 4, seed); v2 = fbm(n, 12, 2, seed + 9)
    if dry:
        a = mix((92, 96, 38), (160, 146, 66), v * 0.7 + v2 * 0.3)
    else:
        a = mix((26, 62, 22), (84, 136, 42), v * 0.7 + v2 * 0.3)
    img = to_img(speckle(a, seed, 14))
    rr = np.random.default_rng(seed + 3)
    cols = ([(190, 176, 84), (130, 120, 50), (110, 100, 40)] if dry else [(120, 180, 60), (40, 100, 30), (96, 150, 48), (20, 56, 20)])
    for _ in range(230):
        x, y = rr.integers(0, n), rr.integers(0, n); L = int(rr.integers(2, 5)); dx = int(rr.integers(-1, 2))
        col = cols[int(rr.integers(0, len(cols)))]
        wrap_draw(img, lambda d, ox, oy: d.line([(x + ox, y + oy), (x + dx + ox, y - L + oy)], fill=col + (255,), width=1))
    if not dry:   # florzinhas
        for _ in range(7):
            x, y = rr.integers(0, n), rr.integers(0, n); col = [(240, 230, 90), (230, 230, 240), (200, 90, 200)][int(rr.integers(0, 3))]
            wrap_draw(img, lambda d, ox, oy: d.point((x + ox, y + oy), fill=col + (255,)))
    return img

def tex_dirt(seed=5):
    n = 64
    v = fbm(n, 5, 4, seed)
    a = mix((84, 58, 36), (140, 104, 66), v)
    img = to_img(speckle(a, seed, 18))
    rr = np.random.default_rng(seed)
    for _ in range(40):
        x, y = rr.integers(0, n), rr.integers(0, n); g = int(rr.integers(90, 170))
        wrap_draw(img, lambda d, ox, oy: d.ellipse([x + ox, y + oy, x + 1 + ox, y + 1 + oy], fill=(g, g - 10, g - 25, 255)))
    return img

def tex_rock(seed=9):
    n = 64
    v = fbm(n, 4, 4, seed); w = fbm(n, 6, 3, seed + 5)
    a = mix((78, 74, 70), (156, 150, 140), v)
    cr = np.abs(w - 0.5) < 0.035
    a[cr] *= 0.45
    hi = np.abs(w - 0.5 - 0.05) < 0.02
    a[hi] = a[hi] * 1.12 + 6
    return to_img(speckle(a, seed, 16))

def tex_sand(seed=11):
    n = 64
    v = fbm(n, 6, 3, seed)
    yy, xx = np.mgrid[0:n, 0:n]
    rip = np.sin((yy + v * 14) * (2 * math.pi / 16)) * 0.5 + 0.5
    a = mix((196, 172, 120), (232, 212, 156), v * 0.6 + rip * 0.4)
    return to_img(speckle(a, seed, 14))

def tex_snow(seed=13):
    n = 64
    v = fbm(n, 5, 4, seed)
    a = mix((196, 208, 228), (250, 252, 255), v)
    return to_img(speckle(a, seed, 8))

def tex_cobble(seed=17):
    n = 64
    img = Image.new("RGBA", (n, n), (58, 52, 46, 255))
    d = ImageDraw.Draw(img); rr = np.random.default_rng(seed)
    y = 0
    for row in range(4):
        x = -8 if row % 2 else 0
        while x < n:
            w = int(rr.integers(13, 19)); g = int(rr.integers(96, 150)); warm = int(rr.integers(-10, 14))
            col = (g + warm, g, g - warm, 255)
            d.rounded_rectangle([x + 1, y + 1, x + w - 1, y + 14], radius=4, fill=col)
            d.line([(x + 3, y + 3), (x + w - 4, y + 3)], fill=(min(255, g + 28),) * 3 + (255,))
            x += w
        y += 16
    a = np.array(img.convert("RGB")).astype(float)
    return to_img(speckle(a, seed, 14))

def tex_water(seed=21):
    n = 32
    v = fbm(n, 3, 3, seed); w = fbm(n, 5, 2, seed + 4)
    a = mix((20, 70, 140), (60, 140, 200), v)
    ca = np.abs(w - 0.5) < 0.05
    a[ca] = a[ca] * 0.6 + np.array([140, 200, 240]) * 0.4
    return to_img(a)

# ------------------------------------------------------------------ construcoes
def tex_wall(seed=31):
    n = 64
    a = mix((196, 176, 140), (226, 208, 170), fbm(n, 4, 3, seed))
    img = to_img(speckle(a, seed, 18)); d = ImageDraw.Draw(img)
    wood = (88, 56, 30, 255); hi = (122, 82, 46, 255)
    for (x0, y0, x1, y1) in [(0, 0, 63, 4), (0, 59, 63, 63), (0, 0, 4, 63), (59, 0, 63, 63), (29, 4, 34, 59)]:
        d.rectangle([x0, y0, x1, y1], fill=wood); d.line([(x0, y0), (x1 if x1 - x0 > y1 - y0 else x0, y1 if y1 - y0 >= x1 - x0 else y0)], fill=hi)
    d.line([(4, 4), (29, 30)], fill=wood, width=3); d.line([(59, 4), (34, 30)], fill=wood, width=3)
    return img

def tex_roof(seed=33):
    n = 64
    img = Image.new("RGBA", (n, n), (90, 40, 30, 255)); d = ImageDraw.Draw(img); rr = np.random.default_rng(seed)
    for row in range(8):
        y = row * 8
        off = 4 if row % 2 else 0
        for x in range(-8 + off, n, 8):
            c = int(rr.integers(-14, 15))
            d.pieslice([x, y - 4, x + 8, y + 12], 0, 180, fill=(150 + c, 66 + c // 2, 44 + c // 3, 255), outline=(70, 30, 24, 255))
    a = np.array(img.convert("RGB")).astype(float)
    return to_img(speckle(a, seed, 12))

def tex_door():
    img = Image.new("RGBA", (32, 64), (96, 62, 34, 255)); d = ImageDraw.Draw(img)
    rr = np.random.default_rng(41)
    for x in range(0, 32, 8):
        g = int(rr.integers(-12, 12)); d.rectangle([x, 0, x + 6, 63], fill=(100 + g, 66 + g, 36 + g, 255)); d.line([(x + 7, 0), (x + 7, 63)], fill=(40, 24, 12, 255))
    for y in (10, 48):
        d.rectangle([0, y, 31, y + 3], fill=(50, 50, 56, 255))
    d.ellipse([24, 30, 28, 34], fill=(220, 190, 70, 255))
    d.rectangle([0, 0, 31, 63], outline=(50, 32, 16, 255))
    return img

def tex_dg_wall(seed=51):
    n = 64
    img = Image.new("RGBA", (n, n), (30, 30, 36, 255)); d = ImageDraw.Draw(img); rr = np.random.default_rng(seed)
    y = 0
    for row in range(4):
        x = -10 if row % 2 else 0
        while x < n:
            w = int(rr.integers(14, 22)); g = int(rr.integers(62, 104))
            d.rectangle([x + 1, y + 1, x + w - 1, y + 14], fill=(g, g + 2, g + 8, 255))
            d.line([(x + 1, y + 1), (x + w - 1, y + 1)], fill=(g + 22, g + 24, g + 30, 255))
            x += w
        y += 16
    a = np.array(img.convert("RGB")).astype(float)
    m = fbm(n, 5, 3, seed + 1) > 0.62
    a[m] = a[m] * 0.6 + np.array([40, 80, 36]) * 0.4   # musgo
    return to_img(speckle(a, seed, 12))

def tex_dg_floor(seed=53):
    n = 64
    img = Image.new("RGBA", (n, n), (24, 24, 28, 255)); d = ImageDraw.Draw(img); rr = np.random.default_rng(seed)
    for gy in range(2):
        for gx in range(2):
            g = int(rr.integers(70, 104)); d.rectangle([gx * 32 + 1, gy * 32 + 1, gx * 32 + 30, gy * 32 + 30], fill=(g, g - 2, g - 8, 255))
    a = np.array(img.convert("RGB")).astype(float)
    cr = np.abs(fbm(n, 6, 3, seed) - 0.5) < 0.025
    a[cr] *= 0.5
    return to_img(speckle(a, seed, 14))

def tex_dg_ceil(seed=55):
    a = np.array(tex_dg_wall(seed).convert("RGB")).astype(float) * 0.65
    return to_img(a)

def tex_dg_door():
    img = Image.new("RGBA", (32, 64), (70, 68, 74, 255)); d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 31, 63], fill=(88, 86, 94, 255))
    d.pieslice([3, 4, 28, 30], 180, 360, fill=(6, 6, 8, 255)); d.rectangle([3, 17, 28, 63], fill=(6, 6, 8, 255))
    for i in range(6):
        y = 50 + i * 3; g = 40 + i * 6
        d.rectangle([6, y, 25, y + 2], fill=(g, g, g + 6, 255))
    d.arc([2, 3, 29, 31], 180, 360, fill=(130, 128, 138, 255), width=2)
    return img

# ------------------------------------------------------------------ sprites (supersample)
def canvas(w=64, h=64):
    return Image.new("RGBA", (w * S, h * S), (0, 0, 0, 0))

def P(pts):
    return [(x * S, y * S) for x, y in pts]

def ell(d, x0, y0, x1, y1, fill, outline=None):
    d.ellipse([x0 * S, y0 * S, x1 * S, y1 * S], fill=fill, outline=outline)

def lin(d, x0, y0, x1, y1, w, fill):
    d.line([(x0 * S, y0 * S), (x1 * S, y1 * S)], fill=fill, width=int(w * S))
    r = w * S / 2
    for (x, y) in ((x0, y0), (x1, y1)):
        d.ellipse([x * S - r, y * S - r, x * S + r, y * S + r], fill=fill)

def finish(img, w=64, h=64, seed=0, edge=True):
    img = img.resize((w, h), Image.LANCZOS)
    a = np.array(img).astype(float)
    rr = np.random.default_rng(seed)
    a[..., :3] += (rr.random((h, w))[..., None] - 0.5) * 10
    al = a[..., 3] >= 128
    if edge:
        pad = np.pad(al, 1, constant_values=False)
        border = al & ~(pad[:-2, 1:-1] & pad[2:, 1:-1] & pad[1:-1, :-2] & pad[1:-1, 2:])
        a[border, :3] *= 0.62
    a[..., 3] = np.where(al, 255, 0)
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), "RGBA")

def shade_poly(d, pts, base, hi=0.0):
    d.polygon(P(pts), fill=base)

def sprite_tree_pine():
    im = canvas(); d = ImageDraw.Draw(im)
    d.polygon(P([(29, 62), (35, 62), (34, 44), (30, 44)]), fill=(84, 54, 30, 255))
    tiers = [(44, 40, 14), (32, 32, 13), (20, 24, 12), (9, 17, 11)]
    for i, (yb, hw, ht) in enumerate(tiers):
        yt = yb - ht - 6
        g = 38 + i * 10
        d.polygon(P([(32, yt), (32 - hw * 0.5 - 4, yb), (32 + hw * 0.5 + 4, yb)]), fill=(20 + i * 4, g + 30, 28 + i * 2, 255))
        d.polygon(P([(32, yt), (32 - hw * 0.5 - 4, yb), (32, yb - 2)]), fill=(14 + i * 3, g + 14, 22, 255))
        d.line(P([(32 - hw * 0.5 - 4, yb), (32 + hw * 0.5 + 4, yb)]), fill=(10, 36, 18, 255), width=S)
    return finish(im, seed=1)

def sprite_tree_oak():
    im = canvas(); d = ImageDraw.Draw(im); rr = np.random.default_rng(2)
    d.polygon(P([(28, 63), (36, 63), (34, 38), (30, 38)]), fill=(92, 60, 34, 255))
    d.polygon(P([(31, 50), (24, 38), (26, 36), (32, 46)]), fill=(92, 60, 34, 255))
    d.polygon(P([(33, 48), (42, 36), (40, 34), (32, 44)]), fill=(92, 60, 34, 255))
    blobs = [(32, 20, 17), (18, 28, 12), (46, 28, 12), (26, 12, 11), (40, 12, 11), (32, 32, 12)]
    for (cx, cy, r) in blobs:
        ell(d, cx - r, cy - r, cx + r, cy + r, (30, 88, 30, 255))
    for (cx, cy, r) in blobs:
        ell(d, cx - r * 0.8, cy - r * 0.9, cx + r * 0.7, cy + r * 0.6, (50, 124, 40, 255))
    for _ in range(46):
        x, y = rr.integers(12, 52), rr.integers(4, 40); r = rr.integers(1, 3)
        c = [(96, 170, 60, 255), (24, 70, 26, 255), (70, 146, 48, 255)][int(rr.integers(0, 3))]
        ell(d, x - r, y - r, x + r, y + r, c)
    return finish(im, seed=2)

def sprite_tree_dead():
    im = canvas(); d = ImageDraw.Draw(im)
    c = (96, 80, 66, 255); c2 = (60, 48, 40, 255)
    d.polygon(P([(28, 63), (37, 63), (34, 30), (31, 30)]), fill=c)
    d.polygon(P([(31, 63), (37, 63), (34, 30), (33, 30)]), fill=c2)
    for (x0, y0, x1, y1, w) in [(32, 40, 18, 24, 2.4), (33, 34, 46, 18, 2.4), (32, 30, 30, 8, 2.2), (18, 24, 12, 22, 1.6), (46, 18, 54, 16, 1.6), (33, 24, 40, 10, 1.6)]:
        lin(d, x0, y0, x1, y1, w, c)
    return finish(im, seed=3)

def sprite_house_far():
    im = canvas(); d = ImageDraw.Draw(im)
    d.rectangle([8 * S, 32 * S, 56 * S, 62 * S], fill=(206, 186, 150, 255))
    d.rectangle([8 * S, 32 * S, 12 * S, 62 * S], fill=(88, 56, 30, 255)); d.rectangle([52 * S, 32 * S, 56 * S, 62 * S], fill=(88, 56, 30, 255))
    d.rectangle([8 * S, 58 * S, 56 * S, 62 * S], fill=(88, 56, 30, 255)); d.rectangle([8 * S, 32 * S, 56 * S, 35 * S], fill=(88, 56, 30, 255))
    d.polygon(P([(32, 6), (2, 36), (62, 36)]), fill=(150, 66, 44, 255))
    d.polygon(P([(32, 6), (32, 36), (62, 36)]), fill=(122, 52, 36, 255))
    for k in range(5):
        y = 14 + k * 5; d.line(P([(32 - (y - 6) * 0.95, y + 3), (32 + (y - 6) * 0.95, y + 3)]), fill=(80, 32, 24, 255), width=S)
    d.rectangle([44 * S, 8 * S, 50 * S, 22 * S], fill=(110, 100, 94, 255))
    d.rectangle([26 * S, 42 * S, 38 * S, 62 * S], fill=(96, 62, 34, 255)); d.rectangle([26 * S, 42 * S, 38 * S, 62 * S], outline=(50, 32, 16, 255), width=S)
    d.rectangle([14 * S, 42 * S, 22 * S, 50 * S], fill=(250, 210, 120, 255), outline=(88, 56, 30, 255), width=S)
    d.rectangle([42 * S, 42 * S, 50 * S, 50 * S], fill=(250, 210, 120, 255), outline=(88, 56, 30, 255), width=S)
    return finish(im, seed=4)

def sprite_dungeon_far():
    im = canvas(); d = ImageDraw.Draw(im)
    d.polygon(P([(2, 63), (8, 34), (20, 20), (34, 14), (48, 22), (58, 38), (62, 63)]), fill=(104, 98, 92, 255))
    d.polygon(P([(34, 14), (48, 22), (58, 38), (62, 63), (36, 63)]), fill=(84, 80, 76, 255))
    for (x, y, r) in [(16, 30, 4), (46, 28, 5), (52, 46, 4), (10, 48, 5)]:
        ell(d, x - r, y - r, x + r, y + r, (52, 92, 44, 255))
    d.rectangle([20 * S, 34 * S, 44 * S, 63 * S], fill=(130, 126, 120, 255))
    d.pieslice([20 * S, 22 * S, 44 * S, 46 * S], 180, 360, fill=(130, 126, 120, 255))
    d.pieslice([24 * S, 28 * S, 40 * S, 44 * S], 180, 360, fill=(8, 8, 10, 255)); d.rectangle([24 * S, 36 * S, 40 * S, 63 * S], fill=(8, 8, 10, 255))
    for i in range(4):
        y = 54 + i * 3; g = 30 + i * 12; d.rectangle([25 * S, y * S, 39 * S, (y + 2) * S], fill=(g, g, g + 6, 255))
    lin(d, 17, 63, 17, 32, 2, (60, 44, 30, 255)); ell(d, 14.5, 28, 19.5, 33, (255, 170, 40, 255)); ell(d, 15.7, 29.4, 18.3, 32, (255, 230, 120, 255))
    lin(d, 47, 63, 47, 32, 2, (60, 44, 30, 255)); ell(d, 44.5, 28, 49.5, 33, (255, 170, 40, 255)); ell(d, 45.7, 29.4, 48.3, 32, (255, 230, 120, 255))
    return finish(im, seed=5)

def sprite_exit():
    im = canvas(); d = ImageDraw.Draw(im)
    d.pieslice([6 * S, 4 * S, 58 * S, 56 * S], 180, 360, fill=(120, 116, 124, 255)); d.rectangle([6 * S, 30 * S, 58 * S, 63 * S], fill=(120, 116, 124, 255))
    d.pieslice([12 * S, 10 * S, 52 * S, 50 * S], 180, 360, fill=(255, 244, 190, 255)); d.rectangle([12 * S, 30 * S, 52 * S, 63 * S], fill=(255, 244, 190, 255))
    d.pieslice([20 * S, 18 * S, 44 * S, 42 * S], 180, 360, fill=(255, 255, 240, 255)); d.rectangle([20 * S, 30 * S, 44 * S, 63 * S], fill=(255, 255, 240, 255))
    return finish(im, seed=6, edge=False)

# ---- personagens
def goblin_alive(frame):
    im = canvas(); d = ImageDraw.Draw(im)
    skin = (96, 152, 64, 255); skin_d = (64, 108, 44, 255); leather = (112, 76, 40, 255)
    lb = [(0, 0), (-3, -4)] if frame == 0 else ([(0, -4), (3, 0)] if frame == 1 else [(-3, 0), (3, 0)])
    for (x, off) in ((28, lb[0]), (36, lb[1])):
        lin(d, x + off[0], 46, x + off[0] * 1.4, 61 + off[1], 4.4, skin_d)
        ell(d, x + off[0] * 1.4 - 4, 59 + off[1], x + off[0] * 1.4 + 4, 63 + off[1], (60, 40, 24, 255))
    d.polygon(P([(22, 30), (42, 30), (40, 50), (24, 50)]), fill=leather)
    d.polygon(P([(22, 30), (28, 30), (27, 50), (24, 50)]), fill=(140, 98, 54, 255))
    d.rectangle([24 * S, 42 * S, 40 * S, 45 * S], fill=(52, 34, 20, 255)); d.rectangle([30 * S, 41 * S, 34 * S, 46 * S], fill=(210, 180, 70, 255))
    lin(d, 24, 33, 17, 45, 4, skin)
    if frame == 2:   # ataque: clava levantada
        lin(d, 40, 33, 46, 18, 4, skin); lin(d, 46, 18, 50, 2, 5, (92, 60, 32, 255)); ell(d, 46, -1, 56, 9, (110, 74, 40, 255))
        for (x, y) in ((50, 1), (53, 5), (49, 7)): ell(d, x - 1, y - 1, x + 1, y + 1, (190, 190, 200, 255))
    else:
        lin(d, 40, 33, 47, 45, 4, skin); lin(d, 47, 45, 53, 27, 4.6, (92, 60, 32, 255)); ell(d, 49, 20, 58, 31, (110, 74, 40, 255))
        for (x, y) in ((54, 23), (52, 28)): ell(d, x - 1, y - 1, x + 1, y + 1, (190, 190, 200, 255))
    d.polygon(P([(24, 16), (11, 12), (24, 23)]), fill=skin); d.polygon(P([(40, 16), (53, 12), (40, 23)]), fill=skin)
    ell(d, 23, 6, 41, 29, skin); ell(d, 30, 8, 41, 25, (112, 170, 76, 255))
    d.rectangle([25 * S, 14 * S, 39 * S, 16 * S], fill=skin_d)
    for x in (28, 36):
        ell(d, x - 2.4, 16, x + 2.4, 21.5, (250, 230, 60, 255)); ell(d, x - 0.8, 17.2, x + 0.8, 20.6, (10, 10, 10, 255))
    d.polygon(P([(31, 21), (33, 21), (32, 25)]), fill=skin_d)
    d.rectangle([27 * S, 25 * S, 37 * S, 26.5 * S], fill=(40, 20, 20, 255))
    d.polygon(P([(28, 25), (29.4, 25), (28.7, 22.8)]), fill=(240, 240, 230, 255)); d.polygon(P([(35, 25), (36.4, 25), (35.7, 22.8)]), fill=(240, 240, 230, 255))
    return im

def skeleton_alive(frame):
    im = canvas(); d = ImageDraw.Draw(im)
    bone = (226, 220, 198, 255); bone_d = (170, 164, 146, 255)
    lb = [(0, 0), (-2, -3)] if frame == 0 else ([(0, -3), (2, 0)] if frame == 1 else [(-2, 0), (2, 0)])
    for (x, off) in ((28, lb[0]), (36, lb[1])):
        lin(d, x, 48, x + off[0], 62 + off[1], 2.6, bone); ell(d, x - 1.6, 53, x + 1.6, 56, bone_d)
        lin(d, x + off[0] - 3, 62 + off[1], x + off[0] + 2, 62 + off[1], 2, bone)
    d.polygon(P([(25, 44), (39, 44), (37, 50), (27, 50)]), fill=bone)
    lin(d, 32, 22, 32, 45, 2.2, bone)
    for i, y in enumerate((27, 31, 35, 39)):
        w = 9 - i
        d.arc([(32 - w) * S, (y - 3) * S, (32 + w) * S, (y + 4) * S], 190, 350, fill=bone, width=int(1.6 * S))
        d.arc([(32 - w) * S, (y - 3) * S, (32 + w) * S, (y + 4) * S], 10, 170, fill=bone_d, width=int(1.2 * S))
    lin(d, 24, 26, 18, 38, 2, bone); lin(d, 18, 38, 17, 48, 2, bone)
    if frame == 2:
        lin(d, 40, 26, 46, 16, 2, bone); lin(d, 46, 16, 50, 8, 2, bone)
        d.polygon(P([(48, 9), (53, 9), (58, -6), (55, -8)]), fill=(168, 160, 150, 255)); d.polygon(P([(50, 8), (53, 9), (58, -6)]), fill=(140, 90, 52, 255))
        lin(d, 46, 10, 56, 11, 2, (110, 80, 40, 255))
    else:
        lin(d, 40, 26, 46, 36, 2, bone); lin(d, 46, 36, 47, 46, 2, bone)
        d.polygon(P([(45, 44), (49, 44), (50, 14), (47, 11)]), fill=(168, 160, 150, 255)); d.polygon(P([(47, 44), (49, 44), (50, 14), (47, 11)]), fill=(140, 90, 52, 255))
        lin(d, 43, 43, 52, 43, 2, (110, 80, 40, 255))
    ell(d, 23, 4, 41, 23, bone); d.rectangle([26 * S, 18 * S, 38 * S, 26 * S], fill=bone); ell(d, 31, 10, 41, 21, bone_d)
    ell(d, 24, 4, 40, 22, bone)
    for x in (28, 36): ell(d, x - 2.8, 11, x + 2.8, 17, (14, 8, 10, 255)); ell(d, x - 0.8, 13, x + 0.8, 14.6, (210, 40, 30, 255))
    d.polygon(P([(31, 18), (33, 18), (32, 21)]), fill=(20, 12, 12, 255))
    for x in range(27, 38, 2): d.rectangle([x * S, 23 * S, (x + 1) * S, 26 * S], fill=(236, 232, 214, 255))
    d.line(P([(27, 23), (37, 23)]), fill=(30, 20, 20, 255), width=S // 2)
    return im

def dead_version(im, seed):
    r = im.rotate(90, resample=Image.BICUBIC, expand=False)
    bb = r.getbbox()
    out = Image.new("RGBA", im.size, (0, 0, 0, 0))
    r = r.crop(bb); out.paste(r, (bb[0], 62 * S - r.size[1]), r)
    a = np.array(out).astype(float); a[..., :3] *= 0.8
    return Image.fromarray(a.astype(np.uint8), "RGBA")

def sprite_goblin(frame):
    if frame == 3: return finish(dead_version(goblin_alive(0), 1), seed=13)
    return finish(goblin_alive(frame), seed=10 + frame)

def sprite_skel(frame):
    if frame == 3: return finish(dead_version(skeleton_alive(0), 2), seed=23)
    return finish(skeleton_alive(frame), seed=20 + frame)

def sprite_npc(var):
    im = canvas(); d = ImageDraw.Draw(im)
    tunic = [(60, 98, 160, 255), (160, 64, 70, 255)][var]; tunic_d = [(40, 70, 120, 255), (120, 44, 50, 255)][var]
    hair = [(70, 44, 24, 255), (210, 170, 70, 255)][var]; skin = (226, 178, 142, 255)
    for x in (28, 36): d.rectangle([(x - 3) * S, 50 * S, (x + 3) * S, 61 * S], fill=(70, 52, 40, 255)); ell(d, x - 4, 58, x + 4, 63, (40, 28, 20, 255))
    if var == 1: d.polygon(P([(21, 26), (43, 26), (47, 56), (17, 56)]), fill=tunic)
    else: d.polygon(P([(22, 26), (42, 26), (41, 54), (23, 54)]), fill=tunic)
    d.polygon(P([(22, 26), (32, 26), (32, 54), (23, 54)]), fill=tunic_d)
    d.rectangle([24 * S, 40 * S, 40 * S, 43 * S], fill=(60, 40, 24, 255))
    if var == 1: d.polygon(P([(26, 36), (38, 36), (40, 56), (24, 56)]), fill=(236, 230, 214, 255))
    lin(d, 22, 28, 18, 44, 4, tunic); lin(d, 42, 28, 46, 44, 4, tunic)
    ell(d, 15.5, 42, 20.5, 47, skin); ell(d, 43.5, 42, 48.5, 47, skin)
    ell(d, 24, 5, 40, 26, skin)
    if var == 0:
        d.pieslice([23 * S, 3 * S, 41 * S, 21 * S], 180, 360, fill=hair); d.rectangle([23 * S, 11 * S, 26 * S, 20 * S], fill=hair); d.rectangle([38 * S, 11 * S, 41 * S, 20 * S], fill=hair)
    else:
        d.pieslice([22 * S, 3 * S, 42 * S, 22 * S], 180, 360, fill=hair); d.rectangle([22 * S, 12 * S, 26 * S, 32 * S], fill=hair); d.rectangle([38 * S, 12 * S, 42 * S, 32 * S], fill=hair)
        d.polygon(P([(14, 11), (50, 11), (40, 5), (24, 5)]), fill=(214, 184, 96, 255)); d.rectangle([20 * S, 9 * S, 44 * S, 12 * S], fill=(176, 140, 64, 255))
    for x in (28.5, 35.5): ell(d, x - 1.2, 16, x + 1.2, 18.6, (30, 24, 20, 255))
    d.arc([28 * S, 18 * S, 36 * S, 24 * S], 20, 160, fill=(150, 60, 50, 255), width=S)
    return finish(im, seed=30 + var)

def sprite_chest(opened):
    im = canvas(32, 32); d = ImageDraw.Draw(im)
    wood = (120, 78, 40, 255); wood_d = (84, 52, 26, 255); gold = (232, 192, 64, 255)
    d.rectangle([3 * S, 14 * S, 29 * S, 29 * S], fill=wood); d.rectangle([3 * S, 24 * S, 29 * S, 29 * S], fill=wood_d)
    d.rectangle([3 * S, 14 * S, 5 * S, 29 * S], fill=gold); d.rectangle([27 * S, 14 * S, 29 * S, 29 * S], fill=gold)
    if not opened:
        d.pieslice([3 * S, 4 * S, 29 * S, 24 * S], 180, 360, fill=wood); d.rectangle([3 * S, 14 * S, 29 * S, 15 * S], fill=gold)
        d.arc([3 * S, 4 * S, 29 * S, 24 * S], 180, 360, fill=gold, width=S)
        d.rectangle([14 * S, 13 * S, 18 * S, 19 * S], fill=gold); d.rectangle([15.2 * S, 15 * S, 16.8 * S, 17 * S], fill=(40, 24, 12, 255))
    else:
        d.rectangle([4 * S, 14 * S, 28 * S, 19 * S], fill=(250, 220, 90, 255))
        for (x, y) in ((9, 14), (16, 13), (22, 14)): ell(d, x - 2, y - 2, x + 2, y + 2, (255, 240, 130, 255))
        d.polygon(P([(3, 14), (29, 14), (27, 3), (5, 3)]), fill=wood_d); d.polygon(P([(3, 14), (5, 14), (6, 3), (5, 3)]), fill=gold)
    return finish(im, 32, 32, seed=40 + opened)

# ---- armas em primeira pessoa
def ellpoly(cx, cy, rx, ry, n=24):
    return [(cx + math.cos(a * 2 * math.pi / n) * rx, cy + math.sin(a * 2 * math.pi / n) * ry) for a in range(n)]

def xf(pts, ang, pivot, off):
    ca, sa = math.cos(math.radians(ang)), math.sin(math.radians(ang))
    return [(off[0] + (x - pivot[0]) * ca - (y - pivot[1]) * sa, off[1] + (x - pivot[0]) * sa + (y - pivot[1]) * ca) for x, y in pts]

def sprite_sword(ang, hand):
    im = canvas(); d = ImageDraw.Draw(im)
    piv = (0, 7)
    def poly(pts, col): d.polygon(P(xf(pts, ang, piv, hand)), fill=col)
    # antebraco (manga de couro + bracal)
    ca, sa = math.cos(math.radians(ang)), math.sin(math.radians(ang))
    ax, ay = hand
    bx, by = ax + 34, ay + 40
    nx, ny = -(by - ay), (bx - ax); L = math.hypot(nx, ny); nx, ny = nx / L * 7.5, ny / L * 7.5
    d.polygon(P([(ax + nx, ay + ny), (ax - nx, ay - ny), (bx - nx * 1.4, by - ny * 1.4), (bx + nx * 1.4, by + ny * 1.4)]), fill=(96, 64, 44, 255))
    d.polygon(P([(ax + nx * .5, ay + ny * .5), (ax - nx * .5, ay - ny * .5), (bx - nx * 1.4, by - ny * 1.4), (bx, by)]), fill=(70, 46, 32, 255))
    mx, my = ax + 9, ay + 10
    d.polygon(P([(mx + nx * 1.05, my + ny * 1.05), (mx - nx * 1.05, my - ny * 1.05), (mx + 7 - nx * 1.1, my + 8 - ny * 1.1), (mx + 7 + nx * 1.1, my + 8 + ny * 1.1)]), fill=(150, 150, 160, 255))
    # lamina
    poly([(-3.6, 0), (3.6, 0), (3.1, -33), (0, -41), (-3.1, -33)], (176, 182, 194, 255))
    poly([(-3.6, 0), (0, 0), (0, -41), (-3.1, -33)], (214, 220, 232, 255))
    poly([(-0.5, -2), (0.5, -2), (0.5, -36), (-0.5, -36)], (130, 136, 150, 255))
    poly([(-10, 0), (10, 0), (10, 3.2), (-10, 3.2)], (216, 176, 60, 255))
    poly([(-10, 0), (10, 0), (10, 1.2), (-10, 1.2)], (250, 224, 120, 255))
    poly([(-2, 3), (2, 3), (2, 13), (-2, 13)], (80, 50, 30, 255))
    poly(ellpoly(0, 14.5, 3, 3), (216, 176, 60, 255))
    # mao
    d.polygon(P(xf(ellpoly(0, 8, 5.6, 5.0), ang, piv, hand)), fill=(214, 164, 128, 255))
    for k in range(4):
        d.polygon(P(xf(ellpoly(-3.6 + k * 2.4, 4.4, 1.5, 1.7, 10), ang, piv, hand)), fill=(232, 184, 148, 255))
        d.line(P(xf([(-3.6 + k * 2.4, 6.0), (-3.6 + k * 2.4, 10.0)], ang, piv, hand)), fill=(160, 112, 84, 255), width=S // 2)
    return finish(im, seed=60, edge=False)

def sprite_fist(center, r):
    im = canvas(); d = ImageDraw.Draw(im)
    cx, cy = center
    ax, ay = cx, cy
    bx, by = cx + 30, cy + 44
    nx, ny = -(by - ay), (bx - ax); L = math.hypot(nx, ny); nx, ny = nx / L * r * 0.95, ny / L * r * 0.95
    d.polygon(P([(ax + nx, ay + ny), (ax - nx, ay - ny), (bx - nx * 1.5, by - ny * 1.5), (bx + nx * 1.5, by + ny * 1.5)]), fill=(96, 64, 44, 255))
    d.polygon(P([(ax + nx * .4, ay + ny * .4), (ax - nx * .4, ay - ny * .4), (bx - nx * 1.5, by - ny * 1.5), (bx, by)]), fill=(70, 46, 32, 255))
    skin = (214, 164, 128, 255); skin_h = (236, 190, 156, 255); skin_d = (168, 118, 88, 255)
    d.polygon(P(ellpoly(cx, cy + r * .1, r * 1.0, r * 0.9)), fill=skin)
    for k in range(4):
        fx = cx - r * 0.78 + k * r * 0.52
        d.polygon(P(ellpoly(fx, cy - r * 0.55, r * 0.30, r * 0.34, 12)), fill=skin_h)
        d.line(P([(fx - r * .26, cy - r * 0.2), (fx + r * .26, cy - r * 0.2)]), fill=skin_d, width=max(1, S // 2))
    d.polygon(P(ellpoly(cx + r * 0.95, cy + r * 0.25, r * 0.36, r * 0.52, 12)), fill=skin)
    d.polygon(P(ellpoly(cx - r * 0.1, cy + r * 0.45, r * 0.9, r * 0.4, 14)), fill=skin_d)
    return finish(im, seed=70, edge=False)

# ---- logo
def make_logo():
    W, H = 256, 64
    big = Image.new("RGBA", (W * 2, H * 2), (0, 0, 0, 0)); d = ImageDraw.Draw(big)
    f1 = None
    for path in ("/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf", "DejaVuSerif-Bold.ttf"):
        try: f1 = ImageFont.truetype(path, 66); f2 = ImageFont.truetype(path, 24); break
        except Exception: pass
    if f1 is None: f1 = ImageFont.load_default(); f2 = f1
    t = "DAGGERFALL"
    w = d.textlength(t, font=f1)
    x = (W * 2 - w) / 2
    d.text((x, 6), t, font=f1, fill=(255, 255, 255, 255), stroke_width=5, stroke_fill=(30, 10, 6, 255))
    sub = "- DS  TESTE TECNICO -"; w2 = d.textlength(sub, font=f2)
    d.text(((W * 2 - w2) / 2, 92), sub, font=f2, fill=(236, 220, 170, 255), stroke_width=3, stroke_fill=(30, 10, 6, 255))
    arr = np.array(big).astype(float)
    yy = np.linspace(0, 1, H * 2)[:, None]
    white = (arr[..., 0] > 200) & (arr[..., 3] > 0) & (arr[..., 2] > 150)
    grad = np.stack([255 - yy * 55, 214 - yy * 80, 100 - yy * 70], -1) * np.ones((1, W * 2, 1))
    arr[white, :3] = grad[white]
    big = Image.fromarray(arr.astype(np.uint8), "RGBA")
    img = big.resize((W, H), Image.LANCZOS)
    a = np.array(img); a[..., 3] = np.where(a[..., 3] >= 100, 255, 0)
    return Image.fromarray(a, "RGBA")

# ------------------------------------------------------------------ lista mestre (ordem = enum TEX_*)
ASSETS = [
    # nome, funcao, wrap (1 = repete)
    ("GRASS",  lambda: tex_grass(1, False), 1), ("GRASS2", lambda: tex_grass(7, True), 1), ("DIRT", tex_dirt, 1),
    ("ROCK", tex_rock, 1), ("SAND", tex_sand, 1), ("SNOW", tex_snow, 1), ("COBBLE", tex_cobble, 1), ("WATER", tex_water, 1),
    ("WALL", tex_wall, 1), ("ROOF", tex_roof, 1), ("DOOR", tex_door, 0),
    ("DG_WALL", tex_dg_wall, 1), ("DG_FLOOR", tex_dg_floor, 1), ("DG_CEIL", tex_dg_ceil, 1), ("DG_DOOR", tex_dg_door, 0),
    ("TREE_PINE", sprite_tree_pine, 0), ("TREE_OAK", sprite_tree_oak, 0), ("TREE_DEAD", sprite_tree_dead, 0),
    ("HOUSE_FAR", sprite_house_far, 0), ("DUNGEON_FAR", sprite_dungeon_far, 0), ("EXIT", sprite_exit, 0),
    ("GOBLIN0", lambda: sprite_goblin(0), 0), ("GOBLIN1", lambda: sprite_goblin(1), 0), ("GOBLIN2", lambda: sprite_goblin(2), 0), ("GOBLIN3", lambda: sprite_goblin(3), 0),
    ("SKEL0", lambda: sprite_skel(0), 0), ("SKEL1", lambda: sprite_skel(1), 0), ("SKEL2", lambda: sprite_skel(2), 0), ("SKEL3", lambda: sprite_skel(3), 0),
    ("NPC0", lambda: sprite_npc(0), 0), ("NPC1", lambda: sprite_npc(1), 0),
    ("CHEST0", lambda: sprite_chest(0), 0), ("CHEST1", lambda: sprite_chest(1), 0),
    ("SWORD0", lambda: sprite_sword(16, (45, 58)), 0), ("SWORD1", lambda: sprite_sword(34, (41, 60)), 0),
    ("SWORD2", lambda: sprite_sword(-8, (35, 58)), 0), ("SWORD3", lambda: sprite_sword(-52, (28, 62)), 0),
    ("FIST0", lambda: sprite_fist((44, 52), 11), 0), ("FIST1", lambda: sprite_fist((52, 58), 11), 0), ("FIST2", lambda: sprite_fist((30, 34), 14), 0),
    ("LOGO", make_logo, 0),
]

def rgb555(img, alpha_bit):
    a = np.array(img.convert("RGBA"))
    r = a[..., 0] >> 3; g = a[..., 1] >> 3; b = a[..., 2] >> 3
    v = (r.astype(np.uint16) | (g.astype(np.uint16) << 5) | (b.astype(np.uint16) << 10))
    op = (a[..., 3] >= 128) if alpha_bit else np.ones(a.shape[:2], bool)
    v = np.where(op, v | 0x8000, 0).astype(np.uint16)
    return v

def main():
    from_png = "--from-png" in sys.argv
    sheet_items = []
    out_c = ['// GERADO por tools/gen_assets.py - nao edite a mao.', '#include "gfx_data.h"', '']
    defs = []
    for name, fn, wrap in ASSETS:
        path = os.path.join(GFX, name.lower() + ".png")
        if from_png and os.path.exists(path): img = Image.open(path).convert("RGBA")
        else: img = fn(); img.save(path)
        w, h = img.size
        v = rgb555(img, alpha_bit=True)
        sheet_items.append((name, img))
        words = ",".join("0x%04X" % x for x in v.flatten())
        out_c.append("static const unsigned short d_%s[%d] __attribute__((aligned(4))) = {%s};" % (name.lower(), w * h, words))
        defs.append("    {d_%s, %d, %d, %d}, /* TEX_%s */" % (name.lower(), w, h, wrap, name))
    out_c.append("\nconst TexDef gTexDefs[TEX_COUNT] = {\n" + "\n".join(defs) + "\n};\n")
    with open(os.path.join(SRC, "gfx_data.c"), "w") as f: f.write("\n".join(out_c))
    hdr = ["// GERADO por tools/gen_assets.py", "#ifndef GFX_DATA_H", "#define GFX_DATA_H",
           "typedef struct { const unsigned short* data; int w, h; int wrap; } TexDef;", "enum {"]
    hdr += ["    TEX_%s," % n for n, _, _ in ASSETS] + ["    TEX_COUNT", "};", "extern const TexDef gTexDefs[TEX_COUNT];", "#endif", ""]
    with open(os.path.join(SRC, "gfx_data.h"), "w") as f: f.write("\n".join(hdr))
    # folha de contato (fundo xadrez p/ ver transparencia)
    cell = 72; cols = 10; rows = (len(sheet_items) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * cell, rows * cell), (70, 90, 110, 255))
    for i, (n, im) in enumerate(sheet_items):
        x, y = (i % cols) * cell + 4, (i // cols) * cell + 4
        im2 = im if im.size[0] <= 64 and im.size[1] <= 64 else im.resize((64, 16), Image.NEAREST)
        sheet.alpha_composite(im2, (x, y))
    sheet.resize((sheet.size[0] * 2, sheet.size[1] * 2), Image.NEAREST).save(os.path.join(GFX, "_preview.png"))
    total = sum(i.size[0] * i.size[1] * 2 for _, i in sheet_items)
    print("OK: %d texturas, %.0f KB de VRAM" % (len(sheet_items), total / 1024))

if __name__ == "__main__":
    main()
