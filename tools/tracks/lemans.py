"""Assemble the full 24 Hours circuit (Circuit de la Sarthe) from OpenStreetMap ways.

The lap uses the permanent circuit (highway=raceway) and the public roads D338 (Hunaudieres / Mulsanne)
and D140 (Mulsanne -> Indianapolis -> Arnage). We route through the named corners in race order on a
graph of those ways; raceway edges are preferred (3x cheaper) so the route takes the Mulsanne chicanes
instead of the public road beside them, and pit lanes / other circuits are left out.

  python3 lemans.py osm_sablons_b.json [extra OSM API .xml ...] -> prints lat/lon loop as JSON
  (the extra files add the short D139 link from Arnage to the Porsche Curves and the Ford chicane area)
"""
import json, sys, math, heapq

ORDER = ['Chicane Dunlop', 'Esses de la Forêt', 'Tertre Rouge', 'Chicane Michelin', 'Virage de Mulsanne',
         "Virage d'Indianapolis", 'Virage Porsche', 'Virage Corvette', 'Maison Blanche', 'Chicane Ford']
ROAD_REFS = ('D 338', 'D338', 'D 140', 'D140', 'D 139', 'D139')
# shared with the small Maison Blanche club circuit: allowed (one short link needs it) but avoided otherwise
AVOID = ()  # (tried avoiding the Maison Blanche club-circuit link: the lap got longer, so it is part of the real route)
SKIP = ('pit', 'c.i.k', 'alain prost',
        'porsche center', 'moto', 'safety car', 'daytona', 'garage bleu')


def dist(a, b):
    return math.hypot((b[1] - a[1]) * 111320 * math.cos(math.radians(a[0])), (b[0] - a[0]) * 110540)


def load_xml(fn):
    """OSM API XML -> Overpass-like way elements (raceways and the circuit's public roads only)."""
    import xml.etree.ElementTree as ET
    t = ET.parse(fn).getroot()
    nodes = {n.get('id'): (float(n.get('lat')), float(n.get('lon'))) for n in t.findall('node')}
    out = []
    for w in t.findall('way'):
        tags = {x.get('k'): x.get('v') for x in w.findall('tag')}
        if tags.get('highway') != 'raceway' and tags.get('ref') not in ROAD_REFS:
            continue
        ids = [int(n.get('ref')) for n in w.findall('nd') if n.get('ref') in nodes]
        out.append({'type': 'way', 'tags': tags, 'nodes': ids,
                    'geometry': [{'lat': nodes[str(i)][0], 'lon': nodes[str(i)][1]} for i in ids]})
    return out


def main(fn, extra=()):
    d = json.load(open(fn))
    for x in extra:
        d['elements'] += load_xml(x)
    pos, adj, named = {}, {}, {}
    for e in d['elements']:
        if e['type'] != 'way':
            continue
        t = e.get('tags', {})
        name = (t.get('name') or '')
        if any(s in name.lower() for s in SKIP):
            continue
        race = t.get('highway') == 'raceway'
        ids = e['nodes']; g = e['geometry']
        for i, nid in enumerate(ids):
            pos[nid] = (g[i]['lat'], g[i]['lon'])
        for a, b in zip(ids, ids[1:]):
            w = dist(pos[a], pos[b]) * (1.0 if race else 3.0) * (8.0 if any(s in name.lower() for s in AVOID) else 1.0)
            adj.setdefault(a, []).append((b, w)); adj.setdefault(b, []).append((a, w))
        if name:
            named.setdefault(name, []).extend(ids)
    def centre(n):
        ids = named[n]
        la = sum(pos[i][0] for i in ids) / len(ids); lo = sum(pos[i][1] for i in ids) / len(ids)
        return min(ids, key=lambda i: dist(pos[i], (la, lo)))
    wps = []
    for n in ORDER:
        if n not in named:
            sys.exit('missing corner ' + n)
        wps.append(centre(n))
    def path(a, b):
        best = {a: 0}; prev = {}; pq = [(0, a)]
        while pq:
            c, u = heapq.heappop(pq)
            if u == b:
                break
            if c > best[u]:
                continue
            for v, w in adj.get(u, []):
                if c + w < best.get(v, 1e18):
                    best[v] = c + w; prev[v] = u; heapq.heappush(pq, (c + w, v))
        out = [b]
        while out[-1] != a:
            out.append(prev[out[-1]])
        return out[::-1]
    loop = []
    for i in range(len(wps)):
        try:
            p = path(wps[i], wps[(i + 1) % len(wps)])
        except KeyError:
            sys.exit("no route " + ORDER[i] + " -> " + ORDER[(i + 1) % len(ORDER)])
        loop += p[:-1]
    pts = [pos[i] for i in loop]
    # start line: on the pit straight, ~380 m before the Dunlop chicane waypoint (which is index 0)
    L = 0; k = len(pts) - 1
    while L < 380:
        L += dist(pts[k], pts[k - 1]); k -= 1
    pts = pts[k:] + pts[:k]
    total = sum(dist(pts[i], pts[(i + 1) % len(pts)]) for i in range(len(pts)))
    print(json.dumps(pts))
    sys.stderr.write(f'Le Mans loop: {len(pts)} points, {total:.0f} m\n')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2:])
