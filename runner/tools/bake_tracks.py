#!/usr/bin/env python3
"""Bake tracks/catalog.json into the viewer JSON and the runnable C span.

The viewer frames each track from s0 to s1. The game header is only the
runnable track, straight.run, whose cycle repeats past the span.
"""

import argparse
import json
import math
import os

FULL = 4096
OBSTACLE_NONE = -128

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
DEFAULT_CATALOG = os.path.join(ROOT, "tracks", "catalog.json")
DEFAULT_JSON = os.path.join(ROOT, "tools", "trackview", "baked.json")
DEFAULT_HEADER = os.path.join(ROOT, "src", "track_data.h")


def die(msg):
    raise SystemExit("bake_tracks: " + msg)


def include_bake(json_path):
    """Copy the saved bake file into the viewer page as JSON, not a script."""
    html_path = os.path.join(os.path.dirname(json_path), "index.html")
    with open(json_path) as f:
        baked_text = f.read().strip().replace("<", "\\u003c")
    start = '<script id="baked" type="application/json">'
    end = "</script>"
    with open(html_path) as f:
        html = f.read()
    i = html.find(start)
    if i < 0:
        die("track viewer page has no baked include")
    j = html.find(end, i + len(start))
    if j < 0:
        die("track viewer page has no baked include")
    html = html[: i + len(start)] + "\n" + baked_text + "\n" + html[j:]
    with open(html_path, "w") as f:
        f.write(html)
    print("included %s in %s" % (json_path, html_path))


def to_units(deg, where):
    units = deg * FULL / 360.0
    if abs(units - round(units)) > 1e-6:
        die("%s angle %s is not an exact turn unit" % (where, deg))
    return int(round(units))


def spread(total, n):
    q, r = divmod(abs(total), n)
    sign = 1 if total >= 0 else -1
    out = [sign * q] * n
    for i in range(r):
        out[i] += sign
    return out


def forward(yaw, pitch):
    y = yaw * math.tau / FULL
    p = pitch * math.tau / FULL
    cp = math.cos(p)
    return (math.sin(y) * cp, math.sin(p), math.cos(y) * cp)


def basis(yaw, pitch, roll):
    fx, fy, fz = forward(yaw, pitch)
    y = yaw * math.tau / FULL
    rx, ry, rz = math.cos(y), 0.0, -math.sin(y)
    ux = fy * rz - fz * ry
    uy = fz * rx - fx * rz
    uz = fx * ry - fy * rx

    def norm(v):
        l = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) or 1.0
        return (v[0] / l, v[1] / l, v[2] / l)

    rx, ry, rz = norm((rx, ry, rz))
    ux, uy, uz = norm((ux, uy, uz))
    c = math.cos(roll * math.tau / FULL)
    s = math.sin(roll * math.tau / FULL)
    r2 = (rx * c - ux * s, ry * c - uy * s, rz * c - uz * s)
    u2 = (rx * s + ux * c, ry * s + uy * c, rz * s + uz * c)
    return norm(r2), norm(u2), norm((fx, fy, fz))


def bank_lift(frame, edge):
    """Rise that keeps the low edge of a banked deck at grade.

    Bank pivots on the inside edge: positive roll drops the right edge, so
    the deck lifts by that edge's offset times sin(roll), along the unrolled
    up vector. Level road lifts nothing."""
    if frame.roll % FULL == 0:
        return (0.0, 0.0, 0.0)
    sin_r = math.sin(frame.roll * math.tau / FULL)
    rise = (edge[1] if sin_r > 0 else edge[0]) * sin_r
    _, up, _ = basis(frame.yaw, frame.pitch, 0)
    return (up[0] * rise, up[1] * rise, up[2] * rise)


class Frame(object):
    def __init__(self):
        self.x = self.y = self.z = 0.0
        self.yaw = self.pitch = self.roll = 0
        self.s = 0

    def copy(self):
        f = Frame()
        f.x, f.y, f.z = self.x, self.y, self.z
        f.yaw, f.pitch, f.roll = self.yaw, self.pitch, self.roll
        f.s = self.s
        return f


def step_frame(frame, dyaw, dpitch, droll, step):
    """Advance one step. Position uses the midpoint heading."""
    my = frame.yaw + dyaw / 2.0
    mp = frame.pitch + dpitch / 2.0
    fx, fy, fz = forward(my, mp)
    end = frame.copy()
    end.x += fx * step
    end.y += fy * step
    end.z += fz * step
    end.yaw = frame.yaw + dyaw
    end.pitch = frame.pitch + dpitch
    end.roll = frame.roll + droll
    end.s = frame.s + step
    return end


def ivec(v):
    return [int(round(v[0])), int(round(v[1])), int(round(v[2]))]


def lane_offsets(n):
    return [i - (n - 1) / 2.0 for i in range(n)]


def nearest_lane(options, value):
    return min(options, key=lambda item: (abs(item - value), abs(item)))


def lane_pairs(n0, n1):
    """Lines across a lane-count change. A new lane starts on the lane it
    leaves, so one lane splitting into two begins as a single line. A merge
    of two lanes brings both lines onto the one that remains."""
    start, end = lane_offsets(n0), lane_offsets(n1)
    if n0 == n1:
        return list(zip(start, end))
    if n1 > n0:
        return [(nearest_lane(start, dest), dest) for dest in end]
    if n1 == 1:
        return [(src, end[0]) for src in start]
    step = (n0 - 1) / float(n1 - 1)
    idxs = [int(round(i * step)) for i in range(n1)]
    return [(start[i], end[k]) for k, i in enumerate(idxs)]


def road_width(n, lane, full):
    # Three lanes use the catalog width. Fewer lanes keep the same side margin.
    return (n - 1) * lane + (full - 2 * lane)


def divider_offsets(n):
    """The lines between lanes, in lane units. Three lanes have two lines."""
    if n < 2:
        return []
    return [k - (n - 2) / 2.0 for k in range(n - 1)]


def divider_pairs(n0, n1):
    start, end = divider_offsets(n0), divider_offsets(n1)
    if n0 == n1:
        return list(zip(start, end))
    if not start:
        return [(dest, dest) for dest in end]
    if not end:
        return [(src, src) for src in start]
    if len(end) >= len(start):
        return [(nearest_lane(start, dest), dest) for dest in end]
    return [(src, nearest_lane(end, src)) for src in start]


def gore_edges(shift, n, parent_lanes, half, lane):
    """Pavement edges, in branch-local units, that meet the road being left.
    The outer edge stays on the parent edge. The inner edge meets the
    divider beside the lanes this branch takes."""
    centers = [shift + c for c in lane_offsets(n)]
    left, right = min(centers), max(centers)
    parent = lane_offsets(parent_lanes)
    half_l = half / float(lane)

    def boundary(side, at):
        neighbor = at + side
        if any(abs(p - neighbor) < 1e-6 for p in parent):
            return (at + neighbor) / 2.0
        return -half_l if side < 0 else half_l

    return (
        (boundary(-1, left) - shift) * lane,
        (boundary(1, right) - shift) * lane,
    )


def detect_caps(pieces, repeat_from):
    caps = []
    if repeat_from:
        caps.append("repeat")
    if any(p.get("yaw", 0) for p in pieces):
        caps.append("yaw")
    if any(p.get("grade", 0) for p in pieces):
        caps.append("grade")
    if any(abs(p.get("grade", 0)) >= 360 for p in pieces):
        die("a full pitch turn is a loop, and loops are not supported")
    if any(p.get("bank", 0) for p in pieces):
        caps.append("bank")
    if any(p.get("lanes", 3) != 3 or p.get("lanesTo", p.get("lanes", 3)) != 3 for p in pieces):
        caps.append("lanes")
    if any(p.get("fork") for p in pieces):
        caps.append("fork")
    arrivals = {}
    for p in pieces:
        for key in ("next", "fork"):
            target = p.get(key)
            if target:
                arrivals.setdefault(target, []).append(p["id"])
    if any(len(srcs) >= 2 and target != repeat_from for target, srcs in arrivals.items()):
        caps.append("join")
    if any(p.get("deck", True) is False for p in pieces):
        caps.append("gap")
    return caps


def bake_track(track, cat):
    step = cat["step"]
    lane_w = cat["lane"]
    width = cat["width"]
    max_step = cat["maxStep"]
    pieces = track["pieces"]
    by_id = {}
    for p in pieces:
        if p["id"] in by_id:
            die("%s repeats piece %s" % (track["id"], p["id"]))
        by_id[p["id"]] = p
        if p["length"] % step != 0 or p["length"] <= 0:
            die("%s piece %s length is not a multiple of %s" % (track["id"], p["id"], step))
    if track["start"] not in by_id:
        die("%s missing start %s" % (track["id"], track["start"]))
    repeat_from = track.get("repeatFrom")
    if not repeat_from:
        die("%s needs a repeatFrom" % track["id"])
    if repeat_from not in by_id:
        die("%s missing repeatFrom %s" % (track["id"], repeat_from))

    found = detect_caps(pieces, repeat_from)
    listed = list(track["capabilities"])
    if set(found) != set(listed):
        die("%s capabilities are %s, catalog says %s" % (track["id"], found, listed))
    if track["status"] not in ("runnable", "view"):
        die("%s status" % track["id"])
    if track["status"] == "runnable":
        extra = [c for c in listed if c not in cat["implemented"]]
        if extra:
            die("%s is runnable but needs %s" % (track["id"], ", ".join(extra)))

    segs = []
    baked = {}
    piece_meta = []

    def bake_piece(pid, frame, chain):
        p = by_id[pid]
        n = p["length"] // step
        dys = spread(to_units(p.get("yaw", 0), pid), n)
        dps = spread(to_units(p.get("grade", 0), pid), n)
        drs = spread(to_units(p.get("bank", 0), pid), n)
        lanes = p.get("lanes", 3)
        lanes_to = p.get("lanesTo", lanes)
        if lanes not in (1, 2, 3) or lanes_to not in (1, 2, 3):
            die("%s piece %s lane count must be 1, 2, or 3" % (track["id"], pid))
        if "lanes" in p or "lanesTo" in p:
            w0 = road_width(lanes, lane_w, width)
            w1 = road_width(lanes_to, lane_w, width)
        else:
            w0 = w1 = p.get("width", width)
        pairs = lane_pairs(lanes, lanes_to)
        marks = divider_pairs(lanes, lanes_to)
        shift = p.get("shift", 0)
        if shift and lanes < 3 and lanes == lanes_to and "lanes" in p:
            edge0 = gore_edges(shift, lanes, 3, width / 2.0, lane_w)
        else:
            edge0 = (-w0 / 2.0, w0 / 2.0)
        edge1 = (-w1 / 2.0, w1 / 2.0)
        obstacles = {}
        for o in p.get("obstacles", []):
            if o["at"] % step != step // 2:
                die("%s %s obstacle at %s is not a segment center" % (track["id"], pid, o["at"]))
            li = (o["at"] - step // 2) // step
            if li < 0 or li >= n:
                die("%s %s obstacle past the piece" % (track["id"], pid))
            if lanes != lanes_to:
                die("%s %s obstacle sits on a lane change" % (track["id"], pid))
            if not any(abs(o["lane"] - off) < 1e-6 for off in lane_offsets(lanes)):
                die("%s %s obstacle lane %s" % (track["id"], pid, o["lane"]))
            obstacles[li] = o["lane"]
        deck = p.get("deck", True)
        if shift:
            right, up, fwd = basis(frame.yaw, frame.pitch, frame.roll)
            frame = frame.copy()
            moved = shift * lane_w
            frame.x += right[0] * moved
            frame.y += right[1] * moved
            frame.z += right[2] * moved
        first = len(segs)
        cur = frame.copy()
        for i in range(n):
            if max(abs(dys[i]), abs(dps[i]), abs(drs[i])) > max_step:
                if track["status"] == "runnable":
                    die("%s %s segment %s turns faster than %s" % (track["id"], pid, i, max_step))
            end = step_frame(cur, dys[i], dps[i], drs[i], step)
            right, up, fwd = basis(cur.yaw, cur.pitch, cur.roll)
            r1, u1, f1 = basis(end.yaw, end.pitch, end.roll)
            t0 = i / float(n)
            t1 = (i + 1) / float(n)
            def at_u(t):
                return [round((src + (dest - src) * t) * lane_w, 3) for src, dest in pairs]
            def at_mark(t):
                return [round((src + (dest - src) * t) * lane_w, 3) for src, dest in marks]
            def at_edge(t):
                return [round(edge0[k] + (edge1[k] - edge0[k]) * t, 3) for k in range(2)]
            e0, e1 = at_edge(t0), at_edge(t1)
            lift0 = bank_lift(cur, e0)
            lift1 = bank_lift(end, e1)
            segs.append({
                "s": cur.s,
                "piece": pid,
                "local": i,
                "chain": chain,
                "p": [cur.x + lift0[0], cur.y + lift0[1], cur.z + lift0[2]],
                "p1": [end.x + lift1[0], end.y + lift1[1], end.z + lift1[2]],
                "lift": lift0,
                "yaw": cur.yaw % FULL,
                "pitch": cur.pitch % FULL,
                "roll": cur.roll % FULL,
                "yaw1": end.yaw % FULL,
                "pitch1": end.pitch % FULL,
                "roll1": end.roll % FULL,
                "lanes": lanes,
                "lanes1": lanes_to,
                "width": round(e0[1] - e0[0], 3),
                "width1": round(e1[1] - e1[0], 3),
                "edge": e0,
                "edge1": e1,
                "laneU": at_u(t0),
                "laneU1": at_u(t1),
                "markU": at_mark(t0),
                "markU1": at_mark(t1),
                "deck": bool(deck),
                "next": -1,
                "fork": -1,
                "obstacle": obstacles.get(i),
                "right": right,
                "up": up,
                "fwd": fwd,
                "right1": r1,
                "up1": u1,
                "over": max(abs(dys[i]), abs(dps[i]), abs(drs[i])) > max_step,
            })
            cur = end
        for i in range(n - 1):
            segs[first + i]["next"] = first + i + 1
        baked[pid] = first
        piece_meta.append({
            "id": pid,
            "s0": frame.s,
            "s1": frame.s + p["length"],
            "chain": chain,
            "first": first,
            "count": n,
        })
        return first, cur

    def beside(frame, seg):
        """True when frame sits on the target piece's first cross-section."""
        dx = frame.x + seg["lift"][0] - seg["p"][0]
        dy = frame.y + seg["lift"][1] - seg["p"][1]
        dz = frame.z + seg["lift"][2] - seg["p"][2]
        lateral = dx * seg["right"][0] + dy * seg["right"][1] + dz * seg["right"][2]
        along = dx * seg["fwd"][0] + dy * seg["fwd"][1] + dz * seg["fwd"][2]
        vertical = dx * seg["up"][0] + dy * seg["up"][1] + dz * seg["up"][2]

        def adiff(a, b):
            d = abs(a - b) % FULL
            return min(d, FULL - d)

        aligned = (adiff(frame.yaw, seg["yaw"]) < 8 and
                   adiff(frame.pitch, seg["pitch"]) < 8 and
                   adiff(frame.roll, seg["roll"]) < 8)
        return (aligned and abs(along) < step / 8.0 and abs(vertical) < step / 8.0
                and abs(lateral) <= seg["width"] / 2.0 + 1)

    def bake_chain(pid, frame, chain, inbound):
        while pid:
            if pid in baked:
                if pid == repeat_from or beside(frame, segs[baked[pid]]):
                    if inbound is not None:
                        segs[inbound]["next"] = baked[pid]
                    return
                die("%s piece %s is reached twice" % (track["id"], pid))
            first, end = bake_piece(pid, frame, chain)
            if inbound is not None:
                segs[inbound]["next"] = first
            p = by_id[pid]
            last = first + (p["length"] // step) - 1
            nxt = p.get("next")
            fork_id = p.get("fork")
            # The continuing chain is baked first, so a later fork can meet it.
            if nxt:
                bake_chain(nxt, end, chain, last)
            if fork_id:
                bake_chain(fork_id, end.copy(), "fork", None)
                segs[last]["fork"] = baked[fork_id]
            return

    bake_chain(track["start"], Frame(), "main", None)

    main = [s for s in segs if s["chain"] == "main"]
    if not main:
        die("%s has no span" % track["id"])
    s0 = main[0]["s"]
    s1 = main[-1]["s"] + step
    repeat_s = None
    if repeat_from:
        repeat_s = segs[baked[repeat_from]]["s"]

    # Every track links back to its seam. The pose there decides the strategy:
    # closes (same place), continues (same heading, further along), or turns
    # (the next period follows the heading the period finished on).
    seam = segs[baked[repeat_from]]
    end = main[-1]
    if end["next"] != baked[repeat_from]:
        die("%s does not link back to %s" % (track["id"], repeat_from))
    if end["lanes1"] != seam["lanes"]:
        die("%s repeat changes the lane count" % track["id"])

    def adiff(a, b):
        d = abs(a - b) % FULL
        return min(d, FULL - d)

    dx = end["p1"][0] - seam["p"][0]
    dy = end["p1"][1] - seam["p"][1]
    dz = end["p1"][2] - seam["p"][2]
    closure = math.sqrt(dx * dx + dy * dy + dz * dz)
    same_heading = (adiff(end["yaw1"], seam["yaw"]) < 8 and
                    adiff(end["pitch1"], seam["pitch"]) < 8 and
                    adiff(end["roll1"], seam["roll"]) < 8)
    same_bank = (adiff(end["pitch1"], seam["pitch"]) < 8 and
                 adiff(end["roll1"], seam["roll"]) < 8)
    if same_heading and closure < (step / 8.0):
        strategy = "closes"
    elif same_heading:
        strategy = "continues"
    elif same_bank:
        strategy = "turns"
    else:
        die("%s repeat changes pitch or bank" % track["id"])
    closes = strategy == "closes"

    warnings = []
    for s in segs:
        if s["over"]:
            warnings.append("%s segment %s turns faster than %s" % (s["piece"], s["local"], max_step))
    for m in piece_meta:
        p = by_id[m["id"]]
        if abs(p.get("grade", 0)) < 360 and abs(p.get("yaw", 0)) < 360:
            continue
        a = segs[m["first"]]
        b = segs[m["first"] + m["count"] - 1]
        dx = b["p1"][0] - a["p"][0]
        dy = b["p1"][1] - a["p"][1]
        dz = b["p1"][2] - a["p"][2]
        miss = math.sqrt(dx * dx + dy * dy + dz * dz)
        if miss >= step / 8.0:
            warnings.append("%s misses its own start by %.1f" % (m["id"], miss))
    if strategy == "continues" and closure < step * 4:
        warnings.append("repeat misses a close by %.1f" % closure)

    # One ghost step: the seam's first segment, placed in the frame where
    # the next period starts. A turn uses that new heading.
    ghost = None
    if strategy != "closes":
        src = seam
        end_right, end_up = end["right1"], end["up1"]
        end_fwd = (
            end_right[1] * end_up[2] - end_right[2] * end_up[1],
            end_right[2] * end_up[0] - end_right[0] * end_up[2],
            end_right[0] * end_up[1] - end_right[1] * end_up[0],
        )
        src_fwd = src["fwd"]

        def project(v, right, up, fwd):
            return (
                v[0] * right[0] + v[1] * right[1] + v[2] * right[2],
                v[0] * up[0] + v[1] * up[1] + v[2] * up[2],
                v[0] * fwd[0] + v[1] * fwd[1] + v[2] * fwd[2],
            )

        def place(local):
            return [
                end["p1"][i]
                + local[0] * end_right[i]
                + local[1] * end_up[i]
                + local[2] * end_fwd[i]
                for i in range(3)
            ]

        step_delta = [src["p1"][i] - src["p"][i] for i in range(3)]
        ghost = {
            "p": list(end["p1"]),
            "p1": place(project(step_delta, src["right"], src["up"], src_fwd)),
            "right": place(project(src["right"], src["right"], src["up"], src_fwd)),
            "right1": place(project(src["right1"], src["right"], src["up"], src_fwd)),
            "up": place(project(src["up"], src["right"], src["up"], src_fwd)),
            "width": src["width"],
            "width1": src.get("width1", src["width"]),
            "piece": src["piece"],
        }
        # place() added the end position; direction vectors are offsets.
        for key in ("right", "right1", "up"):
            ghost[key] = [ghost[key][i] - end["p1"][i] for i in range(3)]

    def pack(s):
        return {
            "s": s["s"],
            "piece": s["piece"],
            "local": s["local"],
            "chain": s["chain"],
            "p": [round(v, 3) for v in s["p"]],
            "p1": [round(v, 3) for v in s["p1"]],
            "yaw": s["yaw"],
            "pitch": s["pitch"],
            "roll": s["roll"],
            "lanes": s["lanes"],
            "width": s["width"],
            "width1": s.get("width1", s["width"]),
            "edge": s.get("edge"),
            "edge1": s.get("edge1"),
            "laneU": s.get("laneU", []),
            "laneU1": s.get("laneU1", []),
            "markU": s.get("markU", []),
            "markU1": s.get("markU1", []),
            "deck": s["deck"],
            "next": s["next"],
            "fork": s["fork"],
            "obstacle": s["obstacle"],
            "right": [round(v, 5) for v in s["right"]],
            "up": [round(v, 5) for v in s["up"]],
            "fwd": [round(v, 5) for v in s["fwd"]],
            "right1": [round(v, 5) for v in s["right1"]],
        }

    span = {
        "s0": s0,
        "s1": s1,
        "repeatFrom": repeat_s,
        "repeats": True,
        "repeat": strategy,
        "closes": bool(closes),
        "closure": None if closure is None else round(closure, 3),
    }
    return {
        "id": track["id"],
        "name": track["name"],
        "status": track["status"],
        "summary": track["summary"],
        "capabilities": track["capabilities"],
        "warnings": warnings,
        "span": span,
        "pieces": [{k: m[k] for k in ("id", "s0", "s1", "chain")}
                   for m in sorted(piece_meta, key=lambda m: list(by_id).index(m["id"]))],
        "segments": [pack(s) for s in segs],
        "ghost": ghost,
        "lane": lane_w,
    }, segs


def old_obstacle(band):
    if band >= 8 and (band % 8) == 5:
        return ((band // 8) % 3) - 1
    return None


def check_straight(baked):
    """The runnable span must reproduce has_obstacle for every band."""
    track = next(t for t in baked["tracks"] if t["id"] == "straight.run")
    step = baked["step"]
    lead = track["span"]["repeatFrom"] // step
    cycle = (track["span"]["s1"] - track["span"]["repeatFrom"]) // step
    segs = track["segments"]
    for band in range(0, 8000):
        if band < lead:
            seg = segs[band]
        else:
            seg = segs[lead + (band - lead) % cycle]
        got = seg["obstacle"]
        want = old_obstacle(band)
        if got != want:
            die("straight.run band %s obstacle %s, formula %s" % (band, got, want))
    for i, s in enumerate(segs):
        expect = lead if i == len(segs) - 1 else i + 1
        if s["next"] != expect:
            die("straight.run segment %s next is %s, expected %s" % (i, s["next"], expect))
    print("straight.run obstacles match the band formula for 8000 bands")


def write_header(path, baked_tracks, step):
    runnable = [t for t in baked_tracks if t["status"] == "runnable"]
    runnable.sort(key=lambda t: 0 if t["id"] == "straight.run" else 1)
    if not runnable or runnable[0]["id"] != "straight.run":
        die("straight.run must be runnable")
    lines = [
        "/* Generated by tools/bake_tracks.py from tracks/catalog.json.",
        " * Each span is one period. Past the lead, path_at repeats the cycle.",
        " * rep_x, rep_y, rep_z shift the next period. A lap that closes",
        " * shifts by zero. */",
        "",
        "typedef struct {",
        "    const char *id;",
        "    const PathSeg *seg;",
        "    int lead;",
        "    int cycle;",
        "    int count;",
        "    int rep_x, rep_y, rep_z;",
        "} TrackDef;",
        "",
    ]
    rows = []
    total = 0
    for t in runnable:
        main = [s for s in t["segments"] if s["chain"] == "main"]
        lead = t["span"]["repeatFrom"] // step
        cycle = (t["span"]["s1"] - t["span"]["repeatFrom"]) // step
        if lead + cycle != len(main):
            die("%s span is not a single chain" % t["id"])
        if t["span"]["repeat"] == "closes":
            rep = (0, 0, 0)
        else:
            seam = main[lead]
            end = main[-1]
            rep = (
                int(round(end["p1"][0] - seam["p"][0])),
                int(round(end["p1"][1] - seam["p"][1])),
                int(round(end["p1"][2] - seam["p"][2])),
            )
        name = "span_" + t["id"].replace(".", "_")
        lines.append("static const PathSeg %s[] = {" % name)
        for s in main:
            obstacle = OBSTACLE_NONE if s["obstacle"] is None else s["obstacle"]
            lines.append(
                "    {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d},%s"
                % (
                    int(round(s["p"][0])), int(round(s["p"][1])), int(round(s["p"][2])),
                    int(round(s["p1"][0])), int(round(s["p1"][1])), int(round(s["p1"][2])),
                    s["yaw"], s["pitch"], s["roll"],
                    int(s["lanes"]), int(round(s["width"])), s["next"], s["fork"],
                    1 if s["deck"] else 0, obstacle,
                    " /* %s */" % s["piece"],
                )
            )
        lines.append("};")
        lines.append("")
        rows.append((t["id"], name, lead, cycle, lead + cycle, rep))
        total += lead + cycle
    lines.append("static const TrackDef track_table[] = {")
    for tid, name, lead, cycle, count, rep in rows:
        lines.append('    {"%s", %s, %d, %d, %d, %d, %d, %d},' % (
            tid, name, lead, cycle, count, rep[0], rep[1], rep[2]))
    lines.append("};")
    lines.append("")
    lines.append("#define TRACK_TABLE_COUNT %d" % len(rows))
    lines.append("")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("\n".join(lines))
    print("wrote %s (%d segments)" % (path, total))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--catalog", default=DEFAULT_CATALOG)
    ap.add_argument("--json", default=DEFAULT_JSON)
    ap.add_argument("--header", default=DEFAULT_HEADER)
    args = ap.parse_args()
    with open(args.catalog) as f:
        cat = json.load(f)
    tracks = []
    straight = None
    for t in cat["tracks"]:
        baked, _ = bake_track(t, cat)
        tracks.append(baked)
        if t["id"] == "straight.run":
            straight = baked
        flag = baked["span"]["repeat"]
        print("%-16s %-8s span %s-%s %s warnings %d" % (
            t["id"], t["status"], baked["span"]["s0"], baked["span"]["s1"],
            flag, len(baked["warnings"])))
    if straight is None:
        die("catalog has no straight.run")
    out = {
        "version": cat["version"],
        "step": cat["step"],
        "lane": cat["lane"],
        "fullTurn": cat["fullTurn"],
        "maxStep": cat["maxStep"],
        "implemented": cat["implemented"],
        "tracks": tracks,
    }
    check_straight(out)
    os.makedirs(os.path.dirname(args.json), exist_ok=True)
    with open(args.json, "w") as f:
        json.dump(out, f, indent=2)
        f.write("\n")
    print("wrote %s" % args.json)
    include_bake(args.json)
    write_header(args.header, tracks, cat["step"])


if __name__ == "__main__":
    main()
