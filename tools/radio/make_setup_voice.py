#!/usr/bin/env python3
"""The setup engineer (a different person from the race engineer, in the garage -- no radio): cut her
ElevenLabs take (flow "EMR2 setup engineer", voice Alice, the lines below in order with pauses) into clips.
usage: make_setup_voice.py take.mp3 DATA/sfx      writes DATA/sfx/setup_<line>.raw (ULW1, mono 22050 Hz)"""
import os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_radio as R

LINES = ["greet", "need_laps", "balanced", "gear_longer", "gear_shorter", "wing_more", "wing_less",
         "susp_softer", "susp_stiffer", "bias_rear", "bias_fwd", "tyre_harder", "tyre_softer",
         "under_entry", "over_exit", "applied", "anything"]


def main():
    take, out = sys.argv[1], sys.argv[2]
    clips = R.segments(R.load(take), len(LINES))
    for name, x in zip(LINES, clips):
        y = R.band(x, 70, 10000, 2)                       # just tidy the ends of the band; no radio
        y = R.fade(y.astype(np.float32), 6)
        y = y / max(1e-6, np.max(np.abs(y))) * 0.85
        with open(os.path.join(out, f"setup_{name}.raw"), "wb") as f:
            f.write(R.ulaw((np.clip(y, -1, 1) * 32767).astype("<i2"), R.SR))
        print(f"  setup_{name:14s} {len(x) / R.SR:4.1f} s")


if __name__ == "__main__":
    main()
