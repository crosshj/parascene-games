"""Extrude the boundary of a fork's road corridors into a closed wall island.

Input points come from the runtime path sampler at the authored wall offsets.
The offset contours cross while the roads overlap. Their actual intersections
are the island's split/join tips; no wall belongs on the crossing portions.
"""

def lerp(a, b, t):
    return [a[i] + (b[i] - a[i]) * t for i in range(len(a))]


def cross(a, b):
    return a[0] * b[1] - a[1] * b[0]


def intersection(a, b, c, d):
    u = [b[0] - a[0], b[2] - a[2]]
    v = [d[0] - c[0], d[2] - c[2]]
    w = [c[0] - a[0], c[2] - a[2]]
    det = cross(u, v)
    if abs(det) < 1e-9:
        return None
    t, q = cross(w, v) / det, cross(w, u) / det
    if -1e-8 <= t <= 1 + 1e-8 and -1e-8 <= q <= 1 + 1e-8:
        return max(0, min(1, t)), max(0, min(1, q))
    return None


def rounded(p):
    return [round(v) for v in p]


def record(a, b, flags):
    # start/end, main base endpoints, alt base endpoints, then their tops.
    sa, ma, aa = a
    sb, mb, ab = b
    points = [ma[0], mb[0], aa[0], ab[0], ma[1], mb[1], aa[1], ab[1]]
    return [round(sa), round(sb), [rounded(p) for p in points], flags]


def compile_forks(sections):
    result = []
    for branch, section in enumerate(sections):
        main, alt = section['main'], section['alt']
        mid = len(main) // 2
        mp, ap = main[mid][1], alt[mid][1]
        # Determine the facing edges from physical positions, including
        # mirrored forks and unequal lane counts.
        right = [mp[2][j] - mp[0][j] for j in range(3)]
        delta = [(ap[0][j] + ap[2][j] - mp[0][j] - mp[2][j]) for j in range(3)]
        mi = 2 if sum(a * b for a, b in zip(right, delta)) > 0 else 0
        ai = 2 - mi
        outer = [[section['start'] - 256,
                  section['entry'][2-mi:4-mi], section['entry'][2-ai:4-ai]]]
        for (s, m), (_, a) in zip(main[:-1], alt[:-1]):
            outer.append([s, m[2-mi:4-mi], a[2-ai:4-ai]])
        outer.append([section['end'], section['exit'][2-mi:4-mi], section['exit'][2-ai:4-ai]])
        result.extend(record(a, b, branch * 4) for a, b in zip(outer, outer[1:]))

        for kind, main, alt in ((1, main, alt),):
            hits = []
            for i in range(len(main) - 1):
                for j in range(len(alt) - 1):
                    hit = intersection(main[i][1][mi], main[i+1][1][mi],
                                       alt[j][1][ai], alt[j+1][1][ai])
                    if hit is None:
                        continue
                    t, q = hit
                    ms = main[i][0] + t * (main[i+1][0] - main[i][0])
                    ass = alt[j][0] + q * (alt[j+1][0] - alt[j][0])
                    if any(abs(ms-h[0]) < 1 and abs(ass-h[1]) < 1 for h in hits):
                        continue
                    base = lerp(main[i][1][mi], main[i+1][1][mi], t)
                    mt = lerp(main[i][1][mi+1], main[i+1][1][mi+1], t)
                    at = lerp(alt[j][1][ai+1], alt[j+1][1][ai+1], q)
                    top = [base[0], (mt[1]+at[1])/2, base[2]]
                    hits.append((ms, ass, [base, top]))
            hits.sort()
            if len(hits) != 2 or hits[0][1] >= hits[1][1]:
                raise ValueError(f'fork {branch}: expected one closed wall island, found {len(hits)} boundary intersections')
            first, last = hits
            inner = [[min(first[:2]), first[2], first[2]]]
            for (s, m), (_, a) in zip(main, alt):
                if max(first[:2]) < s < min(last[:2]):
                    inner.append([s, m[mi:mi+2], a[ai:ai+2]])
            inner.append([max(last[:2]), last[2], last[2]])
            if len(inner) < 3:
                raise ValueError(f'fork {branch}: wall island has no interior')
            result.extend(record(a, b, branch * 4 + kind) for a, b in zip(inner, inner[1:]))
    return result
