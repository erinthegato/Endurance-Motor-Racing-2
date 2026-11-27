#!/usr/bin/env python3
"""The PS Vita's cockpits and steering wheels, one step up (HD/models):
    python3 tools/gen_vita_cockpits.py
The same generators as tools/gen_wec.py (wec_bodies.proto_interior / gt_interior, gen_cars.hd_steering)
with the PC's shapes (rounder tubes and cylinders, baked ambient occlusion), refined once onto their
curves (each triangle becomes four) where the PC's are refined twice, with a 512 px picture:
  hypercar cockpit ~19 k triangles, GT3 ~6.7 k, steering wheels ~5.5 k (the old Vita set: 0.5-1.3 k).
Then packed (tools/pack_files.py). The rest of HD/models is not touched.
"""
import os
import sys
import subprocess
sys.argv = [sys.argv[0]]                 # (gen_wec reads the command line: the Vita's set, no preview)
os.environ['EMR_PC'] = '1'               # the PC's shapes and ambient occlusion...
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_wec as W
from gen_wec import G, B, Paint, write_emm
B.PCSET = True
G.OUT = os.path.join(G.ROOT, 'HD', 'models')     # ...written to the Vita's set


def main():
    out = G.OUT
    S = 512
    pt = Paint(5.0, S)
    pt.base((0.1, 0.1, 0.1))
    pt.swatches(dict(glass=(0.08, 0.11, 0.16), white=(0.92, 0.92, 0.88), red=(0.85, 0.08, 0.06), carbon=(0.09, 0.09, 0.10),
                     cage=(0.50, 0.52, 0.56), second=(0.8, 0.1, 0.1), panel=(0.9, 0.1, 0.1), paint=(0.6, 0.6, 0.6), **G.HD_SWATCH))
    t = pt.img
    NEW_DASH = (0.0, 0.0, 0.0, 0.0, 2)    # EMX1 flags bit 1: the Vita's dash instruments
    ref = lambda m: m.refined(1)
    print('cockpit_hypercar', write_emm(os.path.join(out, 'cockpit_hypercar.emm'), [(ref(B.proto_interior()), t, S)], emx=NEW_DASH))
    print('cockpit_gt3', write_emm(os.path.join(out, 'cockpit_gt3.emm'), [(ref(B.gt_interior()), t, S)], emx=NEW_DASH))
    print('steering_hyper', write_emm(os.path.join(out, 'steering_hyper.emm'), [(ref(G.hd_steering(True)), t, S)], emx=NEW_DASH, ao_ground=False))
    print('steering_gt3', write_emm(os.path.join(out, 'steering_gt3.emm'), [(ref(G.hd_steering(False)), t, S)], emx=NEW_DASH, ao_ground=False))
    names = ['cockpit_hypercar', 'cockpit_gt3', 'steering_hyper', 'steering_gt3']
    subprocess.run([sys.executable, os.path.join(G.ROOT, 'tools', 'pack_files.py')] +
                   [os.path.join('HD', 'models', n + '.emm') for n in names], cwd=G.ROOT, check=True)


if __name__ == '__main__':
    main()
