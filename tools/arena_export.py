"""Regenerate g_arena_walls in src/gameplay/arena.c from FLOODGATE_K12.blend.

    blender --background <map>.blend --python tools/arena_export.py
    blender --background <map>.blend --python tools/arena_export.py -- --write

Without --write it prints the block and the audit and touches nothing. With
--write it patches src/gameplay/arena.c in place and updates ARENA_WALL_COUNT.

arena.c is the single source of truth at runtime: rendering, locomotion,
mantle, wall-run, wall-climb, traversal and bullet raycasts all read
g_arena_walls directly. This script exists so the .blend can be the AUTHORING
tool without becoming a second source of truth.

Axis mapping (Blender Z-up -> engine Y-up):

    engine x   =  blender x
    engine z   = -blender y
    base, top  =  the box's blender-z extent

A box need not sit on the ground. base above zero is a roof, a floor slab, a
catwalk, a pipe, or the lintel over a door. That freedom is worth exactly two
checks, and both of these were real bugs before they were tests:

  CRUSH   a standable top with less than MOVE_PLAYER_HEIGHT of clear air under
          whatever is above it. The player wears it as a hat and cannot leave.
  ORPHAN  a raised box with nothing within MOVE_MANTLE_MAX_RISE of its top: a
          platform with no way onto it.

GROUP_ORDER fixes the emitted order. Without it the output follows Blender's
alphabetical collection order, which is stable but arbitrary, and every export
would produce a large meaningless diff.
"""
import os
import sys

import bpy
from mathutils import Vector

PLAYER_HEIGHT = 2.53   # MOVE_PLAYER_HEIGHT
MANTLE_MAX = 2.3       # MOVE_MANTLE_MAX_RISE
REACH = 1.2            # how near a neighbour must be to count as an approach

ROOT_COLLECTION = "FLOODGATE_K12"
SKIP = {"Reference"}

GROUP_ORDER = ["Perimeter", "PumpHouse", "Control", "Mid", "Buildings",
               "Basins", "Inlet", "Digesters", "Silos", "Channel",
               "Corridors", "Pipes", "Filters", "Outfall", "Masts", "Yard"]

BANNER = {
    "Perimeter": "Perimeter flood walls.",
    "PumpHouse": "Pump house, north-west. Two storeys: slab over the west half, windows above, stepped climb to the roof.",
    "Control": "Control block, south-east. The pump house's opposite number, rotated and mirrored.",
    "Mid": "Mid station. Sits on the long axis so no spawn sees the other.",
    "Buildings": "Sheds, and the crates that reach their roofs.",
    "Basins": "Settling basins: open to the sky, walls to ride, one scraper bridge across the top.",
    "Inlet": "Inlet screens and their channel.",
    "Digesters": "Digester tanks, east. Every rise in the chain is at or under MOVE_MANTLE_MAX_RISE.",
    "Silos": "Silo cluster, west. The same climb the digesters are, so neither half owns the vertical.",
    "Channel": "Main channel: an L-bent slide lane under a suspended sluice gate.",
    "Corridors": "Wall-jump corridors. Paired faces 6.8m apart, hung from 7 to 21, with air underneath so the ground fight carries on below.",
    "Pipes": "Pipe runs and bridges, raised, run over and under.",
    "Filters": "Filter galleries: waist-high cover.",
    "Outfall": "Outfall channel, stepped.",
    "Masts": "Grapple masts, spaced inside MOVE_GRAPPLE_RANGE of the routes they serve.",
    "Yard": "Scattered yard clutter and sightline breakers.",
}


def num(v):
    """arena.c's own style: bare ints, f-suffixed shortened floats."""
    if abs(v - round(v)) < 1e-4:
        return str(int(round(v)))
    s = ("%.4f" % v).rstrip("0")
    if s.startswith("0."):
        s = s[1:]
    elif s.startswith("-0."):
        s = "-" + s[2:]
    return s + "f"


def measure(ob):
    cs = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
    lo = Vector((min(c.x for c in cs), min(c.y for c in cs), min(c.z for c in cs)))
    hi = Vector((max(c.x for c in cs), max(c.y for c in cs), max(c.z for c in cs)))
    mid = (lo + hi) * 0.5
    return {"x": mid.x, "z": -mid.y, "w": hi.x - lo.x, "d": hi.y - lo.y,
            "base": lo.z, "top": hi.z}


def collect():
    root = bpy.data.collections.get(ROOT_COLLECTION)
    if root is None:
        raise SystemExit("no collection named %r in this .blend" % ROOT_COLLECTION)
    cols = [c for c in root.children if c.name not in SKIP]
    unknown = [c.name for c in cols if c.name not in GROUP_ORDER]
    if unknown:
        print("NOTE: collections missing from GROUP_ORDER, appended at the end: %s"
              % ", ".join(sorted(unknown)))
    cols.sort(key=lambda c: (GROUP_ORDER.index(c.name)
                             if c.name in GROUP_ORDER else len(GROUP_ORDER), c.name))
    rows = []
    for col in cols:
        obs = sorted([o for o in col.objects if o.type == 'MESH'],
                     key=lambda o: o.get("arena_index", 1 << 30))
        for ob in obs:
            mat = ob.get("arena_material")
            if mat is None:
                mat = (ob.material_slots[0].name.replace("ARENA_MATERIAL_", "")
                       if ob.material_slots else "NEUTRAL")
            rows.append((col.name, ob.name, measure(ob), mat))
    return rows


def overlaps(a, b, pad=-0.4):
    return (abs(a["x"] - b["x"]) * 2 < a["w"] + b["w"] + pad and
            abs(a["z"] - b["z"]) * 2 < a["d"] + b["d"] + pad)


def audit(rows):
    boxes = [(n, m) for _, n, m, _ in rows]
    bad = []
    for na, a in boxes:
        if a["base"] <= 0.05:
            continue
        for nb, b in boxes:
            if nb == na or b["top"] > a["base"] - 0.01 or not overlaps(a, b):
                continue
            clear = a["base"] - b["top"]
            if 0.1 < clear < PLAYER_HEIGHT:
                bad.append("CRUSH  %.2fm: standing on %s (%.1f) under %s (%.1f)"
                           % (clear, nb, b["top"], na, a["base"]))
        if a["top"] - a["base"] > 3.0:
            continue
        if not any(nb != na and
                   max(abs(a["x"] - b["x"]) - (a["w"] + b["w"]) / 2,
                       abs(a["z"] - b["z"]) - (a["d"] + b["d"]) / 2) <= REACH and
                   -0.1 <= a["top"] - b["top"] <= MANTLE_MAX
                   for nb, b in boxes):
            bad.append("ORPHAN %s (top %.1f): nothing within a mantle of it"
                       % (na, a["top"]))
    return sorted(set(bad))


def emit(rows):
    out = ["const ArenaWall g_arena_walls[ARENA_WALL_COUNT] = {"]
    last = None
    for group, _name, m, mat in rows:
        if group != last:
            if last is not None:
                out.append("")
            out.append("    /* %s */" % BANNER.get(group, group))
            last = group
        out.append("    {%s,%s,%s,%s,%s,%s,ARENA_MATERIAL_%s},"
                   % (num(m["x"]), num(m["z"]), num(m["w"]), num(m["d"]),
                      num(m["base"]), num(m["top"]), mat))
    out.append("};")
    return "\n".join(out)


def write_into(project_root, rows):
    import re
    c = os.path.join(project_root, "src", "gameplay", "arena.c")
    h = os.path.join(project_root, "include", "gameplay", "arena.h")
    text = open(c, encoding="utf-8").read()
    start = text.index("const ArenaWall g_arena_walls[ARENA_WALL_COUNT] = {")
    end = text.index("};", start) + 2
    open(c, "w", encoding="utf-8", newline="").write(
        text[:start] + emit(rows) + text[end:])
    head = open(h, encoding="utf-8").read()
    open(h, "w", encoding="utf-8", newline="").write(
        re.sub(r"#define ARENA_WALL_COUNT \d+",
               "#define ARENA_WALL_COUNT %d" % len(rows), head))
    print("wrote %d walls into %s" % (len(rows), c))


if __name__ == "__main__":
    rows = collect()
    problems = audit(rows)
    for line in problems:
        print(line)
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if "--write" in argv:
        if problems:
            raise SystemExit("refusing to write: %d problem(s) above" % len(problems))
        write_into(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), rows)
    else:
        print(emit(rows))
        print("\n/* %d walls. Re-run with -- --write to patch arena.c. */" % len(rows))
