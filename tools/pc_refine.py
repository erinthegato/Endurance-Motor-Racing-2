"""The PC edition's higher-detail models: curved-triangle refinement for any of the tools' meshes.

The PC edition (src/platform_desktop.cpp) reads PC/models before HD/models (the Vita's set). The
asset tools write that set when they run with --pc (or EMR_PC=1 in the environment):
    python3 tools/gen_wec.py --pc        cars, wheels, cockpits, steering wheels
    python3 tools/gen_props.py --pc      the code-made buildings and props
    python3 tools/gen_trackside.py --pc  the Blender-made trackside objects
    python3 tools/gen_crew.py --pc       the pit crew (every pose)
    python3 tools/gen_cockpit.py --pc    the driver
    python3 tools/gen_textures.py --pc   ground textures (PC/tex)
    python3 tools/build_pc_set.py        all of the above, then packs them (tools/pack_files.py)

refine(): each triangle becomes four, and the three new corners sit on the curved surface that
the corners' normals describe (Vlachos et al., "Curved PN Triangles", 2001):
  * a flat face (all three normals the face's own) stays exactly flat: boxes, boards and walls keep
    their crisp edges;
  * a smooth surface (normals shared between neighbours) bulges onto its curve: bodywork, canopies,
    helmets, tyres, tree crowns and the crew's bodies get rounder, and the baked lighting (worked out
    from the normals) gets smoother;
  * an edge whose two end normals are more than CREASE degrees apart is a crease (a box's corner,
    where one part's normals were averaged over a sharp edge): its new corner is the straight
    midpoint. The decision uses only the edge's own two corners, so both triangles on an edge agree
    and no gaps open between them.
"""
import os
import sys
import numpy as np

CREASE = 50.0                     # degrees between an edge's two normals beyond which it stays straight
_COS = np.cos(np.radians(CREASE))


def pc_mode():
    """True when a tool is building the PC edition's set (--pc on the command line, or EMR_PC=1)"""
    if '--pc' in sys.argv:
        sys.argv.remove('--pc')
        os.environ['EMR_PC'] = '1'
    return os.environ.get('EMR_PC') == '1'


def root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def pc_dir(*sub):
    """PC/<sub...>, made if needed"""
    p = os.path.join(root(), 'PC', *sub)
    os.makedirs(p, exist_ok=True)
    return p


def _unit(v):
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-12)


def _edge_point(pa, pb, na, nb):
    """the middle of the curved edge a-b (straight where the edge is a crease)"""
    if float(np.dot(na, nb)) < _COS:
        return (pa + pb) * 0.5, _unit(na + nb)
    wab = float(np.dot(pb - pa, na)); wba = float(np.dot(pa - pb, nb))
    bab = (2 * pa + pb - wab * na) / 3.0
    bba = (2 * pb + pa - wba * nb) / 3.0
    mid = (pa + 3 * bab + 3 * bba + pb) / 8.0
    # the normal halfway: the average, reflected across the edge's mid-plane (PN triangles' quadratic normal)
    d = pb - pa; dd = float(np.dot(d, d))
    nab = na + nb
    if dd > 1e-18:
        nab = nab - 2.0 * float(np.dot(d, na + nb)) / dd * d
    return mid, _unit(nab)


def is_flat(N):
    """all three corner normals the same (a flat-shaded face): splitting it would add nothing you can see"""
    N = _unit(np.asarray(N, float))
    return float(np.dot(N[0], N[1])) > 0.99995 and float(np.dot(N[0], N[2])) > 0.99995


def split(P, N, UV, keep_flat=True):
    """one triangle (P 3x3, N 3x3, UV 3x2 or None) -> four (P, N, UV) triangles (a flat one stays as it
    is unless keep_flat is False: its neighbours' shared edges stay straight, so no gaps open)"""
    if keep_flat and is_flat(N):
        return [(np.asarray(P, float), np.asarray(N, float), UV)]
    P = np.asarray(P, float); N = _unit(np.asarray(N, float))
    m01, n01 = _edge_point(P[0], P[1], N[0], N[1])
    m12, n12 = _edge_point(P[1], P[2], N[1], N[2])
    m20, n20 = _edge_point(P[2], P[0], N[2], N[0])
    pts = [P[0], P[1], P[2], m01, m12, m20]
    nrm = [N[0], N[1], N[2], n01, n12, n20]
    if UV is not None and not isinstance(UV, str):
        U = np.asarray(UV, float)
        uvs = [U[0], U[1], U[2], (U[0] + U[1]) / 2, (U[1] + U[2]) / 2, (U[2] + U[0]) / 2]
    else:
        uvs = None
    out = []
    for a, b, c in ((0, 3, 5), (3, 1, 4), (5, 4, 2), (3, 4, 5)):
        out.append((np.array([pts[a], pts[b], pts[c]]), np.array([nrm[a], nrm[b], nrm[c]]),
                    UV if uvs is None else np.array([uvs[a], uvs[b], uvs[c]])))
    return out


def _key(p):
    return (round(float(p[0]), 5), round(float(p[1]), 5), round(float(p[2]), 5))


def _refine_once(tris, uv_index, n_index, keep_flat):
    # every edge's new point, worked out once for the whole mesh: if any triangle on the edge curves it,
    # all of them use that point (a flat face beside a curved one -- a cylinder's cap -- then splits too,
    # so the two still meet with no gap)
    edges = {}
    for t in tris:
        P = np.asarray(t[0], float); N = _unit(np.asarray(t[n_index], float))
        for i, j in ((0, 1), (1, 2), (2, 0)):
            k = tuple(sorted((_key(P[i]), _key(P[j]))))
            mid, _ = _edge_point(P[i], P[j], N[i], N[j])
            lin = (P[i] + P[j]) * 0.5
            curved = float(np.linalg.norm(mid - lin)) > 1e-7
            e = edges.setdefault(k, [None, 0])
            if curved:
                e[0] = mid if e[0] is None else (e[0] * e[1] + mid) / (e[1] + 1); e[1] += 1
    out = []
    for t in tris:
        P = np.asarray(t[0], float); N = _unit(np.asarray(t[n_index], float)); UV = t[uv_index]
        keys = [tuple(sorted((_key(P[i]), _key(P[j])))) for i, j in ((0, 1), (1, 2), (2, 0))]
        bent = [edges[k][0] is not None for k in keys]
        if keep_flat and is_flat(N) and not any(bent):
            out.append(t)
            continue
        if keep_flat and is_flat(N) and sum(bent) < 3:
            # a flat face with one or two curved edges: split only towards those edges (a fan), so its
            # straight edges get no new points that a flat neighbour wouldn't have (no T-junctions)
            uvs = None if (UV is None or isinstance(UV, str)) else np.asarray(UV, float)
            def mk(ps, us):
                c = list(t); c[0] = np.array(ps); c[n_index] = N.copy()
                c[uv_index] = UV if uvs is None else np.array(us)
                return tuple(c)
            e = [m for m in range(3) if bent[m]]
            if len(e) == 1:
                m = e[0]; i, j, o = m, (m + 1) % 3, (m + 2) % 3
                q = edges[keys[m]][0]
                uq = None if uvs is None else (uvs[i] + uvs[j]) / 2
                out.append(mk([P[i], q, P[o]], None if uvs is None else [uvs[i], uq, uvs[o]]))
                out.append(mk([q, P[j], P[o]], None if uvs is None else [uq, uvs[j], uvs[o]]))
            else:
                # two curved edges: they meet at corner c; the third (straight) edge a-b stays whole
                m0, m1 = e
                # the corner shared by both curved edges
                ends0 = {m0, (m0 + 1) % 3}; ends1 = {m1, (m1 + 1) % 3}
                c = (ends0 & ends1).pop(); a = (ends0 - {c}).pop(); b = (ends1 - {c}).pop()
                qa = edges[keys[m0]][0]; qb = edges[keys[m1]][0]
                ua = None if uvs is None else (uvs[c] + uvs[a]) / 2
                ub = None if uvs is None else (uvs[c] + uvs[b]) / 2
                # keep the winding of the original triangle: order corners as they were (c, then the next)
                order = [c, (c + 1) % 3, (c + 2) % 3]
                q_next = qa if order[1] == a else qb; u_next = ua if order[1] == a else ub
                q_prev = qb if order[1] == a else qa; u_prev = ub if order[1] == a else ua
                n1, n2 = order[1], order[2]
                L = lambda *x: None if uvs is None else list(x)
                out.append(mk([P[c], q_next, q_prev], L(uvs[c] if uvs is not None else None, u_next, u_prev)))
                out.append(mk([q_next, P[n1], P[n2]], L(u_next, uvs[n1] if uvs is not None else None, uvs[n2] if uvs is not None else None)))
                out.append(mk([q_next, P[n2], q_prev], L(u_next, uvs[n2] if uvs is not None else None, u_prev)))
            continue
        kids = split(P, N, UV, keep_flat=False)
        # the shared edge points (the corner points of the children: 3 = edge 0-1, 4 = 1-2, 5 = 2-0)
        mids = []
        for k, (i, j) in zip(keys, ((0, 1), (1, 2), (2, 0))):
            mids.append(edges[k][0] if edges[k][0] is not None else (P[i] + P[j]) * 0.5)
        lookup = {_key((P[0] + P[1]) * 0.5): mids[0], _key((P[1] + P[2]) * 0.5): mids[1], _key((P[2] + P[0]) * 0.5): mids[2]}
        own = [_edge_point(P[i], P[j], N[i], N[j])[0] for i, j in ((0, 1), (1, 2), (2, 0))]
        remap = {_key(own[m]): mids[m] for m in range(3)}
        for p, n, uv in kids:
            p = np.array([remap.get(_key(q), q) for q in p])
            c = list(t); c[0] = p; c[n_index] = n; c[uv_index] = uv
            out.append(tuple(c))
    return out


def refine_tris(tris, levels=1, uv_index=2, n_index=1, keep_flat=True):
    """refine a tools' triangle list (tuples whose items 0, n_index, uv_index are positions, normals,
    texture coordinates; every other item is copied to the children) `levels` times. Flat faces stay
    single triangles unless a curved neighbour bends one of their edges."""
    for _ in range(levels):
        tris = _refine_once(tris, uv_index, n_index, keep_flat)
    return tris


def refine_part(part, levels=1, keep_flat=True):
    """a Blender-exported part ({'p': flat corner positions, 'n': flat normals, 'm': material per
    triangle, ...}, as in tools/blender/*.json) refined `levels` times; the material follows each child"""
    P = np.array(part['p'], float).reshape(-1, 3, 3)
    N = np.array(part['n'], float).reshape(-1, 3, 3)
    tris = [(P[i], N[i], None, part['m'][i]) for i in range(len(P))]
    tris = refine_tris(tris, levels, uv_index=2, n_index=1, keep_flat=keep_flat)
    out = dict(part)
    out['p'] = [float(x) for t in tris for x in np.asarray(t[0]).reshape(-1)]
    out['n'] = [float(x) for t in tris for x in np.asarray(t[1]).reshape(-1)]
    out['m'] = [t[3] for t in tris]
    out['tris'] = len(tris)
    return out


def auto_smooth(tris, angle=50.0, n_index=1):
    """Blender's "auto smooth" for a triangle list exported flat-shaded (every face its own normal):
    each corner's normal becomes the area-weighted average of the faces around that point that meet
    this face at less than `angle` degrees. Cylinders, tyres, trunks and crowns turn smooth (so
    refine_tris can round them); a box's corners stay sharp."""
    c = np.cos(np.radians(angle))
    fns, areas = [], []
    for t in tris:
        P = np.asarray(t[0], float)
        fn = np.cross(P[1] - P[0], P[2] - P[0]); a = float(np.linalg.norm(fn))
        fns.append(fn / a if a > 1e-15 else np.array([0, 0, 1.0])); areas.append(a)
    around = {}
    for i, t in enumerate(tris):
        for p in np.asarray(t[0], float):
            around.setdefault(_key(p), []).append(i)
    out = []
    for i, t in enumerate(tris):
        P = np.asarray(t[0], float); fi = fns[i]
        N = []
        for p in P:
            s = np.zeros(3)
            for j in set(around[_key(p)]):
                if float(np.dot(fns[j], fi)) > c:
                    s += fns[j] * areas[j]
            N.append(s / np.linalg.norm(s) if np.linalg.norm(s) > 1e-15 else fi)
        tt = list(t); tt[n_index] = np.array(N)
        out.append(tuple(tt))
    return out


# ------------------------------------------------------------------ normals for the PC's real lighting
KEY_LIGHT = np.array([0.45, 0.35, 0.82]) / np.linalg.norm([0.45, 0.35, 0.82])


def baked_light(N):
    """the light the tools bake into vertex colours for a surface facing N (sky from above + key light)"""
    N = _unit(np.asarray(N, float))
    return 0.50 + 0.30 * (N[..., 2] * 0.5 + 0.5) + 0.30 * np.clip(N @ KEY_LIGHT, 0, 1)


def emn_block(lods, scale):
    """'EMN1' + per level of detail, per vertex: int8 normal x, y, z and uint8 baked light. The PC build
    lights the models itself (src/platform_desktop.cpp): it needs which way each corner faces, and how
    much light the tools baked into the vertex colour there (so it can take the old sun out and keep
    the shading that stays true: darker towards the ground, recesses).
      lods: [(N (v,3) model-space normals, glass (v,) mask or None), ...] in the order written
      scale: the model's stored scale (positions are kept as -1..1 of it): the normals are stored for
      the scaled space (n * scale, normalised), so the game's model matrix turns them the right way"""
    out = bytearray(b'EMN1')
    s = np.asarray(scale, float)
    for lod in lods:
        N, glass = lod[0], (lod[1] if len(lod) > 1 else None)
        N = _unit(np.asarray(N, float).reshape(-1, 3))
        ns = _unit(N * s)
        q = np.clip(np.round(ns * 127), -127, 127).astype(np.int8)
        baked = baked_light(N)
        if glass is not None:
            g = np.asarray(glass, bool).reshape(-1)
            gl = 0.55 + 0.6 * np.clip(N[:, 2], 0, 1) + 0.25 * np.clip(N @ KEY_LIGHT, 0, 1)
            baked = np.where(g, gl, baked)
        rec = np.zeros(len(N), dtype=[('x', 'i1'), ('y', 'i1'), ('z', 'i1'), ('a', 'u1')])
        rec['x'] = q[:, 0]; rec['y'] = q[:, 1]; rec['z'] = q[:, 2]
        rec['a'] = np.clip(np.round(baked / 1.5 * 255), 1, 255).astype(np.uint8)   # the game: colour / (a / 255 * 1.5)
        out += rec.tobytes()
    return bytes(out)


# ------------------------------------------------------------------ ambient occlusion (the PC set)
def _fib_dirs(n):
    """n directions spread evenly over the upper hemisphere (z up), cosine-weighted"""
    i = np.arange(n) + 0.5
    r = np.sqrt(i / n); phi = i * np.pi * (3 - np.sqrt(5))
    return np.stack([r * np.cos(phi), r * np.sin(phi), np.sqrt(np.maximum(1 - r * r, 0))], -1)


def vertex_ao(P, N, res=None, reach=None, dirs=32, strength=0.6, ground=True, floor=0.45):
    """How open each corner is to the sky around it: rays from every corner through a voxel copy of
    the model (and the ground under it). Gaps, vents, wheel arches, the underside of wings and roofs
    go dark the way they do in daylight. Returns a factor per corner (m, 3), 1 = fully open.
      P, N: (m, 3, 3) corner positions and normals; res: voxel size (default 1/150 of the model);
      reach: how far a ray looks (default 14 voxels)"""
    P = np.asarray(P, float).reshape(-1, 3, 3); N = _unit(np.asarray(N, float).reshape(-1, 3, 3))
    lo = P.reshape(-1, 3).min(0); hi = P.reshape(-1, 3).max(0)
    ext = float((hi - lo).max())
    res = res or ext / 220.0
    reach = reach or res * 14
    pad = int(np.ceil(reach / res)) + 2
    o = lo - pad * res
    dims = np.ceil((hi - lo) / res).astype(int) + 2 * pad + 1
    occ = np.zeros(dims, bool)
    # the surface into voxels: points spread over every triangle, about two per voxel width
    a, b, c = P[:, 0], P[:, 1], P[:, 2]
    L = np.maximum(np.linalg.norm(b - a, axis=1), np.linalg.norm(c - a, axis=1))
    k = np.clip(np.ceil(L / (res * 0.5)).astype(int), 1, 60)
    for kk in np.unique(k):
        sel = k == kk
        u, v = np.meshgrid(np.arange(kk + 1), np.arange(kk + 1), indexing='ij')
        m = (u + v) <= kk
        u = u[m] / kk; v = v[m] / kk
        pts = a[sel][:, None] + (b[sel] - a[sel])[:, None] * u[None, :, None] + (c[sel] - a[sel])[:, None] * v[None, :, None]
        idx = np.floor((pts.reshape(-1, 3) - o) / res).astype(int)
        occ[idx[:, 0], idx[:, 1], idx[:, 2]] = True
    if ground:      # the road under the model (its lowest point)
        gz = int(np.floor((lo[2] - o[2]) / res)) - 3
        occ[:, :, max(gz, 0)] = True
    D = _fib_dirs(dirs)
    steps = np.arange(0.0, reach + 1e-9, res * 0.9)     # (a voxel at a time: thin shells can't be stepped over)
    # thicken the shells by a voxel so diagonal rays can't slip between surface voxels
    o2 = occ.copy()
    for ax in range(3):
        o2 |= np.roll(occ, 1, ax) | np.roll(occ, -1, ax)
    occ = o2
    Pc = P.reshape(-1, 3); Nc = N.reshape(-1, 3)
    out = np.empty(len(Pc))
    for s in range(0, len(Pc), 20000):
        p = Pc[s:s + 20000]; n = Nc[s:s + 20000]
        # a frame around each normal
        t = np.where(np.abs(n[:, 2:3]) < 0.9, np.array([[0, 0, 1.0]]), np.array([[1.0, 0, 0]]))
        u_ = _unit(np.cross(n, t)); v_ = np.cross(n, u_)
        W = D[None, :, 0:1] * u_[:, None] + D[None, :, 1:2] * v_[:, None] + D[None, :, 2:3] * n[:, None]   # (p, d, 3)
        hit = np.zeros(W.shape[:2], bool)
        start = p + n * res * 2.2           # (clear of its own, thickened shell)
        for st in steps:
            q = start[:, None] + W * st
            idx = np.floor((q - o) / res).astype(int)
            idx = np.clip(idx, 0, dims - 1)
            hit |= occ[idx[..., 0], idx[..., 1], idx[..., 2]]
        vis = 1 - hit.mean(1)
        out[s:s + 20000] = np.maximum(1 - strength * (1 - vis) ** 1.3, floor)
    return out.reshape(-1, 3)
