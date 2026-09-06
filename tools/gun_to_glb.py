#!/usr/bin/env python3
"""Converts a gun's FBX into the binary glTF the engine loads, for any gun
whose import is only the ordinary one.

tools/cz_to_glb.py was the fourth per-asset script and this is the answer to
there being an eighth. What every import does - node transforms, axes and
scale, the texture folding, the glb write - was already shared in
tools/asset_import.py; what stayed per-script was the handful of decisions each
asset makes. Four guns arriving at once made it plain that those decisions are
not per-asset at all. They are per-asset VALUES of the same three questions:

  * which parts of the file are this gun,
  * where along its length the origin goes,
  * and which surface an optic clamps to.

So they are answered in each gun's import.json, in prose, beside the mesh they
describe - and this script is what reads the answers. The CZ keeps its own
script because it also splits an optic out of the same file, which is a real
fourth question and one only it asks.

The one thing here that the CZ's script does not do is pick apart a mesh that
has a single material. Three of the four guns this was written for arrived as
one node or one atlas, and a magazine that cannot be named cannot be dropped
during a reload - so a manifest material may claim its parts by NODE as well as
by source material, and two of them may share one image rather than baking a
second copy of a 1024px atlas into the file.

Usage:  python3 tools/gun_to_glb.py models/weapons/<gun>
        python3 tools/gun_to_glb.py            # every gun that names this file
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import asset_import as ai

GENERATOR = 'tools/gun_to_glb.py'
WEAPONS = 'models/weapons'


# --------------------------------------------------------------------------
# Which parts are this gun

def claim(parts, materials, drop=()):
    """[(output material name, node name, group)] for the parts this manifest
    claims, and the names of the ones it does not.

    An output material claims parts by `nodes`, by `source` - the source file's
    own material names - or, stating neither, by having the same name as a
    source material, which is the plain case and the CZ's. A part claimed by
    nothing is not this gun: it is the artist's preview round in mid-air, or a
    cosmetic body kit nothing can equip, and leaving it in would put the scale
    and the muzzle on a bounding box that includes it.

    `drop` is for the part that WOULD be claimed and should not be. Leaving a
    material out of the manifest is enough to drop it on a gun whose parts are
    split by material; it does nothing on a gun that is one material end to
    end, where every node answers to the body's `source` and the sights come
    back in the moment the group naming them is deleted. That is the MP7, and
    this is how its irons come off. Matched against a node name or a source
    material name, whichever the manifest finds it easier to say.

    First claim wins, so a magazine named by node comes out of the atlas the
    receiver also draws from rather than being taken twice."""
    drop = set(drop)
    taken = {index for index, (node, material, _) in enumerate(parts)
             if node in drop or material in drop}
    unmatched = drop - {n for n, _, _ in parts} - {m for _, m, _ in parts}
    if unmatched:
        raise ValueError('drop names %s, which this mesh does not have'
                         % ', '.join(sorted(repr(u) for u in unmatched)))
    claimed = []
    for output, spec in materials.items():
        nodes = set(spec.get('nodes', ()))
        sources = set(spec.get('source', ()))
        if not nodes and not sources:
            sources = {output}
        for index, (node, material, group) in enumerate(parts):
            if index in taken:
                continue
            if node in nodes or material in sources:
                taken.add(index)
                claimed.append((output, node, group))
    dropped = sorted({node for i, (node, _, _) in enumerate(parts)
                      if i not in taken or node in drop})
    return claimed, dropped


def carve(claimed, boxes, axis, up):
    """The claimed parts with the triangles inside `boxes` taken out.

    For the gun whose irons are welded into the same material as its slide.
    `drop` takes a part off by name, and needs a seam to do it; the three
    pistols have none - one body material, one node, sights and all - so the
    only thing left to name them by is WHERE THEY ARE. A box is a stretch along
    the gun and a height above which nothing in that stretch belongs to the
    gun, measured once off the mesh and written into the manifest with its
    working.

    Coarse on purpose, and the coarseness is the honest part. This is a region
    and not a shell, so it cannot follow the contour a sight is welded along;
    the heights are picked to cut just ABOVE the flat each sight stands on
    rather than into it. What is left is a slide with no sight standing on it
    and, at the seam, a fraction of a millimetre of base that reads as
    machining. Cutting lower would open a hole in the slide, which is worse
    than a sight.

    A triangle goes by its centroid, so the cut lands between triangles rather
    than splitting them, and the edge it leaves is the mesh's own."""
    if not boxes:
        return claimed
    out, removed = [], 0
    for output, node, group in claimed:
        points = ai.vertices(group)
        kept = {'positions': [], 'normals': [], 'uvs': [], 'indices': []}
        moved = {}
        for i in range(0, len(group['indices']), 3):
            triangle = group['indices'][i:i + 3]
            along = sum(points[v][axis] for v in triangle) / 3.0
            height = sum(points[v][up] for v in triangle) / 3.0
            if any(box['along'][0] <= along <= box['along'][1]
                   and height >= box['above'] for box in boxes):
                removed += 1
                continue
            for v in triangle:
                if v not in moved:
                    moved[v] = len(kept['positions']) // 3
                    kept['positions'] += group['positions'][v * 3:v * 3 + 3]
                    kept['normals'] += group['normals'][v * 3:v * 3 + 3]
                    kept['uvs'] += group['uvs'][v * 2:v * 2 + 2]
                kept['indices'].append(moved[v])
        out.append((output, node, kept))
    if not removed:
        raise ValueError('the cut boxes in this manifest match no triangle; '
                         'they are stated in file units and the mesh has '
                         'moved under them')
    print('  cut %d triangles from %d box(es)' % (removed, len(boxes)))
    return out


def part_of(claimed, spec, key):
    """The groups a manifest landmark names, by node or by output material.

    Landmarks are named rather than found - see each manifest for why the gun
    it describes cannot have its receiver or its rail inferred - so a name that
    matches nothing is an error and not an empty answer. Silently measuring
    from nothing would put the origin or the rail at zero, which is a gun that
    imports cleanly and is wrong by ten centimetres."""
    want = spec[key]
    if isinstance(want, str):
        want = {'node': want}
    groups = [g for output, node, g in claimed
              if (want.get('node') == node or want.get('material') == output)]
    if not groups:
        raise ValueError('%s names %r, which is nothing in this gun'
                         % (key, want))
    return groups


# --------------------------------------------------------------------------
# The frame

def muzzle_at_high(axes, axis):
    """Whether the muzzle is at the high end of the length axis.

    Read off the axes rather than stated again. The third entry of `axes` says
    which file axis becomes the viewmodel's z, and forward is -z, so a file
    axis that is negated points its high end forward and one that is not points
    its low end forward. Stating it twice would let the two disagree, and a gun
    whose muzzle is at the wrong end imports without complaint and fires out of
    its buttplate."""
    sign, letter = axes[2][0], axes[2][1]
    if ai.Frame.AXES[letter] != axis:
        raise ValueError('length_axis is %s but the viewmodel z comes from %s'
                         % ('xyz'[axis], letter))
    return sign == '-'


def bore_line(claimed, manifest, axis, low, high):
    """(across axis -> coordinate) of the barrel's centre line, in file units.

    Where the muzzle flash and the tracer hang off, and the height every offset
    in the spec is measured from, so it is worth more than the mesh's middle.

    By default it is the centre of the last twentieth of the gun's length,
    which on a gun whose muzzle end is barrel and nothing else is the bore. A
    manifest can name a `bore` part instead, for the gun where that is not
    true: a revolver's muzzle end is barrel, rib and ejector shroud stacked in
    a column, and its centre is a centimetre under the hole."""
    across = [c for c in range(3) if c != axis]
    if 'bore' in manifest.data:
        groups = part_of(claimed, manifest.data, 'bore')
        blow, bhigh = ai.bounds(groups)
        return {c: (blow[c] + bhigh[c]) * 0.5 for c in across}
    length = high[axis] - low[axis]
    if muzzle_at_high(manifest['axes'], axis):
        keep = lambda p: p[axis] > high[axis] - length * 0.05
    else:
        keep = lambda p: p[axis] < low[axis] + length * 0.05
    barrel = [p for _, _, g in claimed for p in ai.vertices(g) if keep(p)]
    return {c: (min(p[c] for p in barrel) + max(p[c] for p in barrel)) * 0.5
            for c in across}


# A rail is long. That is the whole of the rule below, and it is worth saying
# plainly because the obvious alternatives are both wrong on real meshes.
#
# "The highest point on the gun" is wrong: on the PMX that is a flip-up front
# sight standing 33mm proud of the rail, and mounting an optic there would put
# it three centimetres into the air. "The largest horizontal flat" is wrong
# too, and more subtly: a Picatinny rail is a row of narrow lands separated by
# wide slots, so the biggest flat on it is the FLOOR of a slot, five
# millimetres under the surface an optic actually clamps to.
#
# What separates the rail from everything above it is not height and not area
# but reach. The PMX's rail runs over half the gun; the two sight bases on top
# of it reach nine per cent between them. The MP7's runs 57% against 29% for
# the next flat down. A quarter sits in the middle of a gap that wide on both
# guns, with nothing else near it, which is why it is one constant here rather
# than a number each manifest has to state and get right.
RAIL_REACH = 0.25


def rail_top(claimed, axis, up, length):
    """Y of the flat an optic clamps to, in file units, with the ladder of
    flats above it printed.

    The mirror of asset_import.rail_seat, which finds the sight's half of the
    same joint on the optic's own mesh: that one takes the largest DOWNWARD
    facing flat in the bottom half, this one the highest UPWARD facing flat
    that reaches far enough to be a rail. The two numbers are added by
    weapon_spec_sight_height and have to be taken the same way at both ends.

    The ladder is printed rather than swallowed because this is a found number
    and not a named one. A re-export that raises a sight base or shortens the
    rail shows up as the rungs moving, which is visible; a rule that silently
    answered a different height would not be."""
    step = length * 0.01
    reach = {}
    for _, _, group in claimed:
        points, normals = ai.vertices(group), group['normals']
        indices = group['indices']
        for i in range(0, len(indices), 3):
            a, b, c = indices[i:i + 3]
            if normals[a * 3 + up] < 0.9:
                continue
            triangle = [points[a], points[b], points[c]]
            height = round(sum(v[up] for v in triangle) / 3.0, 1)
            for v in triangle:
                reach.setdefault(height, set()).add(round(v[axis] / step))
    covered = {y: len(bins) * step for y, bins in reach.items()}
    rail = max(y for y, run in covered.items() if run >= length * RAIL_REACH)
    for y in sorted((y for y in covered if y > rail), reverse=True)[:4]:
        print('  above the rail: %s %5.2f runs %4.1f%% of the length'
              % ('xyz'[up], y, 100.0 * covered[y] / length))
    print('  rail top at file %s %.2f, running %.0f%% of the length - an optic '
          'modelled seated on this gun has to agree'
          % ('xyz'[up], rail, 100.0 * covered[rail] / length))
    return rail


def receiver_back(claimed, manifest, axis, low, high):
    """Where along the length the origin goes, in file units.

    The bore at the BACK of the receiver, which is what every offset in
    src/gameplay/weapons/ is measured from. Which point that is cannot be
    inferred:
    on a gun whose chassis is one mesh there is no seam between receiver and
    stock to find, so a manifest that has a part to name names it, and one that
    has not says so and takes the rearmost point of the gun - which on a pistol
    IS the back of the frame, and on a stocked gun is the buttplate and is
    stated in the manifest as such."""
    # The back of the gun is the low end of the length axis when the muzzle is
    # at the high one, and the high end when it is not.
    back = min if muzzle_at_high(manifest['axes'], axis) else max
    if 'receiver' not in manifest.data:
        return back([low[axis], high[axis]])
    groups = part_of(claimed, manifest.data, 'receiver')
    rlow, rhigh = ai.bounds(groups)
    return back([rlow[axis], rhigh[axis]])


# --------------------------------------------------------------------------
# The import

def build(folder):
    manifest = ai.Manifest(folder)
    if manifest.get('generator') != GENERATOR:
        raise ValueError('%s names %r, not this script'
                         % (folder, manifest.get('generator')))
    print('=== %s' % folder)

    everything = ai.load_parts(manifest.path('source'))
    materials = manifest['materials']
    claimed, dropped = claim(everything, materials,
                             manifest.get('drop', ()))
    if not claimed:
        raise ValueError('%s holds nothing this manifest names'
                         % manifest.path('source'))
    if dropped:
        print('not this gun: %s' % ', '.join(dropped))

    axis = manifest['length_axis']
    # And the irons, where there was no seam to take them off by. Before the
    # bounds and before the bore, because a sight that is not part of the gun
    # must not be part of what the gun is measured FROM either - on the M9 the
    # front blade reaches into the last twentieth of the length, which is the
    # stretch the bore line is averaged over.
    claimed = carve(claimed, manifest.get('cut', ()), axis,
                    [c for c in range(3) if c != axis][1])
    low, high = ai.bounds([g for _, _, g in claimed])
    scale = ai.scale_for(low, high, axis, manifest['overall_length'])
    bore = bore_line(claimed, manifest, axis, low, high)

    origin = [0.0, 0.0, 0.0]
    for c in bore:
        origin[c] = bore[c]
    origin[axis] = receiver_back(claimed, manifest, axis, low, high)
    frame = ai.Frame(manifest['axes'], origin, scale)

    # What the maps average across the surface that reads them, and the lift
    # that brings it to the figure every other asset is matched to. One gain
    # over the whole gun - see any manifest for why not one per material.
    parts = [(node, output, group) for output, node, group in claimed]
    # How much of the artist's occlusion to fold in. Full unless a manifest
    # says otherwise, which is the case a gun makes when its map darkens the
    # flats as hard as the crevices - see any manifest that states it.
    ao_strength = manifest.get('ao_strength', 1.0)
    mean = ai.surface_albedo(manifest.textures, materials, parts, ao_strength)
    gain = manifest['target_albedo'] / mean if mean > 0.5 else 1.0
    print('surface averages %.1f -> %.1f  (gain %.2fx)'
          % (mean, mean * gain, gain))

    # Every material first, then every primitive - the order the buffer is laid
    # out in, and the order every asset here is written in.
    primitives = ai.Primitives(GENERATOR, frame)
    images, ids = {}, {}
    for name, spec in materials.items():
        source = spec.get('share', name)
        if source not in images:
            png = ai.texture_png(manifest.textures, materials[source]['map'],
                                 materials[source].get('ao'),
                                 manifest['max_texture'], gain,
                                 ao_strength) \
                if 'map' in materials[source] else None
            # Through glb.image directly rather than through Primitives, so a
            # material that shares another's atlas shares the ONE copy of it in
            # the buffer instead of baking a second megabyte of the same pixels.
            images[source] = (primitives.glb.image(png) if png else None)
        ids[name] = primitives.glb.material(
            name, tuple(spec.get('color', (1.0, 1.0, 1.0, 1.0))),
            images[source])
    # One primitive per MATERIAL, not one per node.
    #
    # The engine loads at most STATIC_MAX_MESHES of them and the revolver
    # arrives as twenty-three - a frame, a cylinder, a crane, and eight rounds
    # with eight cases - every one of which reads the same atlas. Kept apart
    # they would be twenty-three draws off one texture and, before that, a mesh
    # the loader refuses. Merged they are one, and nothing downstream can tell
    # the difference: the magazine and the hidden parts are named by material,
    # which is exactly the seam that survives here.
    for output in materials:
        nodes = [node for out, node, _ in claimed if out == output]
        groups = [g for out, _, g in claimed if out == output]
        if not groups:
            continue
        primitives.add('%s (%d)' % (output, len(nodes)) if len(nodes) > 1
                       else output, ids[output], ai.merge(groups))
    primitives.write(manifest.path('output'))

    side, up = [c for c in range(3) if c != axis]
    front, back = (high, low) if muzzle_at_high(manifest['axes'], axis) \
        else (low, high)
    muzzle = frame.place(*[front[axis] if c == axis else bore[c]
                           for c in range(3)])
    measured = {
        'muzzle_forward': -muzzle[2],
        'muzzle_up': muzzle[1],
        'butt_back': abs(frame.along(axis, back[axis])),
        'lowest_point': (low[up] - bore[up]) * scale,
        'half_width': (high[side] - low[side]) * 0.5 * scale,
    }
    if manifest.get('rail'):
        rail = rail_top(claimed, axis, up, high[axis] - low[axis])
        measured['rail_height'] = (rail - bore[up]) * scale
    print('scale %.6f m per unit, %.3f m overall'
          % (scale, manifest['overall_length']))
    ai.write_measured(folder, measured)


def main(argv):
    folders = argv[1:]
    if not folders:
        folders = sorted(
            os.path.join(WEAPONS, name) for name in os.listdir(WEAPONS)
            if os.path.exists(os.path.join(WEAPONS, name, 'import.json')))
        folders = [f for f in folders
                   if ai.Manifest(f).get('generator') == GENERATOR]
    for folder in folders:
        build(folder)
        print()


if __name__ == '__main__':
    main(sys.argv)
