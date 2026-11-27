"""New car bodies for Endurance Motor Racing 2.0, built in code to the 2027 regulations (tools/gen_wec.py
sizes them, paints them and writes the models; this file only makes the shapes).

Prototypes (Le Mans Hypercar and LMDh):
  * a low pointed nose between two tall fender pods (a deep channel down the middle), each pod carrying
    its headlights (>= 400 mm, Art. 3.1.7) and, over the front wheels, the compulsory cut-outs
    (LMDh Art. 3.4.2.6; louvres on the LMH cars too)
  * real wheel arches: the sides end at the arch line, so the wheels show
  * sculpted sidepods with an undercut between the axles
  * a teardrop canopy over the single central seat; a roof scoop on some
  * an engine-cover fin (LMDh: its top edge at 1050-1060 mm, Art. 3.10), a full-width rear wing on
    swan-neck mounts, end plates or a central pylon, a diffuser with strakes
  * the LMDh rear tyre fairings (Art. 3.5.4 f) behind the rear wheels
  * each make its own nose, fender height, light signature and wing mount
LMGT3 (production-based coupes): a body to the belt line with flared arches, a glass house with
windscreen, side windows and rear glass, and the layout's proportions:
  * 'front' engine (long bonnet, cabin back, fastback), 'mid' engine (short nose, cabin forward,
    long rear deck), 'rear' engine (rounded, sloping tail, round lamps)
  * splitter, canards, bonnet vents, side skirts, diffuser, a swan-neck wing high at the back

Axes: +X forward, +Y left, +Z up, metres, origin on the ground under the middle of the car.
"""
import numpy as np
import gen_cars as G
from gen_cars import Mesh, curve, sstep

R_TYRE = 0.355          # 71 cm tyres (both classes, near enough: GT3 fronts 68 cm)

# PC edition: the top level of detail (lod -1) is built denser still (tools/gen_wec.py --pc -> PC/models):
# twice the stations along the body, the cross-sections' curved upper half subdivided (rounder),
# a finer canopy. The Vita's set (HD/models) is unchanged.
PCSET = False


def smooth_half(half, start, k=2):
    """Catmull-Rom subdivision of a half profile's points from `start` on (the curved upper body):
    k-1 new points between each pair, on a smooth curve through the old ones (the old ones are kept)"""
    if k < 2 or len(half) - start < 3:
        return half
    P = [np.array(p, float) for p in half]
    out = [tuple(p) for p in half[:start]]
    for i in range(start, len(P) - 1):
        p0 = P[max(i - 1, start)]; p1 = P[i]; p2 = P[i + 1]; p3 = P[min(i + 2, len(P) - 1)]
        for j in range(k):
            t = j / k
            q = 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t ** 3)
            out.append((float(q[0]), float(q[1])))
    out.append(tuple(P[-1]))
    return out


def arch_z(x, axles, base):
    """the top of the wheel arch opening at x (base where there is no wheel)"""
    z = base
    for ax in axles:
        dx = abs(x - ax)
        r = R_TYRE + 0.06
        if dx < r:
            z = max(z, R_TYRE + np.sqrt(r * r - dx * dx) * 0.92)
    return z


def in_arch(x, axles):
    return any(abs(x - ax) < R_TYRE + 0.06 for ax in axles)


def stations(xf, xr, axles, n_body, extra=()):
    """x stations from nose to tail: evenly spaced plus the arch edges (so the openings stay sharp)"""
    xs = list(np.linspace(xf, xr, n_body))
    for ax in axles:
        r = R_TYRE + 0.06
        for d in (-r - 0.012, -r + 0.012, -r * 0.7, -r * 0.35, 0.0, r * 0.35, r * 0.7, r - 0.012, r + 0.012):
            xs.append(ax + d)
    xs += list(extra)
    xs = sorted(set(round(x, 4) for x in xs if xr <= x <= xf), reverse=True)
    return xs


def mirror_ring(x, half):
    """half profile (y >= 0) from the bottom middle round the side to the top middle -> closed ring"""
    ring = [(x, y, z) for y, z in half] + [(x, -y, z) for y, z in half[-2:0:-1]]
    return np.array(ring)


# ====================================================================== prototypes
PROTO = {
    #                nose  fend  rfend  pod    canopy scoop  fin    wing     lights
    'ardenza499':    (0.24, 0.84, 0.86, 0.74, 1.00, False, True,  'swan',  'claw'),
    'kaizen10':      (0.27, 0.86, 0.88, 0.80, 1.00, True,  True,  'end',   'bar'),
    'lionne9x9':     (0.25, 0.88, 0.90, 0.76, 0.98, False, False, 'swan',  'claw'),
    'ashcombevalkyr':(0.22, 0.90, 0.92, 0.70, 1.00, True,  True,  'pylon', 'twin'),
    'regalvr8':      (0.28, 0.80, 0.84, 0.82, 1.00, True,  True,  'end',   'bar'),
    'isarmh8':       (0.26, 0.82, 0.84, 0.80, 1.00, True,  True,  'swan',  'twin'),
    'montagne27':    (0.25, 0.81, 0.85, 0.78, 1.00, False, True,  'swan',  'claw'),
    'origingx01':    (0.27, 0.80, 0.83, 0.80, 1.00, True,  True,  'end',   'bar'),
    'wokingw1':      (0.24, 0.83, 0.86, 0.76, 1.00, True,  True,  'swan',  'twin'),
    'dearbornlmdh':  (0.28, 0.82, 0.85, 0.82, 1.00, True,  True,  'end',   'bar'),
    'careerhyper':   (0.26, 0.82, 0.85, 0.78, 1.00, True,  True,  'swan',  'bar'),
}


def proto_spec(cid, spec, rule):
    nose, fend, rfend, pod, can, scoop, fin, wing, lights = PROTO.get(cid, PROTO['careerhyper'])
    r = dict(rule=rule, nose_h=nose, fend_h=fend, rfend_h=rfend, pod=pod, canopy_k=can, scoop=scoop, fin=fin,
             wing_mount=wing, lights=lights)
    if rule == 'LMDh':
        r['fin'] = True                                  # LMDh: the fin is part of the common bodywork
        r['fin_top'] = 1.052                             # Art. 3.10: top edge 1050-1060 mm
    else:
        r['fin_top'] = min(spec.get('fin') or 1.08, 1.10)
    spec.update(r)
    return spec


def proto_fns(spec):
    L = spec['L']; xf = L / 2; xr = -L / 2
    fa, ra = spec['axles']
    nh, fh, rh, pod = spec['nose_h'], spec['fend_h'], spec['rfend_h'], spec['pod']
    # plan view: the full width at the axles, the nose pinched, the waist (above the sidepods) pulled in
    W = curve([(xf, 0.55), (xf - 0.10, 0.80), (fa + 0.42, 0.97), (fa + 0.2, 1.0), (fa - 0.45, 1.0), (fa - 0.95, 0.90),
               (0.0, 0.86), (ra + 0.85, 0.94), (ra + 0.40, 1.0), (ra - 0.35, 1.0), (xr + 0.06, 0.96), (xr, 0.92)])
    # the middle: the nose rising to the cockpit, the engine deck behind it
    deck = curve([(xf, nh - 0.08), (xf - 0.20, nh), (fa + 0.25, nh + 0.12), (fa - 0.20, 0.44), (1.05, 0.50), (0.0, 0.56),
                  (ra + 0.40, 0.62), (ra - 0.20, 0.62), (xr, 0.56)])
    # the fender pods' crest: up over the front wheels, down along the sidepods, up again over the rears
    fend = curve([(xf, nh), (xf - 0.30, nh + 0.22), (fa + 0.42, fh - 0.10), (fa + 0.05, fh), (fa - 0.40, fh - 0.05),
                  (fa - 0.95, 0.58), (0.20, 0.52), (ra + 0.95, 0.56), (ra + 0.40, rh - 0.04), (ra - 0.05, rh), (xr + 0.10, rh - 0.06), (xr, rh - 0.14)])
    # the sidepod undercut (the lower side drawn in between the axles)
    under = curve([(xf, 1.0), (fa - 0.45, 1.0), (fa - 0.90, pod), (ra + 0.90, pod), (ra + 0.45, 1.0), (xr, 1.0)])
    return W, deck, fend, under


def proto_ring(x, spec, fns, lod):
    W, deck, fend, under = fns
    fa, ra = spec['axles']
    w = W(x); d = deck(x); f = fend(x); u = under(x)
    zb = 0.05 + (0.08 if x > spec['L'] / 2 - 0.25 else 0)
    arch = in_arch(x, (fa, ra))
    za = arch_z(x, (fa, ra), 0.24) if arch else 0.24
    yi = 0.58 if arch else w * u * 0.92                # the floor's edge (inside the wheel in an arch)
    ws = w if arch else w * u                          # the side below the waist
    half = [(0.0, zb), (yi, zb), (yi, za if arch else zb + 0.02), (ws, za if arch else 0.24),
            (w, max(za + 0.02, 0.44) if arch else 0.44), (w * 0.97, f - 0.10), (w * 0.88, f - 0.015), (w * 0.79, f),
            (w * 0.69, f - 0.04), (w * 0.60, max(d + 0.05, f - 0.16)), (w * 0.52, d + 0.025), (w * 0.28, d + 0.008), (0.0, d)]
    if lod >= 2:
        half = [half[i] for i in (0, 1, 2, 3, 4, 6, 8, 10, 12)]
    elif PCSET and lod < 0:
        half = smooth_half(half, 7)
    return mirror_ring(x, half)


def canopy_fns(spec):
    k = spec.get('canopy', 1.03) / 1.03
    cw = curve([(1.05, 0.06), (0.80, 0.34), (0.30, 0.47), (-0.40, 0.45), (-1.05, 0.33), (-1.75, 0.12)])
    ct = curve([(1.05, 0.50), (0.72, 0.78), (0.28, 0.98), (-0.15, 1.03), (-0.70, 0.98), (-1.25, 0.84), (-1.75, 0.66)])
    return cw, (lambda x: 0.50 + (ct(x) - 0.50) * k)


def build_proto(spec, lod):
    """lod -1 (HD), 0, 2"""
    L = spec['L']; xf = L / 2; xr = -L / 2
    fa, ra = spec['axles']
    fns = proto_fns(spec)
    W, deck, fend, under = fns
    m = Mesh(L)
    n_body = {-1: 100 if PCSET else 30, 0: 16, 2: 7}[lod]
    xs = stations(xf, xr, (fa, ra) if lod < 2 else (), n_body)
    m.loft([proto_ring(x, spec, fns, lod) for x in xs])
    # the canopy: a teardrop over the central seat; glass from the screen to behind the driver's head
    cw, ct = canopy_fns(spec)
    cxs = {-1: list(np.linspace(1.05, -1.75, 14)), 0: list(np.linspace(1.05, -1.75, 8)), 2: [1.05, 0.3, -0.5, -1.75]}[lod]
    angs = {-1: [0, 15, 30, 45, 60, 75, 90], 0: [0, 30, 60, 90], 2: [0, 50, 90]}[lod]
    if PCSET and lod < 0:
        cxs = list(np.linspace(1.05, -1.75, 44)); angs = [a * 5.0 for a in range(19)]
    rings = []
    for x in cxs:
        w = cw(x); zt = ct(x); zb = deck(x) - 0.03
        half = [(w * np.cos(np.radians(a)), zb + (zt - zb) * np.sin(np.radians(a))) for a in angs]
        rings.append(np.array([(x, y, z) for y, z in half] + [(x, -y, z) for y, z in half[-2::-1]]))
    nglass = sum(1 for x in cxs if x > -0.35)
    m.loft(rings, cap_front=False, cap_back=False, glass=lambda i, k: i < nglass - 1)
    if lod >= 2:
        _proto_wing(m, spec, lod)
        return m
    # roof scoop behind the driver
    if spec['scoop']:
        x0, x1 = -0.40, -1.00
        z0 = ct(-0.40) - 0.02
        m.part(np.array([[x0, 0.10, z0 + 0.07], [x0, -0.10, z0 + 0.07], [x1, -0.13, ct(x1) - 0.02], [x1, 0.13, ct(x1) - 0.02],
                         [x0, 0.10, z0 - 0.01], [x0, -0.10, z0 - 0.01]]),
               [[0, 1, 2], [0, 2, 3], [4, 5, 1], [4, 1, 0], [0, 3, 4], [1, 5, 2]], uv='proj', flat=True)
        m.quad((x0 + 0.002, 0.09, z0 + 0.06), (x0 + 0.002, -0.09, z0 + 0.06), (x0 + 0.002, -0.09, z0), (x0 + 0.002, 0.09, z0), uv='carbon', shade=0.4)
    # the fin: from the canopy's tail to the rear wing
    if spec['fin']:
        x0, x1 = -1.30, xr + 0.38
        top = spec['fin_top']
        P = np.array([[x0, 0.0, ct(x0)], [x1, 0.0, deck(x1)], [x1, 0.0, top], [x0 - 0.05, 0.0, ct(x0) + 0.03]])
        for s in (0.010, -0.010):
            Q = P.copy(); Q[:, 1] = s
            m.part(Q, [[0, 1, 2], [0, 2, 3]], uv='second', flat=True)
    _proto_wing(m, spec, lod)
    # splitter, dive planes, headlights, mirrors, louvres, diffuser, tail lights
    m.box((xf - 0.40, -0.86, 0.035), (xf + 0.03, 0.86, 0.06), uv='carbon')
    for s in (-1, 1):
        # dive planes on the nose corners
        x0 = xf - 0.22
        m.quad((x0 + 0.12, s * 0.78, 0.30), (x0 - 0.12, s * 0.93, 0.36), (x0 - 0.12, s * 0.93, 0.375), (x0 + 0.12, s * 0.78, 0.315), uv='carbon')
        # headlights on the pod fronts (>= 400 mm)
        _proto_lights(m, spec, fns, s)
        # the cut-outs over the front wheels (louvres) -- compulsory on the LMDh
        n_l = 6 if lod < 0 else 4
        for k in range(n_l):
            xl = fa + 0.30 - k * (0.62 / n_l)
            yl0, yl1 = s * W(xl) * 0.70, s * W(xl) * 0.93
            zl = fend(xl) + 0.004
            m.quad((xl, yl0, zl), (xl - 0.055, yl0, zl - 0.005), (xl - 0.055, yl1, zl - 0.03), (xl, yl1, zl - 0.025), uv='carbon', shade=0.35)
        # mirrors on stalks, on the fender crest by the windscreen
        mx = 0.62; my = s * W(mx) * 0.80; mz = fend(mx)
        # (the mirror's glass: its rear face -- the PC draws a live picture there, see EMQ1)
        mw, mh = (0.17, 0.09) if PCSET else (0.09, 0.07)        # (the PC: a real racing mirror's size)
        if lod < 0:
            m.mirrors = getattr(m, 'mirrors', []) + [(mx - 0.10 - 0.003, my - s * (mw / 2 - 0.004), mz + 0.10 + mh / 2, mw / 2 - 0.008, mh / 2 - 0.008)]
        m.box((mx - 0.05, my - 0.012, mz), (mx - 0.01, my + 0.012, mz + 0.11), uv='carbon')
        m.box((mx - 0.10, my - mw * s - 0.004 if s > 0 else my - 0.004, mz + 0.10), (mx + 0.03, my + 0.004 if s > 0 else my + mw, mz + 0.10 + mh), uv='paint' if lod < 0 else 'carbon')
        # LMDh rear tyre fairings: a vertical plate just behind each rear wheel, low down
        if spec['rule'] == 'LMDh':
            xp = ra - 0.44
            m.quad((xp, s * 0.62, 0.05), (xp, s * 0.97, 0.05), (xp, s * 0.97, 0.36), (xp, s * 0.62, 0.36), uv='carbon', shade=0.7)
    # diffuser: the tail's lower panel with strakes, the tail-light bar across the back
    m.quad((xr - 0.004, -0.82, 0.07), (xr - 0.004, 0.82, 0.07), (xr - 0.004, 0.82, 0.32), (xr - 0.004, -0.82, 0.32), uv='carbon', shade=0.5)
    for k in range(-3, 4):
        m.box((xr - 0.02, k * 0.22 - 0.008, 0.06), (xr + 0.35, k * 0.22 + 0.008, 0.30), uv='carbon')
    zt = fend(xr + 0.05) - 0.06
    m.quad((xr - 0.006, -0.88, zt - 0.03), (xr - 0.006, 0.88, zt - 0.03), (xr - 0.006, 0.88, zt), (xr - 0.006, -0.88, zt), uv='red', shade=1.4)
    m.quad((xr - 0.006, -0.05, 0.36), (xr - 0.006, 0.05, 0.36), (xr - 0.006, 0.05, 0.46), (xr - 0.006, -0.05, 0.46), uv='red', shade=1.4)   # rain light
    if lod < 0:
        # the roof antenna and the tow strap
        m.box((-0.36, -0.004, ct(-0.36)), (-0.30, 0.004, ct(-0.36) + 0.15), uv='carbon')
        m.box((xr - 0.03, -0.03, 0.30), (xr + 0.01, 0.03, 0.34), uv='yellow')
    if lod < 0 and PCSET:
        # the PC's close-up details: the wiper parked at the screen's foot, the roof's position
        # lights, the refuelling and air-jack couplings, the front tow eye
        def on_canopy(x, y, lift):
            w = cw(x); zt = ct(x); zb = deck(x) - 0.03
            c = min(abs(y) / max(w, 1e-3), 1.0)
            return (x, y, zb + (zt - zb) * float(np.sqrt(max(1 - c * c, 0))) + lift)
        xw = 0.93
        m.tube([on_canopy(xw, -0.30, 0.012), on_canopy(xw - 0.015, -0.08, 0.014), on_canopy(xw - 0.03, 0.14, 0.012)], 0.006, sides=6, uv='carbon')
        m.tube([on_canopy(xw - 0.02, -0.30, 0.004), on_canopy(xw - 0.02, -0.30, 0.03)], 0.012, sides=6, uv='carbon')      # its pivot
        for s in (-1, 1):
            m.box((-0.05, s * 0.06 - 0.012, ct(-0.05) - 0.004), (0.02, s * 0.06 + 0.012, ct(-0.05) + 0.012), uv='red' if s > 0 else 'white', shade=1.3)
            # fuel and air couplings on the pod tops behind the doors
            xc = -0.55; yc = s * W(xc) * 0.70
            m.cyl((xc, yc, fend(xc) - 0.01), (xc, yc, fend(xc) + 0.025), 0.035, sides=12, uv='grey', shade=1.1)
            m.cyl((xc, yc, fend(xc) + 0.025), (xc, yc, fend(xc) + 0.03), 0.022, sides=10, uv='red', shade=1.2)
        m.tube([(xf - 0.02, 0.0, 0.10), (xf + 0.015, 0.0, 0.10), (xf + 0.015, 0.0, 0.14)], 0.009, sides=6, uv='yellow')     # tow eye
    return m


def _proto_wing(m, spec, lod):
    L = spec['L']; xr = -L / 2
    wz = spec['wing_z']; ws = spec['wing_span']
    x0 = xr + 0.02
    # main plane and a flap above its trailing edge (a gurney on the HD car)
    m.box((x0 + 0.06, -ws, wz), (x0 + 0.36, ws, wz + 0.03), uv='carbon')
    if lod < 2:
        m.box((x0, -ws, wz + 0.035), (x0 + 0.16, ws, wz + 0.06), uv='second')
        for s in (-1, 1):
            # end plates
            m.box((x0 - 0.02, s * ws - 0.010, wz - 0.22), (x0 + 0.40, s * ws + 0.010, wz + 0.095), uv='second')
        mount = spec['wing_mount']
        if mount == 'swan':
            for s in (-0.30, 0.30):
                m.tube([(x0 + 0.40, s, 0.60), (x0 + 0.30, s, wz + 0.09), (x0 + 0.20, s, wz + 0.09)], 0.012, uv='carbon')
        elif mount == 'pylon':
            m.box((x0 + 0.08, -0.012, 0.58), (x0 + 0.32, 0.012, wz), uv='carbon')
        else:
            for s in (-0.42, 0.42):
                m.box((x0 + 0.10, s - 0.010, 0.58), (x0 + 0.28, s + 0.010, wz), uv='carbon')


def _proto_lights(m, spec, fns, s):
    W, deck, fend, under = fns
    L = spec['L']; xf = L / 2
    fa = spec['axles'][0]
    st = spec['lights']
    x0 = fa + 0.50
    y0 = s * W(x0) * 0.82
    z0 = max(0.42, fend(x0) - 0.10)                  # Art. 3.1.7: the main beam >= 400 mm up
    wht = 'white'
    if st == 'bar':                                  # one slim horizontal bar across the pod front
        m.quad((x0 + 0.05, y0 - s * 0.14, z0), (x0 + 0.05, y0 + s * 0.10, z0 + 0.01), (x0 + 0.02, y0 + s * 0.10, z0 + 0.05), (x0 + 0.02, y0 - s * 0.14, z0 + 0.04), uv=wht, shade=1.5)
    elif st == 'claw':                               # three slashes, like claw marks
        for k in range(3):
            yy = y0 - s * (0.10 - k * 0.07)
            m.quad((x0 + 0.05, yy, z0 - 0.02), (x0 + 0.05, yy + s * 0.025, z0 - 0.02), (x0 + 0.0, yy + s * 0.045, z0 + 0.07), (x0 + 0.0, yy + s * 0.02, z0 + 0.07), uv=wht, shade=1.5)
    elif st == 'twin':                               # two stacked lamps
        for dz in (0.0, 0.06):
            m.quad((x0 + 0.05, y0 - s * 0.08, z0 + dz), (x0 + 0.05, y0 + s * 0.08, z0 + dz), (x0 + 0.03, y0 + s * 0.08, z0 + dz + 0.035), (x0 + 0.03, y0 - s * 0.08, z0 + dz + 0.035), uv=wht, shade=1.5)
    # the daytime running line along the pod's leading edge
    m.quad((xf - 0.10, s * W(xf - 0.10) * 0.70, deck(xf - 0.1) + 0.03), (x0, s * W(x0) * 0.66, z0 + 0.03),
           (x0, s * W(x0) * 0.66, z0 + 0.045), (xf - 0.10, s * W(xf - 0.10) * 0.70, deck(xf - 0.1) + 0.045), uv=wht, shade=1.3)


def proto_side_w(spec, z=0.40):
    """the body's half width at height z along x (the number and the stickers go there)"""
    W, deck, fend, under = proto_fns(spec)
    fa, ra = spec['axles']
    def f(x):
        if in_arch(x, (fa, ra)):
            return W(x)
        w, u = W(x), under(x)
        t = np.clip((z - 0.24) / 0.20, 0, 1)
        return w * u + (w - w * u) * t
    return f


# ====================================================================== LMGT3
GT = {
    #                 layout   nose  bonnet tail  lights   wing_h  bulge
    'ardenza296':    ('mid',   0.50, 0.66, 0.88, 'slim',  1.30, 0.00),
    'zuffen911':     ('rear',  0.52, 0.70, 0.84, 'round', 1.28, 0.00),
    'isarm4':        ('front', 0.60, 0.82, 0.92, 'slim',  1.34, 0.03),
    'kentuckyz6':    ('mid',   0.52, 0.68, 0.90, 'slim',  1.30, 0.00),
    'ashcombevanguard':('front',0.58, 0.80, 0.90, 'slim', 1.32, 0.03),
    'woking720':     ('mid',   0.48, 0.64, 0.86, 'eye',   1.29, 0.00),
    'sterneamg':     ('front', 0.62, 0.84, 0.94, 'slim',  1.34, 0.05),
    'kotobukigr':    ('front', 0.58, 0.80, 0.90, 'eye',   1.31, 0.02),
    'dearbornstallion':('front',0.64, 0.84, 0.92, 'bar',  1.33, 0.05),
    'torotgt':       ('mid',   0.48, 0.64, 0.86, 'eye',   1.28, 0.00),
    'careergt3':     ('front', 0.58, 0.80, 0.90, 'slim',  1.32, 0.02),
}


def gt_spec(cid, spec):
    lay, nose, bonnet, tail, lights, wing_h, bulge = GT.get(cid, GT['careergt3'])
    L = spec['L']
    cab = dict(front=-0.32, mid=0.14, rear=-0.10)[lay]
    spec.update(layout=lay, nose=nose, bonnet=bonnet, tail=tail, lights=lights, wing_z=wing_h, bulge=bulge, cab=cab,
                rear_glass=dict(front=1.62, mid=1.15, rear=1.95)[lay], top_panel=cab - 0.25, panel=(cab - 0.02, 0.56),
                bonnet_x=spec['axles'][0] - 0.05, wing_span=0.88)
    return spec


def gt_fns(spec):
    L = spec['L']; xf = L / 2; xr = -L / 2
    fa, ra = spec['axles']
    cab = spec['cab']; nose = spec['nose']; belt = 0.88; tail = spec['tail']; lay = spec['layout']
    flare = lambda x: 0.07 * (np.exp(-((x - fa) / 0.36) ** 2) + 1.15 * np.exp(-((x - ra) / 0.40) ** 2))
    Wb = curve([(xf, 0.72), (xf - 0.12, 0.88), (xf - 0.45, 0.92), (xr + 0.35, 0.92), (xr, 0.86)])
    W = lambda x: float(Wb(x) + flare(x))
    # the top of the body (bonnet, the belt along the cabin, the rear deck)
    if lay == 'rear':
        top = curve([(xf, nose), (xf - 0.10, nose + 0.06), (fa, spec['bonnet']), (cab + 0.80, belt - 0.03), (cab + 0.4, belt),
                     (cab - 1.0, belt + 0.02), (ra + 0.10, tail + 0.04), (xr + 0.12, tail), (xr, tail - 0.08)])
    else:
        top = curve([(xf, nose), (xf - 0.10, nose + 0.05), (fa, spec['bonnet']), (cab + 0.80, belt - 0.02), (cab + 0.4, belt),
                     (cab - 1.0, belt + 0.01), (cab - 1.75, tail + 0.02), (xr + 0.12, tail), (xr, tail - 0.05)])
    fend = lambda x: float(top(x) + 0.05 * (np.exp(-((x - fa) / 0.40) ** 2) + np.exp(-((x - ra) / 0.42) ** 2)))
    return W, top, fend


def gt_ring(x, spec, fns, lod):
    W, top, fend = fns
    fa, ra = spec['axles']
    w = W(x); t = top(x); f = fend(x)
    zb = 0.08
    arch = in_arch(x, (fa, ra))
    za = arch_z(x, (fa, ra), 0.20) if arch else 0.20
    yi = 0.60 if arch else w * 0.90
    bulge = spec['bulge'] * float(np.exp(-((x - (fa - 0.25)) / 0.55) ** 2))     # the bonnet's power bulge
    half = [(0.0, zb), (yi, zb), (yi, za if arch else zb + 0.02), (w * (1.0 if arch else 0.97), za if arch else 0.20),
            (w, max(za + 0.02, 0.50) if arch else 0.50), (w * 0.985, f - 0.12), (w * 0.93, f - 0.025), (w * 0.84, f),
            (w * 0.62, t + 0.01 + bulge * 0.5), (w * 0.30, t + bulge), (0.0, t + bulge)]
    if lod >= 2:
        half = [half[i] for i in (0, 1, 2, 3, 4, 6, 8, 10)]
    elif PCSET and lod < 0:
        half = smooth_half(half, 7)
    return mirror_ring(x, half)


def gt_cabin(spec, fns):
    """the glass house: x stations, half widths at the belt and the roof, the roof height"""
    cab = spec['cab']; lay = spec['layout']; L = spec['L']; xr = -L / 2
    W, top, fend = fns
    ws_x = cab + 0.98                                            # the windscreen's base
    if lay == 'mid':
        xs = [ws_x, cab + 0.60, cab + 0.20, cab - 0.25, cab - 0.60, cab - 0.95]
        roof = [top(ws_x) + 0.02, 1.17, 1.26, 1.27, 1.19, top(cab - 0.95) + 0.10]
    elif lay == 'rear':
        xs = [ws_x, cab + 0.60, cab + 0.20, cab - 0.25, cab - 0.80, cab - 1.40, cab - 1.95]
        roof = [top(ws_x) + 0.02, 1.17, 1.27, 1.28, 1.20, 1.05, top(cab - 1.95) + 0.03]
    else:
        xs = [ws_x, cab + 0.60, cab + 0.20, cab - 0.25, cab - 0.75, cab - 1.25, cab - 1.62]
        roof = [top(ws_x) + 0.02, 1.17, 1.27, 1.28, 1.22, 1.07, top(cab - 1.62) + 0.04]
    return xs, roof


def build_gt(spec, lod):
    L = spec['L']; xf = L / 2; xr = -L / 2
    fa, ra = spec['axles']
    fns = gt_fns(spec)
    W, top, fend = fns
    m = Mesh(L)
    n_body = {-1: 96 if PCSET else 28, 0: 15, 2: 7}[lod]
    xs = stations(xf, xr, (fa, ra) if lod < 2 else (), n_body)
    m.loft([gt_ring(x, spec, fns, lod) for x in xs])
    # the glass house: belt -> roof, glass on the windscreen, side windows and rear glass, the roof painted
    cxs, roof = gt_cabin(spec, fns)
    rings = []
    for x, rz in zip(cxs, roof):
        belt = top(x) - 0.01
        wb = W(x) * 0.86; wr = W(x) * 0.62
        half = [(wb, belt), (wb - 0.06, belt + (rz - belt) * 0.55), (wr, rz - 0.015), (wr * 0.5, rz), (0.0, rz)]
        rings.append(np.array([(x, y, z) for y, z in half] + [(x, -y, z) for y, z in half[-2::-1]]))
    # ring points: 0 belt, 1 mid glass, 2 roof edge, 3, 4 roof, 5, 6 roof edge, 7 mid glass, 8 belt (other side);
    # quad k joins point k to k + 1 (k 8: across the belt, the glass house's floor)
    def glass(i, k):
        if k == 8:
            return False
        if i == 0:
            return True                             # the windscreen
        if i == len(cxs) - 2 and spec['layout'] != 'mid':
            return True                             # the rear glass
        return k in (0, 1, 6, 7)                    # the side windows
    m.loft(rings, cap_front=False, cap_back=spec['layout'] == 'mid', glass=glass)
    # the headliner: a dark lining just under the roof, so from the seat the roof is black, not the livery
    if lod < 2:
        lining = []
        for r in rings[1:]:
            row = [np.array(r[k]) * np.array([1, 0.97, 1]) - np.array([0, 0, 0.015]) for k in range(1, 8)]
            lining.append(row)
        m.sheet(lining, uv='carbon', shade=0.35)
    if lod >= 2:
        _gt_wing(m, spec, fns, lod)
        return m
    _gt_wing(m, spec, fns, lod)
    # splitter, canards, bonnet vents, side skirts, diffuser, lights, mirrors
    m.box((xf - 0.28, -0.90, 0.07), (xf + 0.05, 0.90, 0.095), uv='carbon')
    for s in (-1, 1):
        for k in range(2):
            x0 = xf - 0.18 - k * 0.12
            m.quad((x0 + 0.10, s * 0.78, 0.30 + k * 0.08), (x0 - 0.10, s * (W(x0) + 0.04), 0.33 + k * 0.08), (x0 - 0.10, s * (W(x0) + 0.04), 0.345 + k * 0.08), (x0 + 0.10, s * 0.78, 0.315 + k * 0.08), uv='carbon')
        # side skirt
        m.box((fa - 0.42, s * W(0.0) - 0.04 if s > 0 else -W(0.0) - 0.03, 0.08), (ra + 0.44, s * W(0.0) + 0.03 if s > 0 else -W(0.0) + 0.04, 0.16), uv='carbon')
        # fender louvres behind the front wheels' tops
        for k in range(4 if lod < 0 else 2):
            xl = fa + 0.10 - k * 0.11
            yl0, yl1 = s * W(xl) * 0.70, s * W(xl) * 0.90
            zl = fend(xl) + 0.004
            m.quad((xl, yl0, zl), (xl - 0.05, yl0, zl - 0.004), (xl - 0.05, yl1, zl - 0.02), (xl, yl1, zl - 0.016), uv='carbon', shade=0.35)
        _gt_lights(m, spec, fns, s)
        # mirror on the door
        mx = spec['cab'] + 0.78; my = s * W(mx) * 0.88; mz = top(mx) + 0.04
        if lod < 0:
            m.mirrors = getattr(m, 'mirrors', []) + [(mx - 0.06 - 0.003, my + s * 0.068, mz + 0.09, 0.062, 0.034)]
        m.box((mx - 0.03, my - 0.012, mz - 0.02), (mx + 0.02, my + 0.012, mz + 0.06), uv='carbon')
        m.box((mx - 0.06, my - 0.004 if s > 0 else my - 0.14, mz + 0.05), (mx + 0.04, my + 0.14 if s > 0 else my + 0.004, mz + 0.13), uv='paint' if lod < 0 else 'carbon')
    bx = spec['bonnet_x']
    for k in range(3 if lod < 0 else 2):
        x0 = bx - 0.05 - k * 0.12
        m.quad((x0, -0.30, top(x0) + 0.006), (x0, 0.30, top(x0) + 0.006), (x0 - 0.06, 0.30, top(x0 - 0.06) + 0.006), (x0 - 0.06, -0.30, top(x0 - 0.06) + 0.006), uv='carbon', shade=0.35)
    m.quad((xr - 0.004, -0.80, 0.09), (xr - 0.004, 0.80, 0.09), (xr - 0.004, 0.80, 0.34), (xr - 0.004, -0.80, 0.34), uv='carbon', shade=0.5)
    for k in range(-2, 3):
        m.box((xr - 0.02, k * 0.28 - 0.008, 0.08), (xr + 0.30, k * 0.28 + 0.008, 0.32), uv='carbon')
    # tail lights: a bar across the tail (an LED strip on most, round pairs on the rear-engined car)
    zt = top(xr + 0.05) - 0.10
    if spec['lights'] == 'round':
        for s in (-1, 1):
            m.quad((xr - 0.006, s * 0.55, zt - 0.03), (xr - 0.006, s * 0.80, zt - 0.03), (xr - 0.006, s * 0.80, zt + 0.02), (xr - 0.006, s * 0.55, zt + 0.02), uv='red', shade=1.4)
    m.quad((xr - 0.006, -0.82, zt + 0.02), (xr - 0.006, 0.82, zt + 0.02), (xr - 0.006, 0.82, zt + 0.045), (xr - 0.006, -0.82, zt + 0.045), uv='red', shade=1.4)
    m.quad((xr - 0.006, -0.05, 0.36), (xr - 0.006, 0.05, 0.36), (xr - 0.006, 0.05, 0.45), (xr - 0.006, -0.05, 0.45), uv='red', shade=1.4)
    if lod < 0:
        m.box((xr - 0.03, -0.03, 0.30), (xr + 0.01, 0.03, 0.34), uv='yellow')
        rz = max(gt_cabin(spec, fns)[1])
        m.box((spec['cab'] - 0.35, -0.004, rz), (spec['cab'] - 0.29, 0.004, rz + 0.12), uv='carbon')    # antenna
    if lod < 0 and PCSET:
        # the PC's close-up details: twin exhausts under the tail, the refuelling coupling in the rear
        # side window, the roof's position lights, the front tow eye
        for s in (-1, 1):
            m.cyl((xr + 0.22, s * 0.36, 0.20), (xr + 0.002, s * 0.36, 0.20), 0.048, sides=12, uv='grey', shade=1.1)
            m.cyl((xr + 0.004, s * 0.36, 0.20), (xr + 0.24, s * 0.36, 0.20), 0.036, sides=12, uv='carbon', shade=0.3, caps=False)   # the dark inside
        xs_, roof = gt_cabin(spec, fns)
        xfc = spec['cab'] - 0.55; zfc = top(xfc) + 0.12
        m.cyl((xfc, W(xfc) * 0.80, zfc), (xfc, W(xfc) * 0.80 + 0.03, zfc), 0.04, sides=12, uv='grey', shade=1.1)
        for s in (-1, 1):
            m.box((spec['cab'] + 0.05, s * 0.10 - 0.012, rz - 0.004), (spec['cab'] + 0.12, s * 0.10 + 0.012, rz + 0.012), uv='red' if s > 0 else 'white', shade=1.3)
        m.tube([(xf - 0.02, 0.25, 0.16), (xf + 0.015, 0.25, 0.16), (xf + 0.015, 0.25, 0.20)], 0.009, sides=6, uv='yellow')
    return m


def _gt_wing(m, spec, fns, lod):
    L = spec['L']; xr = -L / 2
    W, top, fend = fns
    wz = spec['wing_z']; ws = spec['wing_span']
    x0 = xr + 0.10
    m.box((x0, -ws, wz), (x0 + 0.30, ws, wz + 0.03), uv='carbon')
    if lod < 2:
        m.box((x0 - 0.04, -ws, wz + 0.035), (x0 + 0.10, ws, wz + 0.055), uv='carbon')
        for s in (-1, 1):
            m.box((x0 - 0.05, s * ws - 0.008, wz - 0.12), (x0 + 0.34, s * ws + 0.008, wz + 0.08), uv='second')
            # swan-neck uprights from the boot lid
            yb = s * 0.42
            m.tube([(x0 + 0.40, yb, top(x0 + 0.40)), (x0 + 0.28, yb, wz + 0.07), (x0 + 0.16, yb, wz + 0.07)], 0.012, uv='carbon')


def _gt_lights(m, spec, fns, s):
    W, top, fend = fns
    L = spec['L']; xf = L / 2
    st = spec['lights']
    x0 = xf - 0.14
    y0 = s * W(x0) * 0.74
    z0 = top(x0) - 0.03
    if st == 'round':
        for k in range(6):
            a0, a1 = k * np.pi / 3, (k + 1) * np.pi / 3
            m.part(np.array([[x0 + 0.01, y0, z0], [x0, y0 + 0.09 * np.cos(a0), z0 + 0.09 * np.sin(a0)], [x0, y0 + 0.09 * np.cos(a1), z0 + 0.09 * np.sin(a1)]]),
                   [[0, 1, 2]], uv='white', flat=True, shade=1.5)
    elif st == 'eye':                                 # a narrow swept "eye" with a vertical slot (mid-engined cars)
        m.quad((x0 + 0.04, y0 - s * 0.16, z0), (x0 + 0.04, y0 + s * 0.06, z0 - 0.02), (x0 - 0.08, y0 + s * 0.12, z0 + 0.02), (x0 - 0.04, y0 - s * 0.10, z0 + 0.04), uv='white', shade=1.5)
    elif st == 'bar':                                 # a three-bar signature and a light bar between them
        for k in range(3):
            yy = y0 - s * (0.08 - k * 0.06)
            m.quad((x0 + 0.03, yy, z0 - 0.04), (x0 + 0.03, yy + s * 0.02, z0 - 0.04), (x0 + 0.02, yy + s * 0.02, z0 + 0.05), (x0 + 0.02, yy, z0 + 0.05), uv='white', shade=1.5)
    else:                                             # slim LED units
        m.quad((x0 + 0.04, y0 - s * 0.14, z0 - 0.01), (x0 + 0.04, y0 + s * 0.10, z0 - 0.015), (x0 + 0.01, y0 + s * 0.10, z0 + 0.03), (x0 + 0.01, y0 - s * 0.14, z0 + 0.035), uv='white', shade=1.5)


def gt_side_w(spec, z=0.50):
    W, top, fend = gt_fns(spec)
    return lambda x: W(x)


# ====================================================================== cockpits (Art. 13 of the LMH / LMDh rules)
# Cabin coordinates: metres, +X forward, +Y left, +Z up from the ground; the reference plane of the survival
# cell (Zsc) sits on the floor at z 0.05. They match COCKPIT in src/render.cpp and the dash displays in
# Renderer::drawNewInstruments.
ZSC = 0.05
XSC = -0.62                      # Xsc: the front face of the rear rollover structure
DRV_Y = 0.17                     # the driver sits on the left; wheel centre >= 165 mm (LMDh) / 150 mm (LMH) out
PROTO_EYE = (-0.30, DRV_Y, 0.78)
PROTO_WHEEL = (0.15, DRV_Y, 0.612)


def proto_interior():
    """The prototype cockpit to Art. 13 / 14 / 15.1: a two-place carbon survival cell, the driver on the
    left; H2 leg volumes for both occupants (355 mm wide, 350 mm high, floor <= Zsc + 200, front face
    1570 mm ahead of Xsc); H3/H4 room up to Zsc + 500 with the cell's sides at least that high; the
    headrest padding 85 mm ahead of Xsc; the wheel's top >= Zsc + 650 and the dash >= 50 mm from it; front
    and rear rollover structures (>= Zsc + 935 / 945, >= 600 mm apart); the ESS compartment, auxiliary
    battery and electronics in the passenger place; pedals >= 250 mm behind the front bulkhead."""
    m = Mesh(5.0)
    cw, ct = canopy_fns({})
    yd = DRV_Y
    floor = ZSC + 0.012
    x_ped = XSC + 1.57               # H2's forward face: the foremost pedal
    x_bulk = x_ped + 0.30            # the front bulkhead of the cell
    sill = ZSC + 0.56                # the cell's sides beside H3 (>= Zsc + 500)
    # the survival cell: an open U section from the rear bulkhead (Xsc) to the front bulkhead, its sides rolling
    # over at the top into the door openings; narrowing into the foot wells
    rings = []
    for x in np.linspace(XSC, x_bulk, 12):
        f = float(sstep(0.25, x_ped, x))
        hw = 0.50 - 0.07 * f                         # half width inside the cell
        sz = sill + 0.05 * f                         # the sills rise a little towards the dash
        prof = [(-hw - 0.06, sz - 0.03), (-hw - 0.03, sz + 0.02), (-hw, sz), (-hw, floor + 0.10), (-hw + 0.06, floor),
                (hw - 0.06, floor), (hw, floor + 0.10), (hw, sz), (hw + 0.03, sz + 0.02), (hw + 0.06, sz - 0.03)]
        rings.append([(x, y, z) for y, z in prof])
    m.sheet(rings, uv='carbon', shade=0.75)
    m.quad((XSC, -0.50, floor), (XSC, 0.50, floor), (XSC, 0.50, sill), (XSC, -0.50, sill), uv='carbon', shade=0.55)   # rear bulkhead
    m.quad((x_bulk, -0.43, floor), (x_bulk, 0.43, floor), (x_bulk, 0.43, sill + 0.05), (x_bulk, -0.43, sill + 0.05), uv='carbon', shade=0.5)
    # the central tunnel between the two leg volumes (their inboard faces are symmetric, not overlapping)
    m.box((0.0, -0.012, floor), (x_bulk, 0.012, floor + 0.20), uv='carbon', shade=0.6)
    # orange trim along both sills and round the driver's foot well
    for s in (-1, 1):
        m.quad((XSC + 0.05, s * 0.505, sill + 0.005), (x_bulk - 0.05, s * 0.435, sill + 0.055),
               (x_bulk - 0.05, s * 0.448, sill + 0.058), (XSC + 0.05, s * 0.518, sill + 0.008), uv='orange', shade=1.5)
    m.quad((0.25, yd - 0.18, floor + 0.002), (x_ped, yd - 0.18, floor + 0.002), (x_ped, yd - 0.165, floor + 0.002), (0.25, yd - 0.165, floor + 0.002), uv='orange', shade=1.4)
    m.quad((0.25, yd + 0.165, floor + 0.002), (x_ped, yd + 0.165, floor + 0.002), (x_ped, yd + 0.18, floor + 0.002), (0.25, yd + 0.18, floor + 0.002), uv='orange', shade=1.4)
    # leg padding either side of the driver's leg volume (Art. 14.7), clear of the 355 mm volume
    for s in (-1, 1):
        y0 = yd + s * 0.18
        m.box((0.05, min(y0, y0 + s * 0.05), floor + 0.10), (x_ped - 0.05, max(y0, y0 + s * 0.05), floor + 0.33), uv='grey', shade=0.65)
    # the dash cowl: its face towards the driver 50 mm clear of the wheel (the wheel's plate ends at x 0.19)
    x_dash = PROTO_WHEEL[0] + 0.04 + 0.06
    cow = []
    for x in np.linspace(x_dash, x_bulk, 6):
        prof = []
        for y in np.linspace(-0.44, 0.44, 13):
            hump = 0.03 * np.exp(-((y - yd) / 0.20) ** 2)            # over the driver's display
            prof.append((x, y, sill + 0.02 + hump + 0.02 * float(sstep(x_dash, 0.7, x)) - 0.05 * (abs(y) / 0.44) ** 2))
        cow.append(prof)
    m.sheet(cow, uv='grey', shade=0.85)
    m.sheet([[(x_dash, y, floor + 0.36) for y in np.linspace(-0.44, 0.44, 13)], cow[0]], uv='carbon', shade=0.6)
    m.quad((x_dash - 0.005, yd - 0.24, sill + 0.035), (x_dash - 0.005, yd + 0.24, sill + 0.035),
           (x_dash - 0.005, yd + 0.24, sill + 0.047), (x_dash - 0.005, yd - 0.24, sill + 0.047), uv='orange', shade=1.6)
    # the housing behind the driver's display (the game draws the display on its face at x 0.43)
    m.box((0.455, yd - 0.19, 0.65), (0.56, yd + 0.19, 0.735), uv='carbon', shade=0.55)
    # switch panels either side of the wheel, the master switch (Art. 13.20: a red spark in a blue triangle) and the
    # extinguisher button on the dash
    for s in (-1, 1):
        y0 = yd + s * 0.27
        m.box((x_dash, y0 - 0.06, floor + 0.38), (x_dash + 0.06, y0 + 0.06, floor + 0.52), uv='carbon', shade=0.65)
        for k in range(3):
            yk = y0 + s * (k - 1) * 0.035
            m.box((x_dash - 0.008, yk - 0.010, floor + 0.45), (x_dash, yk + 0.010, floor + 0.47), uv=('green', 'red', 'blue')[k], shade=1.5)
    m.part(np.array([[x_dash - 0.006, yd - 0.39, sill - 0.03], [x_dash - 0.006, yd - 0.36, sill - 0.03], [x_dash - 0.006, yd - 0.375, sill - 0.005]]),
           [[0, 1, 2]], uv='blue', flat=True, shade=1.5)
    m.cyl((x_dash, yd - 0.33, sill - 0.02), (x_dash - 0.012, yd - 0.33, sill - 0.02), 0.014, sides=12, uv='red', shade=1.3)
    # the driver's seat on its own centre line: back, cushion, side bolsters with orange edges
    sx = XSC + 0.10
    m.quad((sx, yd - 0.20, floor + 0.05), (sx, yd + 0.20, floor + 0.05), (sx + 0.14, yd + 0.20, floor + 0.62), (sx + 0.14, yd - 0.20, floor + 0.62), uv='grey', shade=0.7)
    m.quad((sx + 0.02, yd - 0.20, floor + 0.06), (sx + 0.02, yd + 0.20, floor + 0.06), (0.05, yd + 0.20, floor + 0.12), (0.05, yd - 0.20, floor + 0.12), uv='grey', shade=0.75)
    for s in (-1, 1):
        m.box((sx, yd + s * 0.22 - 0.035, floor + 0.05), (0.05, yd + s * 0.22 + 0.035, floor + 0.26), uv='carbon', shade=0.7)
        m.box((sx + 0.02, yd + s * 0.22 - 0.04, floor + 0.26), (0.05, yd + s * 0.22 + 0.04, floor + 0.275), uv='orange', shade=1.3)
    # the headrest padding (Art. 14.6): behind the helmet, its front 85 mm ahead of Xsc, and either side of it
    hz = PROTO_EYE[2]
    m.box((XSC, yd - 0.13, hz - 0.16), (XSC + 0.085, yd + 0.13, hz + 0.08), uv='blue', shade=0.75)
    for s in (-1, 1):
        m.box((XSC, yd + s * 0.17 - 0.05, hz - 0.20), (XSC + 0.26, yd + s * 0.17 + 0.05, hz - 0.02), uv='blue', shade=0.65)
    # harness: two shoulder straps over the seat back
    for y in (yd - 0.08, yd + 0.08):
        m.quad((sx + 0.13, y - 0.022, floor + 0.60), (sx + 0.13, y + 0.022, floor + 0.60), (sx + 0.38, y + 0.022, floor + 0.20),
               (sx + 0.38, y - 0.022, floor + 0.20), uv='red', shade=1.0)
    # the passenger place: the ESS compartment's top, the auxiliary battery and the electronics (Art. 13.6.2)
    yp = -yd
    m.box((XSC + 0.05, yp - 0.20, floor), (0.10, yp + 0.20, floor + 0.20), uv='carbon', shade=0.55)
    m.box((XSC + 0.05, yp - 0.185, floor + 0.20), (0.10, yp + 0.185, floor + 0.205), uv='orange', shade=1.2)   # the HV warning edge
    m.box((0.15, yp - 0.12, floor), (0.42, yp + 0.06, floor + 0.16), uv='grey', shade=0.6)
    for k in range(3):
        m.box((0.48 + k * 0.12, yp - 0.13, floor), (0.58 + k * 0.12, yp + 0.10, floor + 0.11), uv='carbon', shade=0.7)
    m.cyl((XSC + 0.12, yp + 0.16, floor + 0.21), (XSC + 0.12, yp + 0.16, floor + 0.48), 0.06, sides=12, uv='red', shade=1.1)   # extinguisher
    # the rollover structures: the front one (the canopy's front hoop) and the rear one at Xsc, both padded in
    # the middle, and the canopy frame between them
    r = 0.030
    xfh = XSC + 0.64
    for x, top in ((xfh, ZSC + 0.935), (XSC + 0.02, ZSC + 0.945)):
        z = min(top, ct(x) - 0.035)
        w_ = cw(x) - 0.05
        m.tube([(x, -w_, sill), (x, -w_ * 0.80, z - 0.05), (x, 0.0, z), (x, w_ * 0.80, z - 0.05), (x, w_, sill)], r * 1.2, sides=8, uv='grey', shade=0.9)
    for s in (-1, 1):
        pts = [(x, s * (cw(x) - 0.03), sill + 0.06 + (ct(x) - sill) * 0.12) for x in np.linspace(1.0, 0.62, 3)]
        pts += [(x, s * (cw(x) - 0.045), ct(x) - 0.05) for x in np.linspace(0.40, XSC, 6)]
        m.tube(pts, r, sides=8, uv='grey', shade=0.85)
    m.tube([(x, 0.0, ct(x) - 0.04) for x in np.linspace(xfh, XSC, 5)], r * 1.1, sides=8, uv='grey', shade=0.95)
    # the rear-view camera screen on the passenger side of the dash (the game draws its picture on the face)
    m.box((0.50, -0.33, 0.64), (0.54, -0.11, 0.72), uv='carbon', shade=0.5)
    # the pedals in the driver's foot well: throttle, brake (no clutch pedal: paddle clutch)
    for k, y in enumerate((yd - 0.07, yd + 0.07)):
        m.box((x_ped, y - 0.04, floor + 0.04), (x_ped + 0.03, y + 0.04, floor + 0.17), uv='grey' if k == 0 else 'carbon', shade=0.8)
    m.box((x_ped - 0.10, yd - 0.17, floor), (x_ped - 0.02, yd + 0.17, floor + 0.03), uv='rubber', shade=0.6)     # foot rest
    # the steering column from the wheel to the bulkhead
    m.tube([(PROTO_WHEEL[0] + 0.04, yd, PROTO_WHEEL[2] - 0.01), (x_bulk, yd, floor + 0.42)], 0.022, sides=8, uv='grey', shade=0.6)
    return m


def mirror_y(m):
    """swap a mesh left for right (the GT3 interior was laid out right-hand drive)"""
    out = []
    for P, n, u, g, fn, sh in m.tris:
        P = P[[0, 2, 1]] * np.array([1, -1, 1]); n = n[[0, 2, 1]] * np.array([1, -1, 1])
        if not isinstance(u, str):
            u = u[[0, 2, 1]]
        out.append((P, n, u, g, fn * np.array([1, -1, 1]), sh))
    m.tris = out
    return m


GT_EYE = (-0.28, 0.38, 1.05)     # LMGT3: left-hand drive, as all the WEC cars run
GT_WHEEL = (0.14, 0.38, 0.86)


def gt_interior():
    """the LMGT3 cockpit: the production car's cabin with the FIA GT3 cage, seat, window net and the
    compulsory rear-view camera screen; left-hand drive"""
    m = mirror_y(G.hd_gt_interior())
    m.box((0.66, -0.13, 1.045), (0.70, 0.13, 1.115), uv="grey", shade=0.45)    # the rear-view camera screen
    return m
