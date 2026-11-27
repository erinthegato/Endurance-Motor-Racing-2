#!/usr/bin/env python3
"""Build the PC edition's whole higher-detail set (PC/), then pack it.
    python3 tools/build_pc_set.py [preview dir]

The PC build (src/platform_desktop.cpp) reads PC/ before HD/ (the Vita's set) and DATA/. Everything
here comes from the same generators as the Vita's set, run with --pc (tools/pc_refine.py says how
the detail is raised):
  cars (40 + the career cars), road wheels,   tools/gen_wec.py --pc
  cockpits, steering wheels
  trackside objects and circuit buildings     tools/gen_trackside.py --pc   (the Blender props)
  the pit crew (16 poses)                     tools/gen_crew.py --pc
  the driver                                  tools/gen_cockpit.py --pc
  ground textures (2048 px) and kerbs (1024)  tools/gen_textures.py --pc
then tools/pack_files.py zlib-packs the models (the game unpacks them as it loads).
The Vita's HD/ and the PSP-era DATA/ are not touched.
"""
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def run(*args):
    print('==>', ' '.join(args), flush=True)
    subprocess.run([sys.executable] + list(args), cwd=ROOT, check=True)


def main():
    prev = sys.argv[1:2]
    run('tools/gen_wec.py', '--pc', *prev)
    run('tools/gen_trackside.py', '--pc', *prev)
    run('tools/gen_crew.py', '--pc')
    run('tools/gen_cockpit.py', '--pc')
    run('tools/gen_textures.py', '--pc', *prev)
    files = sorted(glob.glob(os.path.join(ROOT, 'PC', 'models', '*')) + glob.glob(os.path.join(ROOT, 'PC', 'tex', '*')))
    run('tools/pack_files.py', *[os.path.relpath(f, ROOT) for f in files])
    total = sum(os.path.getsize(f) for f in files)
    print(f'PC set: {len(files)} files, {total / 1048576:.1f} MB packed')


if __name__ == '__main__':
    main()
