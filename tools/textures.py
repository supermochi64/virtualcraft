"""Virtualcraft textures: 16 x 16 pixel art in the VB's 4 shades (0 black .. 3 bright), our own, in the spirit of the
genre. Side faces near the camera are drawn from pre-scaled columns: for every texture variant, texel column u and
on-screen height h (1..128; taller columns: src/render.c tex_col doubles the rows of the half height) the 2-bit words of that column -> build/tex.bin, build/tex.h.

    python3 tools/textures.py [--png build/textures.png]   (needs numpy; Pillow for --png)
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HMAX = 128
rng = np.random.default_rng(7)


def noise(p):
    return rng.random((16, 16)) < p


def dirt():
    t = np.ones((16, 16), int)
    t[noise(0.28)] = 2
    t[noise(0.06)] = 0
    return t


def grass_side():
    t = dirt()
    lip = [3, 4, 3, 2, 3, 5, 4, 3, 2, 3, 4, 3, 5, 3, 2, 3]
    for u in range(16):
        for v in range(lip[u]):
            t[v, u] = 3 if (v < lip[u] - 1 or (u + v) % 3) else 2
        t[lip[u], u] = 1 if t[lip[u], u] else 0
    return t


def stone():
    t = np.full((16, 16), 2)
    t[noise(0.12)] = 3
    for _ in range(5):                                # cracks
        y, x = rng.integers(0, 16, 2)
        for k in range(rng.integers(3, 6)):
            t[(y + k // 2) % 16, (x + k) % 16] = 1
    return t


def cobble():
    t = np.full((16, 16), 2)
    stones = [(0, 0, 6, 5), (0, 6, 5, 16), (5, 0, 10, 7), (5, 7, 11, 12), (5, 12, 10, 16), (10, 0, 16, 5), (11, 5, 16, 11), (10, 11, 16, 16)]
    for i, (y0, x0, y1, x1) in enumerate(stones):
        t[y0:y1, x0:x1] = 2 if i % 2 else 3
        t[y0, x0:x1] = 1
        t[y0:y1, x0] = 1
        if y1 - y0 > 2 and x1 - x0 > 2:
            t[y0 + 1, x0 + 1] = 3
            t[y1 - 1, x0 + 1:x1] = 1 if i % 3 else t[y1 - 1, x0 + 1:x1]
    t[noise(0.05)] = 1
    return t


def planks():
    t = np.full((16, 16), 2)
    for v in range(16):
        if v % 4 == 3:
            t[v, :] = 1
    joints = {0: 11, 1: 4, 2: 13, 3: 7}
    for b, x in joints.items():
        t[b * 4:b * 4 + 3, x] = 1
    g = noise(0.10)
    t[(t == 2) & g] = 3
    t[0::4, :][t[0::4, :] == 2] = 3                   # each board's lit top edge
    return t


def log_side():
    t = np.full((16, 16), 1)
    for u in range(16):
        if u % 4 == 1:
            t[:, u] = 2
        if u % 4 == 3:
            t[:, u] = 0
    t[noise(0.12)] = 2
    t[noise(0.05)] = 0
    return t


def log_top():
    t = np.full((16, 16), 2)
    for v in range(16):
        for u in range(16):
            r = max(abs(v - 7.5), abs(u - 7.5))
            if r > 6.5:
                t[v, u] = 1
            elif int(r) % 3 == 0:
                t[v, u] = 3
    return t


def brick():
    t = np.full((16, 16), 1)
    t[noise(0.2) & (t == 1)] = 2
    for v in range(16):
        if v % 4 == 3:
            t[v, :] = 3
    for row in range(4):
        off = 0 if row % 2 == 0 else 4
        for x in range(off, 16, 8):
            t[row * 4:row * 4 + 3, x] = 3
    return t


def sand():
    t = np.full((16, 16), 3)
    t[noise(0.18)] = 2
    t[noise(0.03)] = 1
    return t


def gold():
    t = stone()
    for y, x in [(3, 3), (2, 10), (9, 6), (11, 12), (13, 3)]:
        for dy, dx in [(0, 0), (0, 1), (1, 0), (1, 1), (0, 2)]:
            if 0 <= y + dy < 16 and 0 <= x + dx < 16:
                t[y + dy, x + dx] = 3
        if y + 2 < 16:
            t[y + 2, x:x + 2] = 0
    return t


def lamp():
    t = np.full((16, 16), 3)
    t[0, :] = t[15, :] = t[:, 0] = t[:, 15] = 1
    t[7:9, 2:14] = 2
    t[2:14, 7:9] = 2
    t[1, 1:15] = 2
    return t


def wool():
    t = np.full((16, 16), 3)
    for v in range(16):
        for u in range(16):
            if (u + (v // 2)) % 4 == 0:
                t[v, u] = 2
    return t


def bedrock():
    t = np.ones((16, 16), int)
    t[noise(0.3)] = 0
    t[noise(0.15)] = 2
    return t


def flower():
    """a plant on a see-through ground (shade 0 is transparent for plants): stem, leaves, a bright bloom"""
    t = np.zeros((16, 16), int)
    t[7:16, 7] = 1
    t[11, 5:7] = 2; t[10, 4] = 2; t[13, 8:10] = 2; t[12, 10] = 2
    bloom = [".3..3.", "332233", ".2332.", ".2332.", "332233", ".3..3."]
    for y, row in enumerate(bloom):
        for x, ch in enumerate(row):
            if ch != ".":
                t[1 + y, 5 + x] = int(ch)
    return t


def tall_grass():
    """blades of grass that lean and taper: bright tips, mid stems, a few dark ones behind for depth"""
    t = np.zeros((16, 16), int)
    blades = [(1, 6, 1, 2), (2, 2, 0, 3), (4, 8, -1, 2), (5, 4, 1, 3), (7, 1, 0, 3), (8, 6, -1, 1), (9, 3, 1, 3),
              (11, 7, 0, 2), (12, 2, -1, 3), (14, 5, 0, 2), (6, 9, 0, 1), (13, 9, 1, 1)]
    for x0, top, lean, sh in blades:
        for y in range(top, 16):
            x = x0 + (lean if y < top + (16 - top) // 3 else 0)
            if 0 <= x < 16:
                t[y, x] = sh if (y > top or sh == 1) else 3
    return t


def sword():
    """a sword held up diagonally: bright blade with a dark edge, a cross guard, a dark grip (see-through ground)"""
    t = np.zeros((16, 16), int)
    for i in range(10):                               # the blade from the tip (top right) down to the guard
        x, y = 13 - i, 2 + i
        t[y, x] = 3
        t[y - 1, x] = 2
        if x + 1 < 16:
            t[y, x + 1] = 1
    for x, y in [(2, 9), (3, 10), (4, 11), (5, 12), (6, 13)]:   # the guard
        t[y, x] = 2
    for x, y in [(3, 13), (2, 14), (1, 15)]:          # the grip
        t[y, x] = 1
    t[14, 1] = 2
    return t


# block id (src/game.h) -> side texture
SIDES = {1: grass_side(), 2: dirt(), 3: stone(), 4: cobble(), 5: planks(), 6: log_side(), 9: brick(), 10: sand(),
         12: bedrock(), 13: lamp(), 14: gold(), 15: wool(), 16: flower(), 17: tall_grass(), 18: sword()}
PLANTS = (16, 17, 18)
SHADE = np.array([0, 1, 1, 2])            # x faces: one step darker (Minecraft's light from one side)


def edge(t):
    """a faint dark rim on the left and bottom texels: block edges read as a grid without an outline pass"""
    t = t.copy()
    t[15, :] = np.minimum(t[15, :], 1)
    t[:, 0] = np.where(t[:, 0] >= 2, t[:, 0] - 1, t[:, 0])
    return t


def columns(t):
    """t: 16 x 16 shades -> for u, h: the words (row 0 in the low bits), + a zero word"""
    per_h = []
    sh = (2 * np.arange(16)).astype(np.uint64)
    for h in range(1, HMAX + 1):
        nw = (h + 15) // 16
        px = np.zeros((nw * 16, 16), np.uint64)
        px[:h] = t[(np.arange(h) * 16) // h, :]
        w = (px.reshape(nw, 16, 16) << sh[None, :, None]).sum(axis=1)       # nw x 16 (u)
        per_h.append(np.vstack([w, np.zeros((1, 16), np.uint64)]))
    out = []
    for u in range(16):
        for blk in per_h:
            out.append(blk[:, u])
    return np.concatenate(out).astype(np.uint32)


def main():
    variants, index = [], {}
    for blk in range(32):
        if blk not in SIDES:
            continue
        lit = SIDES[blk] if blk in PLANTS else edge(SIDES[blk])
        index[blk] = len(variants)
        variants.append(lit)
        variants.append(SHADE[lit])
    cols = [columns(t) for t in variants]
    per = len(cols[0])
    arr = np.concatenate([np.zeros(1, np.uint32)] + cols)      # (a leading zero: a column's word -1 reads 0)
    arr.tofile(os.path.join(HERE, "build", "tex.bin"))
    L = ["/* made by tools/textures.py */", "#define TEX_PER %d   /* words a variant (16 texel columns) */" % per,
         "#define TEX_HMAX %d" % HMAX,
         "static const s8 tex_var[32] = {%s};   /* block -> its lit side variant (+1: the shaded one), -1 none */"
         % ",".join(str(index.get(b, -1)) for b in range(32))]
    open(os.path.join(HERE, "build", "tex.h"), "w").write("\n".join(L) + "\n")
    print("tex.bin %.2f MB, %d variants" % (arr.nbytes / 1e6, len(variants)))
    if "--png" in sys.argv:
        from PIL import Image
        lv = np.array([0, 91, 171, 254], np.uint8)
        sheet = np.zeros((16, 16 * len(variants)), np.uint8)
        for i, t in enumerate(variants):
            sheet[:, i * 16:(i + 1) * 16] = lv[t]
        img = np.zeros((16, sheet.shape[1], 3), np.uint8)
        img[..., 0] = sheet
        Image.fromarray(img).resize((sheet.shape[1] * 8, 128), Image.NEAREST).save(sys.argv[sys.argv.index("--png") + 1])


if __name__ == "__main__":
    main()
