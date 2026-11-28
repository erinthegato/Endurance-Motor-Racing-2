#!/usr/bin/env python3
"""Export every car's 2027 design (tools/wec_bodies.py, sized to the regulations by tools/gen_wec.py) for
the Blender rebuild (EMR_Liveries/emr_newcar.py):
    python3 tools/export_designs.py <out dir> [car id ...]
Per car: <id>.npz -- the body and canopy lofts as cross-section rings (so Blender can rebuild them as
smooth four-sided surfaces), each with its texture coordinates and glass faces, and every other part's
triangles with its material tag (carbon, paint, red, glass ...) -- and <id>_livery.png, the team's livery
picture (2048 px, the PC set's) that those coordinates point into. Plus designs.json: sizes, axles,
regulation class, numbers. Nothing is written to the game's folders.
"""
import re, os, sys, json
OUTDIR = sys.argv[1]
ONLY = set(sys.argv[2:])
sys.argv = [sys.argv[0], '--pc']
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import gen_wec as W
from gen_wec import G, B, Paint, REG, BODY_HALF_W

os.makedirs(OUTDIR, exist_ok=True)
Mesh = G.Mesh

# record what the builders do: each loft's rings, and every triangle's material tag (set by the outermost
# builder call: box / quad / tube / cyl / part / loft, whichever the body code called)
_depth = [0]
def tagged(orig, tag_of):
    def f(self, *a, **k):
        t0 = len(self.tris)
        _depth[0] += 1
        try:
            r = orig(self, *a, **k)
        finally:
            _depth[0] -= 1
        if _depth[0] == 0:
            tags = getattr(self, 'tags', [])
            tags = (tags + ['carbon'] * len(self.tris))[:t0]
            tags += tag_of(self, t0, a, k)
            self.tags = tags
        return r
    return f
def tag_from_args(a, k, default='carbon'):
    uv = k.get('uv')
    if uv is None:
        for v in a:
            if isinstance(v, str): uv = v; break
    return uv or default
_orig_tube = Mesh.tube
for name in ('box', 'quad', 'tube', 'cyl'):
    setattr(Mesh, name, tagged(getattr(Mesh, name), lambda self, t0, a, k: [tag_from_args(a, k)] * (len(self.tris) - t0)))
_part = Mesh.part
def part_tags(self, t0, a, k):
    gf = set(k.get('glass_faces') or (a[5] if len(a) > 5 and a[5] else ()) or ())
    uv = k.get('uv', a[2] if len(a) > 2 else 'proj')
    uv = uv if isinstance(uv, str) else 'proj'
    return ['glass' if i in gf else uv for i in range(len(self.tris) - t0)]
Mesh.part = tagged(_part, part_tags)
_loft = Mesh.loft
def loft(self, rings, uv='proj', group='body', cap_front=True, cap_back=True, glass=None):
    t0 = len(self.tris)
    _loft(self, rings, uv, group, cap_front, cap_back, glass)
    n = len(rings[0]); S = len(rings)
    gl = np.zeros((S - 1, n), bool)
    if glass is not None:
        for i in range(S - 1):
            for k_ in range(n): gl[i, k_] = bool(glass(i, k_))
    self.lofts = getattr(self, 'lofts', []) + [dict(rings=np.array(rings, float), t0=t0, glass=gl, caps=(cap_front, cap_back))]
Mesh.loft = tagged(loft, lambda self, t0, a, k: ['proj'] * (len(self.tris) - t0))

types = W.car_types()
_src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'data.cpp')).read()
NUMS = {mm.group(1): [int(mm.group(2)), int(mm.group(3))] for mm in re.finditer(r'\{"(\w+)",\s*"[^"]*",\s*"[^"]*",\s*"[^"]*",\s*\{(\d+), (\d+)\}', _src)}
_mp = os.path.join(OUTDIR, 'designs.json')
meta = json.load(open(_mp)) if os.path.exists(_mp) else {}      # (add to / update the cars already exported)
for c in G.read_cars():
    if ONLY and c['id'] not in ONLY: continue
    rule = types.get(c['id'], 'GT3' if not c['hyper'] else 'LMDh')
    spec = G.car_spec(dict(c, id=G.REF_ALIAS.get(c['id'], c['id'])) if c['id'] in G.REF_ALIAS else c)
    spec = W.apply_rules(spec, rule)
    spec = B.proto_spec(c['id'], spec, rule) if c['hyper'] else B.gt_spec(c['id'], spec)
    target = c['L']
    for it in range(3):
        if rule != 'GT3': G.HY_AXLES = W.proto_axles(spec['L'], rule)
        spec['axles'] = G.HY_AXLES if c['hyper'] else G.gt_axles(spec['L'])
        P = (B.build_proto if c['hyper'] else B.build_gt)(spec, -1).arrays()[0].reshape(-1, 3)
        over = (P[:, 0].max() - P[:, 0].min()) - target
        if abs(over) < 0.002: break
        spec['L'] = spec['L'] - over
    if rule != 'GT3': G.HY_AXLES = W.proto_axles(spec['L'], rule)
    spec['axles'] = G.HY_AXLES if c['hyper'] else G.gt_axles(spec['L'])
    if not c['hyper']: spec['bonnet_x'] = spec['axles'][0] - 0.05
    c = dict(c, L=spec['L'])
    p = Paint(c['L'], S=2048)
    spec['no_panel'] = True
    G.livery(p, c, spec, c['hyper'])
    G.hd_details(p, c, spec, c['hyper'])
    build = B.build_proto if c['hyper'] else B.build_gt
    fns = B.proto_side_w(spec, 0.38) if c['hyper'] else B.gt_side_w(spec)
    m = build(spec, -1); G.decal_quads(m, c, spec, fns)
    T = m.tris
    Pt, Ut, _lt, Gl = m.arrays()
    Nt = np.array([t[1] for t in T], float)
    Sh = np.array([t[5] for t in T], float)
    # material tag: glass, the swatch name (carbon, paint, red, white ...), 'proj' (painted body, livery),
    # 'decal' (number / name decals: their own coordinates into the livery picture)
    tags = ['glass' if g else (t[2] if isinstance(t[2], str) else 'decal') for t, g in zip(T, Gl)]
    in_loft = np.zeros(len(T), bool)
    arrays = {}
    for li, lf in enumerate(m.lofts):
        R = lf['rings']; S_, n = R.shape[:2]
        k = (S_ - 1) * n * 2
        in_loft[lf['t0']:lf['t0'] + k] = True
        caps = (S_ - 2) * 0
        if lf['caps'][0]: in_loft[lf['t0'] + k:lf['t0'] + k + (n - 2)] = True
        if lf['caps'][1]: in_loft[lf['t0'] + k + (n - 2) * int(lf['caps'][0]):lf['t0'] + k + (n - 2) * (1 + int(lf['caps'][0]))] = True
        arrays[f'loft{li}_rings'] = R
        arrays[f'loft{li}_uv'] = Ut[lf['t0']:lf['t0'] + k].reshape(S_ - 1, n, 2, 3, 2)
        arrays[f'loft{li}_glass'] = lf['glass']
        arrays[f'loft{li}_caps'] = np.array(lf['caps'])
    keep = ~in_loft
    arrays['parts_P'] = Pt[keep]; arrays['parts_N'] = Nt[keep]; arrays['parts_U'] = Ut[keep]; arrays['parts_shade'] = Sh[keep]
    arrays['parts_tag'] = np.array([t for t, k in zip(tags, keep) if k])
    # the cockpit (the game's own, unrefined) so the cabin is not empty through the glass
    # (the hypercar's rollover hoops and canopy-frame tubes, r >= 30 mm, are tagged 'cage' and left out:
    # they follow the in-game cockpit's generic canopy, not this car's, and stick out through the glass)
    _tube = Mesh.tube
    if c['hyper']:
        Mesh.tube = tagged(_orig_tube, lambda self, t0, a, k: [('cage' if tag_from_args(a, k) == 'grey' and len(a) > 1
                                                               and float(a[1]) >= 0.029 else tag_from_args(a, k))] * (len(self.tris) - t0))
    try:
        mi = B.proto_interior() if c['hyper'] else B.gt_interior()
    finally:
        Mesh.tube = _tube
    Pi = mi.arrays()[0]
    ti = getattr(mi, 'tags', [])
    ti = (ti + ['carbon'] * len(mi.tris))[:len(mi.tris)]
    # the steering wheel (the game's own, drawn on its own in the cockpit view) at its place in the cabin
    ms = G.hd_steering(c['hyper'])
    Ps = ms.arrays()[0] + np.array(B.PROTO_WHEEL if c['hyper'] else B.GT_WHEEL)
    ts = getattr(ms, 'tags', [])
    ts = (ts + ['carbon'] * len(ms.tris))[:len(ms.tris)]
    Pi = np.concatenate([Pi, Ps]); ti = ti + ts
    ti = [t if isinstance(t, str) else 'carbon' for t in ti]
    keep_i = np.array([t != 'cage' for t in ti])
    # the cockpit was made for the game's generic canopy: drop whatever pokes out through this car's bodywork
    # (e.g. the dash cowl ahead of a low windscreen) -- a corner above the body / canopy roof line is outside
    env = [m.lofts[0]['rings'], m.lofts[1]['rings']]
    def roof(x, y):
        best = -1.0
        for R in env:
            xs = R[:, 0, 0]
            if x < xs.min() - 0.01 or x > xs.max() + 0.01: continue
            r = R[np.argmin(abs(xs - x))]
            a, b = r, np.roll(r, -1, 0)
            for (ya, za), (yb, zb) in zip(a[:, 1:], b[:, 1:]):
                if (ya - y) * (yb - y) <= 0 and ya != yb:
                    best = max(best, za + (zb - za) * (y - ya) / (yb - ya))
        return best
    for i, tri in enumerate(Pi):
        if keep_i[i] and any(p[2] > roof(p[0], p[1]) + 0.005 for p in tri):
            keep_i[i] = False
    arrays['interior_P'] = Pi[keep_i]; arrays['interior_tag'] = np.array(ti)[keep_i]
    np.savez_compressed(os.path.join(OUTDIR, f"{c['id']}.npz"), **arrays)
    p.img.save(os.path.join(OUTDIR, f"{c['id']}_livery.png"))
    fa, ra = spec['axles']
    meta[c['id']] = dict(rule=rule, hyper=bool(c['hyper']), L=float(spec['L']), axles=[float(fa), float(ra)],
                         lofts=len(m.lofts), numbers=NUMS.get(c['id'], []), panel=[float(v) for v in spec['panel']], top_panel=float(spec['top_panel']), make=c.get('make', ''), team=c.get('team', ''),
                         tyre_f=REG[rule]['tyre_f'], tyre_r=REG[rule]['tyre_r'],
                         mirrors=[list(map(float, q)) for q in getattr(m, 'mirrors', [])],
                         colour=[float(v) for v in c['colour']], second=[float(v) for v in spec['second']], accent=[float(v) for v in spec['accent']],
                         eye=list(B.PROTO_EYE if c['hyper'] else B.GT_EYE),
                         lights=spec.get('lights', ''), wing_mount=spec.get('wing_mount', ''), scoop=bool(spec.get('scoop', False)))
    print(c['id'], rule, f"L {spec['L']:.3f}", 'lofts', len(m.lofts), 'parts', int(keep.sum()), 'tris', len(T))
json.dump(meta, open(os.path.join(OUTDIR, 'designs.json'), 'w'), indent=1)
