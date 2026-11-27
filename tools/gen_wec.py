#!/usr/bin/env python3
"""The 2027 FIA WEC cars for Endurance Motor Racing 2.0, built from code to the 2027 technical regulations.

Every car is made by the procedural Hypercar / LMGT3 generator inherited from Endurance Motor Racing
(tools/gen_cars.py: bodies, canopies, cabins, wings, liveries, HD details and interiors), sized here to
the rules each car is built to:

  Le Mans Hypercar (LMH), 2027 Technical Regulations
    3.1.1 height     <= 1150 mm above the reference plane         -> canopy top 1.08 m, fin <= 1.12 m
    3.1.2 width      <= 2000 mm                                    -> body half width 1.00 m
    3.1.3 overhangs  <= 1100 mm ahead of the front axle, <= 1000 mm behind the rear axle
    3.1.4 length     <= 5000 mm
    3.1.5 wheelbase  <= 3150 mm
    3.1.7 headlights >= 400 mm above the reference plane
    12.3  tyres      29/71-18 front, 34/71-18 rear (2WD, and 4WD cars homologated after 2022)
    4.1   mass       >= 1030 kg (+ up to 70 kg BoP ballast)        -> src/data.cpp
  LMDh, 2027 Technical Regulations
    3.1.1 height     <= 1060 mm                                    -> canopy top 1.03 m, fin and wing <= 1.06 m
    3.1.2 width      <= 2000 mm
    3.1.3 overhangs  <= 1100 mm front, <= 850 mm rear (rear wing), <= 750 mm (bodywork)
    3.1.4 length     <= 5100 mm
    3.1.5 wheelbase  3148 mm (+/- 5 mm)
    12.3  tyres      29/71-18 front, 34/71-18 rear
    5.1.2 power      ~500 kW at the wheels (BoP), the common rear-axle hybrid
  LMGT3, 2027 Technical Regulations: production-based (FIA GT3 homologation) cars; the regulations
    set the BoP, the refuelling and the safety lights (flashing at 4 Hz in FCY / slow zones). Sizes are
    the GT3 cars' own: 4.6-4.9 m long, ~2.0 m wide, wheelbase ~58 % of the length; Michelin 30/68-18
    front and 31/71-18 rear tyres.

Output (PS Vita set, HD/models): car_<id>.emm, wheel_hyper.emm, wheel_gt3.emm, steering_hyper.emm,
steering_gt3.emm, cockpit_hypercar.emm, cockpit_gt3.emm, livery_career<cls>.lvy.
    python3 tools/gen_wec.py [preview dir]
Axes: +X forward, +Y left, +Z up, metres, origin on the ground under the middle of the car.
"""
import os, sys
sys.argv.insert(1, '--hd')                 # gen_cars: the Vita's high-detail set (HD/models)
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np
import gen_cars as G
from gen_cars import Mesh, Paint, write_emm, curve, sstep
import wec_bodies as B

# ------------------------------------------------------------------ the regulations (metres)
REG = {
    'LMH':  dict(Lmax=5.000, wb=3.150, oh_f=1.100, oh_r=1.000, hmax=1.150, canopy=1.08, wing_max=1.11, tyre_f=(0.29, 0.71), tyre_r=(0.34, 0.71)),
    'LMDh': dict(Lmax=5.100, wb=3.148, oh_f=1.100, oh_r=0.850, hmax=1.060, canopy=1.03, wing_max=1.02, tyre_f=(0.29, 0.71), tyre_r=(0.34, 0.71)),
    'GT3':  dict(Lmax=4.900, wb=None,  oh_f=None,  oh_r=None,  hmax=1.30,  canopy=None, wing_max=1.38, tyre_f=(0.30, 0.68), tyre_r=(0.31, 0.71)),
}
BODY_HALF_W = 1.00                 # 2000 mm overall bodywork width (both prototype rule sets)


def car_types():
    """id -> 'LMH' / 'LMDh' / 'GT3', read from src/data.cpp (the type column)"""
    import re
    src = open(os.path.join(G.ROOT, 'src', 'data.cpp')).read()
    out = {}
    for m in re.finditer(r'\{"(\w+)",[^\n]*?(TYPE_LMH|TYPE_LMDH|TYPE_GT3)', src):
        out[m.group(1)] = {'TYPE_LMH': 'LMH', 'TYPE_LMDH': 'LMDh', 'TYPE_GT3': 'GT3'}[m.group(2)]
    return out


def proto_axles(L, rule):
    """front and rear axle x (from the middle of the car) for a prototype of length L: the regulation
    wheelbase, the overhangs inside their limits, the front one kept to the longest allowed"""
    r = REG[rule]
    assert L <= r['Lmax'] + 1e-6, f'{L} m is longer than the {rule} limit'
    # (the splitter sticks out ~3.7 cm past the body, the endplates and tow strap ~3 cm)
    front = min(r['oh_f'] - 0.042, L - r['wb'] - 0.70, (L - r['wb']) * 0.56)
    front = max(front, L - r['wb'] - (r['oh_r'] - 0.035))       # (the diffuser, end plates and tow strap ~3 cm)
    rear = L - r['wb'] - front
    return L / 2 - front, L / 2 - front - r['wb']


# the new makes' looks (the rest are Endurance Motor Racing's: gen_cars.LOOK)
G.LOOK.update({
    'montagne27':   ('split',   (0.95, 0.95, 0.97), (0.90, 0.12, 0.12)),
    'wokingw1':     ('arrow',   (0.06, 0.06, 0.08), (0.15, 0.65, 0.95)),
    'dearbornlmdh': ('stripe',  (0.95, 0.95, 0.97), (0.85, 0.10, 0.10)),
    'kotobukigr':   ('slash',   (0.85, 0.10, 0.12), (0.08, 0.08, 0.10)),
    'torotgt':      ('arrow',   (0.06, 0.06, 0.08), (0.98, 0.80, 0.10)),
})
G.GT_LAYOUT.update({'torotgt': 'mid'})
# the reference shapes (tools/ref_shapes.json) of the renamed cars
G.REF_ALIAS = {'montagne27': 'montagne26', 'kotobukigr': 'kotobukircg'}


def apply_rules(spec, rule):
    """the body inside its rule set's box: heights, wing and fin"""
    r = REG[rule]
    if rule in ('LMH', 'LMDh'):
        # (the endplates stand 0.10 m above the wing: the whole wing assembly under the height limit)
        spec['wing_z'] = float(min(spec['wing_z'], r['hmax'] - 0.115))
        if spec['fin']:
            spec['fin'] = float(min(spec['fin'], r['hmax'] - 0.04))
        spec['canopy'] = r['canopy']
        # LMH: the taller canopy and higher fenders the LMH rules allow; LMDh: lower and longer
        if rule == 'LMH':
            spec['fender'] = float(min(spec['fender'] + 0.03, 0.82))
            spec['rear_fender'] = float(min(spec['rear_fender'] + 0.03, 0.88))
    else:
        spec['wing_z'] = float(min(spec['wing_z'], r['wing_max'] - 0.04))
    return spec


# the canopy, scaled to the rule set's height (gen_cars draws it with its top at ~1.03 m)
_canopy = G.hyper_canopy


def canopy_for(spec):
    cw, ct = _canopy(spec)
    k = spec.get('canopy', 1.03) / 1.03
    return cw, (lambda x: 0.47 + (ct(x) - 0.47) * k)


G.hyper_canopy = canopy_for


def wheel(R, hw, spokes, sides, pairs=False):
    """gen_cars.hd_wheel with the tyre's own size: radius R, half width hw (axle along Y, outer face +Y)"""
    m = Mesh(1.0)
    rr = R - 0.12
    prof = [(-hw, rr + 0.02), (-hw, R - 0.03), (-hw + 0.03, R), (hw - 0.03, R), (hw, R - 0.03), (hw, rr + 0.02)]
    rings = []
    for k in range(sides + 1):
        a = k * 2 * np.pi / sides
        rings.append([(r_ * np.cos(a), y, r_ * np.sin(a)) for y, r_ in prof])
    m.sheet(rings, uv='rubber', shade=0.9)
    ang = [k * 2 * np.pi / sides for k in range(sides + 1)]
    lip = [[(rr * np.cos(a), hw - 0.012, rr * np.sin(a)), ((rr + 0.022) * np.cos(a), hw + 0.002, (rr + 0.022) * np.sin(a))] for a in ang]
    m.sheet([[lip[k][0], lip[k][1]] for k in range(sides + 1)], uv='grey', shade=1.1)
    m.sheet([[(rr * np.cos(a), y, rr * np.sin(a)) for y in (hw - 0.012, -hw)] for a in ang], uv='rubber', shade=0.5)
    if spokes:
        for k in range(spokes):
            a = k * 2 * np.pi / spokes
            for da in ((-0.06, 0.06) if pairs else (0.0,)):
                c, s_ = np.cos(a + da), np.sin(a + da)
                m.tube([(0.045 * c, hw - 0.03, 0.045 * s_), (rr * c, hw - 0.012, rr * s_)], 0.010, sides=5, uv='grey', shade=0.95)
        m.cyl((0, hw - 0.06, 0), (0, hw - 0.005, 0), 0.05, sides=12, uv='grey', shade=1.1)
        m.cyl((0, hw - 0.005, 0), (0, hw + 0.012, 0), 0.028, sides=6, uv='red', shade=1.2)       # centre-lock nut
        m.cyl((0, -0.02, 0), (0, 0.0, 0), rr - 0.035, sides=sides, uv='cage', shade=0.7)        # brake disc (<= 381 mm)
        m.box((rr - 0.115, -0.035, -0.07), (rr - 0.03, 0.035, 0.07), uv='caliper', shade=1.2)
    else:
        m.cyl((0, hw - 0.012, 0), (0, hw, 0), rr, sides=sides, uv='grey', shade=0.9)              # far: a plain disc
    return m


def main():
    out = G.OUT
    os.makedirs(out, exist_ok=True)
    types = car_types()
    prev = sys.argv[1] if len(sys.argv) > 1 else None
    report = []
    for c in G.read_cars():
        rule = types.get(c['id'], 'GT3' if not c['hyper'] else 'LMDh')
        spec = G.car_spec(dict(c, id=G.REF_ALIAS.get(c['id'], c['id'])) if c['id'] in G.REF_ALIAS else c)
        spec = apply_rules(spec, rule)
        # the 2027 bodies (tools/wec_bodies.py), each make its own nose, lights, pods and wing
        spec = B.proto_spec(c['id'], spec, rule) if c['hyper'] else B.gt_spec(c['id'], spec)
        # the splitter, diffuser and tow strap stick out past the body's own length: shorten the body
        # until the whole car is the length in src/data.cpp (inside the rule's maximum)
        target = c['L']
        for it in range(3):
            if rule != 'GT3':
                G.HY_AXLES = proto_axles(spec['L'], rule)
            spec['axles'] = G.HY_AXLES if c['hyper'] else G.gt_axles(spec['L'])
            P = (B.build_proto if c['hyper'] else B.build_gt)(spec, -1).arrays()[0].reshape(-1, 3)
            over = (P[:, 0].max() - P[:, 0].min()) - target
            if abs(over) < 0.002:
                break
            spec['L'] = spec['L'] - over
        if rule != 'GT3':
            G.HY_AXLES = proto_axles(spec['L'], rule)
        spec['axles'] = G.HY_AXLES if c['hyper'] else G.gt_axles(spec['L'])
        if not c['hyper']:
            spec['bonnet_x'] = spec['axles'][0] - 0.05
        c = dict(c, L=spec['L'])
        p = Paint(c['L'], S=1024)
        G.livery(p, c, spec, c['hyper'])
        G.hd_details(p, c, spec, c['hyper'])
        build = B.build_proto if c['hyper'] else B.build_gt
        fns = B.proto_side_w(spec, 0.38) if c['hyper'] else B.gt_side_w(spec)
        top_mesh = build(spec, -1); G.decal_quads(top_mesh, c, spec, fns)
        mid_mesh = build(spec, 0); G.decal_quads(mid_mesh, c, spec, fns)
        far_mesh = build(spec, 2)
        # 2000 mm overall width: the mirrors and dive planes squeezed in with the rest
        wmax = max(np.abs(m_.arrays()[0].reshape(-1, 3)[:, 1]).max() for m_ in (top_mesh, mid_mesh, far_mesh))
        if wmax > 0.999:
            k = 0.999 / wmax
            for m_ in (top_mesh, mid_mesh, far_mesh):
                m_.tris = [(np.array(t[0]) * np.array([1, k, 1]),) + tuple(t[1:]) for t in m_.tris]
        lods = [(top_mesh, p.img, 512), (mid_mesh, p.img, 256), (far_mesh, p.img, 128)]
        px = spec['panel'][0]
        meta = (0.0 if c['hyper'] else spec['cab'], min(fns(px - 0.2), fns(px + 0.2)) + 0.012, px)
        fa, ra = G.HY_AXLES if c['hyper'] else G.gt_axles(c['L'])
        tf = REG[rule]['tyre_f']
        wy = BODY_HALF_W - tf[0] / 2 - (0.035 if c['hyper'] else 0.06)      # the front wheel's centre, inside the body
        # two files: car_<id>_hd.emm (512 px livery) for your own car, the showroom and the menus;
        # car_<id>.emm (256 px) for the rest of the field -- 40 % less memory per car on a 40-car grid
        n = write_emm(os.path.join(out, f"car_{c['id']}_hd.emm"), lods, meta=meta, emx=(fa, ra, wy, 0.0, 0), share_tex=True)
        write_emm(os.path.join(out, f"car_{c['id']}.emm"), [(top_mesh, p.img, 256), (mid_mesh, p.img, 256), (far_mesh, p.img, 256)],
                  meta=meta, emx=(fa, ra, wy, 0.0, 0), share_tex=True)
        P = top_mesh.arrays()[0].reshape(-1, 3)
        dims = P.max(0) - P.min(0)
        # the height rule leaves out the FIA/ACO antenna (Art. 3.1.1): measured without the roof antenna
        T = top_mesh.arrays()[0]
        body = T[~(np.abs(T[:, :, 1]).max(1) <= 0.0045)].reshape(-1, 3)
        dims[2] = body[:, 2].max() - P[:, 2].min()
        report.append((c['id'], rule, dims, fa - ra, fa, ra))
        ohf, ohr = P[:, 0].max() - fa, ra - P[:, 0].min()
        print(f"{c['id']:18s} {rule:5s} {dims[0]:.3f} x {dims[1]:.3f} x {dims[2]:.3f} m  wheelbase {fa - ra:.3f}  "
              f"overhangs {ohf:.3f} / {ohr:.3f}  triangles {n}")
        if rule != 'GT3':
            r = REG[rule]
            assert dims[0] <= r['Lmax'] + 0.002 and ohf <= r['oh_f'] + 1e-3 and ohr <= r['oh_r'] + 1e-3, (rule, dims[0], ohf, ohr)
        assert dims[1] <= 2.0 + 0.002, 'too wide'
        if rule != 'GT3':
            assert dims[2] <= REG[rule]['hmax'] + 0.002, f"{rule} height {dims[2]:.3f}"
        if c['id'].startswith('career'):
            G.write_livery_set(os.path.join(out, f"livery_{c['id']}.lvy"), c, spec, (512, 256, 128))
        if prev:
            os.makedirs(prev, exist_ok=True)
            p.img.save(os.path.join(prev, f"livery_{c['id']}.png"))
    # each class's road wheel (the rear tyre; the game narrows / shrinks the fronts: FRONT_W, FRONT_R)
    S = 256
    pt = Paint(5.0, S)
    pt.base((0.1, 0.1, 0.1))
    pt.swatches(dict(glass=(0.08, 0.11, 0.16), white=(0.92, 0.92, 0.88), red=(0.85, 0.08, 0.06), carbon=(0.09, 0.09, 0.10),
                     cage=(0.50, 0.52, 0.56), second=(0.8, 0.1, 0.1), panel=(0.9, 0.1, 0.1), paint=(0.6, 0.6, 0.6), **G.HD_SWATCH))
    t = pt.img
    for era, rule in (('hyper', 'LMDh'), ('gt3', 'GT3')):
        w, d = REG[rule]['tyre_r']
        R, hw = d / 2, w / 2
        print(f'wheel_{era}', write_emm(os.path.join(out, f'wheel_{era}.emm'),
                                        [(wheel(R, hw, 10, 16, pairs=True), t, S), (wheel(R, hw, 6, 12), t, 128), (wheel(R, hw, 0, 8), t, 64)]))
    NEW_DASH = (0.0, 0.0, 0.0, 0.0, 2)        # EMX1 flags bit 1: the Vita's dash instruments
    print('cockpit_hypercar', write_emm(os.path.join(out, 'cockpit_hypercar.emm'), [(G.hd_hyper_interior(), t, S)], emx=NEW_DASH))
    print('cockpit_gt3', write_emm(os.path.join(out, 'cockpit_gt3.emm'), [(G.hd_gt_interior(), t, S)], emx=NEW_DASH))
    print('steering_hyper', write_emm(os.path.join(out, 'steering_hyper.emm'), [(G.hd_steering(True), t, S)], emx=NEW_DASH))
    print('steering_gt3', write_emm(os.path.join(out, 'steering_gt3.emm'), [(G.hd_steering(False), t, S)], emx=NEW_DASH))
    # the front tyres against the rears, for src/render.cpp (FRONT_W, FRONT_R)
    for rule in ('LMDh', 'GT3'):
        (wf, df), (wr, dr) = REG[rule]['tyre_f'], REG[rule]['tyre_r']
        print(f'{rule}: FRONT_W {wf / wr:.3f}  FRONT_R {df / dr:.3f}')


if __name__ == '__main__':
    main()
