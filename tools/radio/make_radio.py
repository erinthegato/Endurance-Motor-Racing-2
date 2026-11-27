#!/usr/bin/env python3
"""Team radio: cut the race engineer's take (one ElevenLabs recording of every line, in order)
into clips and give each era its own radio sound. Same engineer in every era; only the radio changes:

  1980  crackly AM set: narrow band, overdriven, hiss and crackle, a noise burst as the key drops
  2010  classic F1 radio: telephone band, compressed, the "squelch" chirp before and after
  2016  digital: wider band, clean compression, a short double beep in front
  2025  clean modern: nearly full band, light compression, a soft click

usage: make_radio.py take.mp3 DATA/sfx                   the main take (LINES)
       make_radio.py DATA/sfx GROUP=take.mp3 [...] [--era 16]
  GROUP is one of the groups below (each an ElevenLabs take of its lines, in order, with pauses);
  add _b (info_b=lines_b.mp3) for a second take of the same lines: written as <line>_b, and the game
  picks one of the two at random. --era: only that era's radio (the game uses 16).
writes DATA/sfx/radio_<era>_<line>.raw (ULW1: 8-bit mu-law, mono 11025 Hz -- a radio's band fits; see
tools/sfx/adpcm.py), era = 80 / 10 / 16 / 25
"""
import subprocess, sys, os
import numpy as np
from scipy.signal import butter, sosfilt, resample_poly
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sfx"))
from adpcm import ulaw

SR = 22050
LINES = ["bestlap", "box", "box_ack", "contact", "damage", "drivethrough", "finish", "formation", "fuel",
         "giveback", "go", "green", "lastlap", "lemans", "limits", "overtake", "penalty", "sc", "sc_in", "tyres",
         "engine", "gearbox", "turbo", "suspension", "puncture", "wheel", "leak", "throttle", "brakes",
         "steering", "overheat"]
# the ElevenLabs flow "EMR2 engineer" (voice 6pePhCBvwi0Dte0AkHf3): one take per group, lines in this order
GROUPS = {
    "lines": LINES,
    # a bad result: clean, then (swear) the same lines with the engineer swearing (Settings > Sound)
    "results": ["res_awful", "res_tough", "res_dnf", "res_points", "res_notwanted", "res_last"],
    "swear": ["res_awful_swear", "res_tough_swear", "res_dnf_swear", "res_points_swear", "res_notwanted_swear", "res_last_swear"],
    # other cars retiring
    "retire": ["ret_sister", "ret_leader", "ret_hyper_wall", "ret_gt3_stopped", "ret_ahead", "ret_ahead_engine", "ret_big", "ret_another"],
    # weather, tyres, neutralisations, traffic, night, the drivers, the clock
    "info": ["rain_soon", "rain_wets", "drying", "radar_rain", "radar_dry", "tyres_warm", "tyres_cold", "tyres_hot",
             "fcy", "slowzone", "hyper_behind", "blue_leaders", "night", "sunrise", "tired", "driver_limit", "hour_left", "ten_left"],
}
# a bad day (a poor finish, out of the race), each clean and then swearing (Settings > Sound > Engineer)
BAD_DAY = ["finish_bad", "finish_bad_swear", "retired", "retired_swear"]


def load(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(SR), "-f", "s16le", "-"],
                         check=True, capture_output=True).stdout
    return np.frombuffer(raw, np.int16).astype(np.float32) / 32768.0


def segments(x, n):
    """the n spoken lines: runs of sound split by pauses of 0.45 s or more"""
    hop = int(SR * 0.01)
    env = np.sqrt(np.convolve(x * x, np.ones(hop) / hop, "same"))[::hop]
    loud = env > 10 ** (-38 / 20)
    segs, start, quiet = [], None, 0
    for i, l in enumerate(loud):
        if l:
            if start is None: start = i
            quiet = 0
        elif start is not None:
            quiet += 1
            if quiet >= 45:
                segs.append((start, i - quiet + 1)); start = None
    if start is not None: segs.append((start, len(loud)))
    segs = [s for s in segs if s[1] - s[0] > 15]          # drop clicks / breaths
    # a pause inside a line ("Fuck. Tough day."): join across the shortest pauses until n are left
    # (the pauses between lines are the long ones)
    while len(segs) > n:
        k = min(range(len(segs) - 1), key=lambda i: segs[i + 1][0] - segs[i][1])
        segs[k:k + 2] = [(segs[k][0], segs[k + 1][1])]
    if len(segs) != n:
        sys.exit(f"found {len(segs)} lines, expected {n}")
    pad = int(0.06 * SR)
    return [x[max(0, a * hop - pad): min(len(x), b * hop + pad)] for a, b in segs]


def band(x, lo, hi, order=4):
    return sosfilt(butter(order, [lo, hi], "bandpass", fs=SR, output="sos"), x)


def comp(x, thr=0.12, ratio=4.0):
    env = np.abs(x)
    a = np.exp(-1 / (0.005 * SR)); r = np.exp(-1 / (0.08 * SR))
    e = np.zeros_like(x); v = 0.0
    for i, s in enumerate(env):                            # attack / release follower (lines are short)
        v = a * v + (1 - a) * s if s > v else r * v + (1 - r) * s
        e[i] = v
    gain = np.where(e > thr, (thr + (e - thr) / ratio) / np.maximum(e, 1e-9), 1.0)
    return x * gain


def noise(n, rng, lo=300, hi=4000):
    return band(rng.standard_normal(n).astype(np.float32), lo, hi, 2)


def fade(x, ms=8):
    k = min(len(x) // 2, int(SR * ms / 1000))
    if k > 0:
        x[:k] *= np.linspace(0, 1, k); x[-k:] *= np.linspace(1, 0, k)
    return x


def chirp(f0, f1, dur, vol):
    t = np.arange(int(dur * SR)) / SR
    return fade((np.sin(2 * np.pi * (f0 + (f1 - f0) * t / dur / 2) * t) * vol).astype(np.float32), 3)


def era80(x, rng):
    y = band(x, 380, 2600)
    y = np.tanh(comp(y, 0.08, 5) * 7.0) * 0.55                  # overdriven little set
    y = band(y, 350, 2900, 2)
    hiss = noise(len(y), rng, 500, 5000) * 0.035
    crack = np.zeros_like(y)                                     # random crackles
    for _ in range(int(len(y) / SR * 9)):
        p = rng.integers(0, len(y) - 40); crack[p:p + 40] += rng.standard_normal(40) * rng.uniform(0.05, 0.25)
    fadeAm = 1 + 0.18 * np.sin(2 * np.pi * rng.uniform(0.4, 1.1) * np.arange(len(y)) / SR)   # AM fading
    y = y * fadeAm + hiss + band(crack, 400, 3500, 2)
    pre = noise(int(0.12 * SR), rng, 400, 4500) * 0.22
    post = noise(int(0.22 * SR), rng, 400, 4500) * 0.3           # the noise burst as the key is released
    post *= np.linspace(1, 0, len(post)) ** 0.5
    return np.concatenate([fade(pre), y, fade(post)])


def era10(x, rng):
    y = band(x, 320, 3300)
    y = np.tanh(comp(y, 0.1, 4) * 3.2) * 0.62
    y = y + noise(len(y), rng, 600, 6000) * 0.012
    sq = np.concatenate([chirp(2400, 1800, 0.07, 0.18), noise(int(0.05 * SR), rng, 800, 6000) * 0.12])
    sq2 = np.concatenate([noise(int(0.09 * SR), rng, 800, 6000) * 0.16 * np.linspace(1, 0, int(0.09 * SR)),
                          chirp(1800, 1300, 0.06, 0.14)])
    return np.concatenate([fade(sq), y, fade(sq2)])


def era16(x, rng):
    y = band(x, 200, 5200)
    y = comp(y, 0.12, 3.5) * 2.4
    y = np.round(y * 2048) / 2048                               # a little digital grit
    beep = np.concatenate([chirp(1500, 1500, 0.045, 0.12), np.zeros(int(0.03 * SR), np.float32),
                           chirp(1900, 1900, 0.045, 0.12), np.zeros(int(0.05 * SR), np.float32)])
    return np.concatenate([beep, y, np.zeros(int(0.05 * SR), np.float32)])


def era25(x, rng):
    y = band(x, 120, 7800, 2)
    y = comp(y, 0.15, 2.5) * 2.0
    click = noise(int(0.012 * SR), rng, 1500, 9000) * 0.08
    return np.concatenate([fade(click, 2), np.zeros(int(0.03 * SR), np.float32), y, fade(click.copy(), 2)])


ERAS = {"80": era80, "10": era10, "16": era16, "25": era25}


def write(out, names, clips, eras):
    for tag, fx in ERAS.items():
        if eras and tag not in eras: continue
        rng = np.random.default_rng(len(tag) * 7 + int(tag))
        for name, x in zip(names, clips):
            y = fx(x.copy(), rng)
            y = resample_poly(y, 1, 2)                           # 11025 Hz: radio is band-limited
            y = y / max(1e-6, np.max(np.abs(y))) * 0.82          # every clip as loud as the next
            with open(os.path.join(out, f"radio_{tag}_{name}.raw"), "wb") as f:
                f.write(ulaw((np.clip(y, -1, 1) * 32767).astype("<i2"), SR // 2))


def main():
    args = sys.argv[1:]
    eras = []
    if "--era" in args:
        k = args.index("--era"); eras = args[k + 1].split(","); del args[k:k + 2]
    if len(args) == 2 and "=" not in args[0] and "=" not in args[1]:
        args = [args[1], "lines=" + args[0]]                     # the old form: take.mp3 DATA/sfx
    out, total = args[0], 0
    for a in args[1:]:
        group, take = a.split("=", 1)
        alt = group.endswith("_b")
        names = GROUPS[group[:-2] if alt else group]
        clips = segments(load(take), len(names))
        write(out, [n + "_b" for n in names] if alt else names, clips, eras)
        for n, x in zip(names, clips): print(f"  {group:8s} {n:22s} {len(x) / SR:4.1f} s")
        total += len(clips)
    print(f"{total} lines x {len(eras) or len(ERAS)} eras")


if __name__ == "__main__":
    main()
