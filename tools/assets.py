"""Virtualcraft assets -> build/assets.h: the pixel font, the PCM sounds (Minetest Game's, CC BY-SA 3.0 / CC BY 3.0 / CC0,
see docs/CREDITS.md), the PCM level table, the music (an original piece, written here as notes).

    python3 tools/assets.py   (needs numpy, ffmpeg and minetest_game cloned into ext/mtg)
"""
import math
import os
import subprocess

import numpy as np

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SND = os.path.join(HERE, "ext", "mtg", "mods", "default", "sounds")
OUT = os.path.join(HERE, "build", "sound_data.h")
RATE = 10000

# ---- the font: 5 x 7, our own
FONT = {
    " ": [], "!": ["..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."],
    "'": ["..#..", "..#..", "....."], "(": ["...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."],
    ")": [".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."], "+": [".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."],
    ",": [".....", ".....", ".....", ".....", ".....", "..#..", ".#..."], "-": [".....", ".....", ".....", "#####", ".....", ".....", "....."],
    ".": [".....", ".....", ".....", ".....", ".....", ".....", "..#.."], "/": ["....#", "....#", "...#.", "..#..", ".#...", "#....", "#...."],
    "0": [".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."], "1": ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "2": [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"], "3": ["####.", "....#", "....#", ".###.", "....#", "....#", "####."],
    "4": ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."], "5": ["#####", "#....", "####.", "....#", "....#", "#...#", ".###."],
    "6": [".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."], "7": ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."],
    "8": [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."], "9": [".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."],
    ":": [".....", "..#..", ".....", ".....", ".....", "..#..", "....."], "=": [".....", ".....", "#####", ".....", "#####", ".....", "....."],
    "?": [".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."],
    "A": [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"], "B": ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
    "C": [".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."], "D": ["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."],
    "E": ["#####", "#....", "#....", "####.", "#....", "#....", "#####"], "F": ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
    "G": [".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"], "H": ["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "I": [".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."], "J": ["..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."],
    "K": ["#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"], "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    "M": ["#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"], "N": ["#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."], "P": ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
    "Q": [".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"], "R": ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    "S": [".####", "#....", "#....", ".###.", "....#", "....#", "####."], "T": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    "U": ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."], "V": ["#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."],
    "W": ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"], "X": ["#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"],
    "Y": ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."], "Z": ["#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"],
}


def font_rows():
    out = []
    for ch in range(32, 96):
        g = FONT.get(chr(ch), [])
        cols = []
        for c in range(5):
            b = 0
            for r, row in enumerate(g):
                if c < len(row) and row[c] == "#":
                    b |= 1 << r
            cols.append(b)
        out.append(cols)
    return out


# ---- sounds
SOUNDS = [  # (the enum's order in src/game.h), file, gain, max seconds
    ("dig_dirt", "default_dig_crumbly.ogg", 1.0, 0.35), ("dig_stone", "default_dig_cracky.1.ogg", 1.0, 0.3),
    ("dig_wood", "default_dig_choppy.1.ogg", 1.0, 0.3), ("dig_sand", "default_gravel_dig.1.ogg", 1.0, 0.3),
    ("dig_leaves", "default_dig_snappy.ogg", 1.0, 0.3), ("dug", "default_dug_node.1.ogg", 1.0, 0.45),
    ("place", "default_place_node.1.ogg", 1.0, 0.3), ("place_hard", "default_place_node_hard.1.ogg", 1.0, 0.3),
    ("glass", "default_break_glass.1.ogg", 1.0, 0.6), ("step_grass", "default_grass_footstep.1.ogg", 0.6, 0.3),
    ("step_stone", "default_hard_footstep.1.ogg", 0.6, 0.25), ("step_wood", "default_wood_footstep.1.ogg", 0.6, 0.3),
    ("step_sand", "default_sand_footstep.1.ogg", 0.6, 0.3), ("step_water", "default_water_footstep.1.ogg", 0.6, 0.4),
    ("hurt", "player_damage.ogg", 1.0, 0.4),
]
SYNTH = ["squeal", "groan", "swish", "cluck", "hiss", "boom"]          # our own, made below (the enum continues in src/game.h)


def synth(name):
    t = np.arange(int(RATE * {"squeal": 0.32, "groan": 0.55, "swish": 0.14, "cluck": 0.12, "hiss": 1.5, "boom": 0.9}[name])) / RATE
    rng = np.random.default_rng(5)
    if name == "squeal":                              # an animal's squeal: a rising, wobbling nasal tone
        f = 600 + 900 * t / t[-1] + 60 * np.sin(2 * np.pi * 18 * t)
        x = np.sign(np.sin(2 * np.pi * np.cumsum(f) / RATE)) * 0.6 + np.sin(4 * np.pi * np.cumsum(f) / RATE) * 0.4
        env = np.minimum(1, t / 0.02) * np.exp(-t * 5)
    elif name == "groan":                             # a zombie's groan: a low buzzing tone that sags, with breath
        f = 140 - 50 * t / t[-1]
        ph = 2 * np.pi * np.cumsum(f) / RATE
        x = np.sign(np.sin(ph)) * 0.5 + np.sin(2 * ph) * 0.3 + rng.normal(0, 0.25, len(t))
        env = np.minimum(1, t / 0.06) * np.minimum(1, (t[-1] - t) / 0.15)
    elif name == "swish":                             # the sword through the air: a noise sweep
        x = rng.normal(0, 1, len(t))
        x = np.convolve(x, np.ones(3) / 3, "same")
        env = np.sin(np.pi * t / t[-1]) ** 2
    elif name == "hiss":                              # a boomcap's fuse: a hiss that swells and sharpens
        x = rng.normal(0, 1, len(t))
        x = x - np.convolve(x, np.ones(4) / 4, "same") * (1 - t / t[-1])
        env = (0.25 + 0.75 * t / t[-1]) * np.minimum(1, t / 0.05) * (1 + 0.3 * np.sin(2 * np.pi * 9 * t))
    elif name == "boom":                              # the burst: a crack, then low rumbling noise dying away
        x = rng.normal(0, 1, len(t))
        k = 1 + (t * 60).astype(int)
        y = np.zeros_like(x); acc = 0.0
        for i in range(len(x)):
            a = 1.0 / k[i]; acc += (x[i] - acc) * a; y[i] = acc
        x = y / max(1e-6, np.abs(y).max()) + np.sin(2 * np.pi * 55 * t) * 0.5
        env = np.exp(-t * 4.5) * np.minimum(1, t / 0.004)
    else:                                             # a chicken's cluck: two short knocks
        x = np.sin(2 * np.pi * 900 * t) * (np.sin(2 * np.pi * 40 * t) > 0)
        env = np.exp(-t * 18)
    return x * env


def load(fn, secs):
    raw = subprocess.run(["ffmpeg", "-v", "quiet", "-i", os.path.join(SND, fn), "-ac", "1", "-ar", str(RATE), "-f", "f32le", "-"],
                         capture_output=True, check=True).stdout
    x = np.frombuffer(raw, np.float32).astype(np.float64)
    nz = np.nonzero(np.abs(x) > 0.02 * np.abs(x).max())[0]
    x = x[max(0, nz[0] - 10):]
    x = x[: int(secs * RATE)]
    fade = min(len(x), int(0.05 * RATE))
    x[-fade:] *= np.linspace(1, 0, fade)
    x -= np.mean(x)
    return x


def to_s8(x, peak, gain):
    x = x / (np.abs(x).max() + 1e-9) * peak * gain
    return [int(v) for v in np.clip(np.round(x), -127, 127)]


def level_pairs():
    """the 29 loudness levels: (L * E >> 3) + 1 for L, E in 0..15 (both nibbles of SxLRV the same)"""
    best = {}
    for L in range(16):
        for E in range(16):
            a = ((L * E) >> 3) + 1
            if a not in best:
                best[a] = (L, E)
    return best


def level_table():
    pairs = level_pairs()
    t = []
    for s in range(-384, 384):
        a = int(round(14.5 + 14.5 * math.tanh(s / 140.0)))
        a = min(pairs, key=lambda k: abs(k - a))
        L, E = pairs[a]
        t.append((L << 4) | L | (E << 12))
    return t


# ---- the music: an original piece in D, 4/4, a beat = 44 frames (68 a minute)
BEAT = 44
NOTE = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}


def hz(n):
    name, octv = n[:-1], int(n[-1])
    m = NOTE[name] + 12 * (octv + 1)
    return 440.0 * 2 ** ((m - 69) / 12)


def vsu_fq(f):
    return max(0, min(2047, int(round(2048 - 5e6 / (32 * f)))))


CHORDS = {  # arpeggio notes (low to high), bass
    "Dmaj7": (["D4", "A4", "C#5", "F#5"], "D3"), "Bm7": (["B3", "F#4", "A4", "D5"], "B2"),
    "Gmaj7": (["G3", "D4", "F#4", "B4"], "G2"), "A6": (["A3", "E4", "F#4", "C#5"], "A2"),
    "F#m7": (["F#3", "C#4", "E4", "A4"], "F#2"), "Asus": (["A3", "D4", "E4", "A4"], "A2"),
    "Em7": (["E3", "B3", "D4", "G4"], "E2"), "D/F#": (["F#3", "A3", "D4", "F#4"], "F#2"),
}
PROG = ["Dmaj7", "Bm7", "Gmaj7", "A6", "Dmaj7", "F#m7", "Gmaj7", "Asus",
        "Em7", "D/F#", "Gmaj7", "A6", "Bm7", "F#m7", "Gmaj7", "Asus"]
# the tune: (beat in the bar, note, beats long) per bar of the A and B parts
MEL_A = [[(0, "F#5", 2), (2, "E5", 1), (3, "D5", 1)], [(0, "A4", 3)], [(0, "B4", 1.5), (1.5, "D5", 0.5), (2, "E5", 2)],
         [(0, "C#5", 3), (3, "E5", 1)], [(0, "F#5", 1.5), (1.5, "A5", 0.5), (2, "F#5", 2)], [(0, "E5", 2), (2, "C#5", 2)],
         [(0, "D5", 1), (1, "B4", 1), (2, "D5", 1), (3, "E5", 1)], [(0, "A4", 4)]]
MEL_B = [[(0, "G5", 2), (2, "F#5", 1), (3, "E5", 1)], [(0, "F#5", 2), (2, "D5", 2)], [(0, "B4", 1), (1, "D5", 1), (2, "G5", 2)],
         [(0, "E5", 3), (3, "C#5", 1)], [(0, "D5", 2), (2, "F#5", 1), (3, "B5", 1)], [(0, "A5", 2), (2, "F#5", 1), (3, "E5", 1)],
         [(0, "D5", 1.5), (1.5, "E5", 0.5), (2, "B4", 2)], [(0, "A4", 2), (2, "D5", 2)]]
ARP = [(0, 0), (0.5, 1), (1, 2), (1.5, 3), (2, 1), (2.5, 2), (3, 3), (3.5, 2)]


def music():
    ev = []  # (frame, ch, fq, lrv, ev0)
    bars = []
    # intro: 4 bars of the arpeggio, then A (melody), B, A again, a 4-bar outro
    order = [(PROG[i], None) for i in range(4)]
    order += [(PROG[i], MEL_A[i]) for i in range(8)]
    order += [(PROG[8 + i], MEL_B[i]) for i in range(8)]
    order += [(PROG[i], MEL_A[i]) for i in range(8)]
    order += [(PROG[i], None) for i in range(3)] + [("Dmaj7", None)]
    for b, (ch, mel) in enumerate(order):
        t0 = b * 4 * BEAT
        notes, bass = CHORDS[ch]
        last = b == len(order) - 1
        ev.append((t0, 3, vsu_fq(hz(bass)), 0x99, 0xF4))
        if not last:
            ev.append((t0 + 2 * BEAT, 3, vsu_fq(hz(bass) * 1.5), 0x66, 0xF3))
        for i, (bt, k) in enumerate(ARP):
            if last and bt > 0:
                break
            soft = 0x55 if i % 2 else 0x77
            ev.append((t0 + int(bt * BEAT), 1 + (i & 1), vsu_fq(hz(notes[k])), soft, 0xF2))
        if mel:
            for bt, n, ln in mel:
                ev.append((t0 + int(bt * BEAT), 0, vsu_fq(hz(n)), 0xAA, 0xF3 if ln < 2 else 0xF5))
    ev.sort()
    length = len(order) * 4 * BEAT
    return ev, length


def wave(harm):
    x = np.zeros(32)
    for k, a in harm:
        x += a * np.sin(2 * np.pi * k * np.arange(32) / 32)
    x = x / np.abs(x).max()
    return [int(round(31.5 + 31.5 * v)) for v in x]


def main():
    open(os.path.join(HERE, "build", "recip.h"), "w").write("/* (FOC << 16) / z for z < 4096 (Q8): tools/assets.py */\nstatic const u32 recip_tab[4096] = {%s};\n" % ",".join(str((232 << 16) // z if z else 0) for z in range(4096)))
    open(os.path.join(HERE, "build", "assets.h"), "w").write("/* made by tools/assets.py */\nstatic const u8 font5x7[64][5] = {%s};\n" % ",".join("{%s}" % ",".join(str(b) for b in g) for g in font_rows()))
    L = ["/* made by tools/assets.py: do not edit */"]
    datas, lens = [], []
    for name, fn, gain, secs in SOUNDS:
        d = to_s8(load(fn, secs), 120, gain)
        datas.append((name, d))
        lens.append(len(d))
    for name in SYNTH:
        d = to_s8(synth(name), 120, 1.0)
        datas.append((name, d))
        lens.append(len(d))
    for name, d in datas:
        L.append("static const s8 pcm_%s[%d] = {%s};" % (name, len(d), ",".join(str(v) for v in d)))
    L.append("static const s8 *const pcm_data[%d] = {%s};" % (len(datas), ",".join("pcm_" + n for n, _ in datas)))
    L.append("static const u16 pcm_len[%d] = {%s};" % (len(lens), ",".join(map(str, lens))))
    L.append("const u16 pcm_lv[768] = {%s};" % ",".join("0x%04X" % v for v in level_table()))
    ev, length = music()
    L.append("typedef struct { u16 t; u8 ch, lrv; u16 fq; u8 ev0, pad; } Note;")
    L.append("#define MUS_N %d\n#define MUS_LEN %d" % (len(ev), length))
    L.append("static const Note mus[%d] = {%s};" % (len(ev) + 1, ",".join("{%d,%d,%d,%d,%d,0}" % (t, c, lv, f, e) for t, c, f, lv, e in ev) + ",{65535,0,0,0,0,0}"))
    waves = [wave([(1, 1), (2, .35), (3, .12), (4, .06)]), wave([(1, 1), (3, .18), (5, .05)]), wave([(1, 1), (3, .18), (5, .05)]),
             wave([(1, 1), (2, .45), (3, .1)]), [63] * 32]
    L.append("static const u8 snd_waves[5][32] = {%s};" % ",".join("{%s}" % ",".join(map(str, w)) for w in waves))
    open(OUT, "w").write("\n".join(L) + "\n")
    print(OUT, "sounds", sum(lens) / RATE, "s", "music", len(ev), "notes", length / 50, "s")


if __name__ == "__main__":
    main()
