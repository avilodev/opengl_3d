#!/usr/bin/env python3
"""Converts the Trijicon MRO and its mount into the binary glTF the engine loads.

What every import does - node transforms, axes and scale, the glb write - is in
tools/asset_import.py. What is true of an optic and of nothing else is in
tools/scope_to_glb.py's docstring and holds here too: the origin on the aim
point, the rail seat measured so the gun's half and the sight's half can be
added, the window kept translucent. What is different about THIS optic:

  * The scale comes off the RAIL. The artist modelled a length of Picatinny
    under the mount, and a rail is the one thing in any of these files with a
    published dimension - MIL-STD-1913 fixes it at 21.2mm across. Every other
    optic here is scaled by somebody recalling how long the real thing is; this
    one is scaled by a standard, and the optic that falls out of it is 63.7mm
    where a Trijicon MRO is 66mm. See the manifest for the second check.

  * The rail is measured and then dropped. It is the seat - where the artist
    rested the optic is not something to infer - but exporting it would ship a
    second rail to sit on top of the gun's own.

  * The glass is not named. This delivery is ONE material over the whole optic,
    so unlike every other optic here the window cannot be looked up and has to
    be found. A lens is the only thing on this mesh that is a filled disc
    square to the optical axis, which is what glass_of below tests for; the
    bezel around each lens is an annulus in the same plane and never reaches
    the centre, so the radius is what separates them.

  * The optical axis is found, not stated. It is the centroid of everything
    square to the axis at the two ends - lens and bezel together, which are
    concentric. The bounding box would not do: the turrets pull it 6.7 units
    off the axis across and 23 up.

The reticle is not in this file. It is drawn by the renderer, at the centre of
the screen, because the dot has to sit exactly where the bullet goes and the
bullet leaves the eye.

Everything else is stated in models/attachments/sight_mro/import.json.

Usage:  python3 tools/mro_to_glb.py   # needs Pillow, for the textures only
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import asset_import as ai

FOLDER = 'models/attachments/sight_mro'


def triangles(group):
    """(the three vertex indices, the mean normal) for each triangle."""
    normals, indices = group['normals'], group['indices']
    for i in range(0, len(indices), 3):
        triangle = indices[i:i + 3]
        yield triangle, [sum(normals[v * 3 + c] for v in triangle) / 3.0
                         for c in range(3)]


def end_faces(group, axis, depth):
    """The triangles square to the optical axis within `depth` of either end.

    Lens and bezel both, because at this stage there is nothing to tell them
    apart with - the axis they are measured against is what this is for."""
    points = ai.vertices(group)
    low = min(v[axis] for v in points)
    high = max(v[axis] for v in points)
    for triangle, normal in triangles(group):
        if abs(normal[axis]) < 0.9:
            continue
        along = sum(points[v][axis] for v in triangle) / 3.0
        if along < low + depth or along > high - depth:
            yield triangle, along < (low + high) * 0.5


def openings(group, axis, depth):
    """(the optical axis, and for each end its aperture and the plane of it).

    The axis first, as the centroid of everything square to it at the two ends
    - bezel and bore together, concentric either way. Then at each end the
    aperture is the smallest radius anything reaches, because the hole is what
    nothing occupies, and the plane is where that radius sits: the shelf the
    lens would have been seated on."""
    points = ai.vertices(group)
    across = [c for c in range(3) if c != axis]
    faces = list(end_faces(group, axis, depth))
    if not faces:
        raise ValueError('nothing on this mesh is square to axis %d at its '
                         'ends; the openings are what the window fills' % axis)
    seen = {v for triangle, _ in faces for v in triangle}
    centre = [sum(points[v][c] for v in seen) / len(seen) if c in across else 0.0
              for c in range(3)]

    ends = {}
    for front in (True, False):
        at = {v for triangle, is_front in faces if is_front == front
              for v in triangle}
        if not at:
            raise ValueError('the %s end of this mesh has no face square to '
                             'the axis' % ('front' if front else 'back'))
        radius = {v: sum((points[v][c] - centre[c]) ** 2
                         for c in across) ** 0.5 for v in at}
        bore = min(radius.values())
        # The plane of the rim itself, not of the end: on both ends the bore is
        # a step set back from the bezel, and a disc on the bezel's plane would
        # float in front of the hole it is filling.
        rim = [v for v in at if radius[v] < bore * 1.01]
        ends[front] = (bore, sum(points[v][axis] for v in rim) / len(rim))
    return centre, ends


def disc(centre, axis, plane, radius, outward, segments):
    """A flat circle of glass, filling one open end.

    Wound so that its face points `outward` along the axis, which for the
    ocular is at the eye - the one the shooter is actually looking through."""
    across = [c for c in range(3) if c != axis]
    group = {'positions': [], 'normals': [], 'uvs': [], 'indices': []}
    normal = [0.0, 0.0, 0.0]
    normal[axis] = outward

    def vertex(u, v):
        point = [0.0, 0.0, 0.0]
        point[axis] = plane
        point[across[0]] = centre[across[0]] + u
        point[across[1]] = centre[across[1]] + v
        group['positions'] += point
        group['normals'] += normal
        # Across the disc rather than off the housing's unwrap: this primitive
        # has no map, and a UV that indexed into one would be a claim it does.
        group['uvs'] += [0.5 + 0.5 * u / radius, 0.5 + 0.5 * v / radius]

    import math
    vertex(0.0, 0.0)
    for i in range(segments):
        angle = 2.0 * math.pi * i / segments
        vertex(radius * math.cos(angle), radius * math.sin(angle))
    for i in range(segments):
        a, b = 1 + i, 1 + (i + 1) % segments
        group['indices'] += [0, a, b] if outward > 0.0 else [0, b, a]
    return group


def main():
    manifest = ai.Manifest(FOLDER)
    everything = ai.load_parts(manifest.path('source'))

    # The scale, off the rail. A standard rather than a recollection.
    rail = [g for node, _, g in everything if node == manifest['scale_reference']]
    if not rail:
        raise ValueError('no node named %r to take the scale from'
                         % manifest['scale_reference'])
    low, high = ai.bounds(rail)
    scale = ai.scale_for(low, high, manifest['scale_axis'],
                         manifest['scale_metres'])

    # The seat: the top of the rail the optic is modelled sitting on. Not
    # inferred from the optic's own shape - this mount's jaws wrap the rail and
    # hang below its top face, so a rule that looks underneath would find the
    # jaws and seat the optic several millimetres too low.
    seat = max(v[1] for g in rail for v in ai.vertices(g))
    guessed = ai.rail_seat(rail)
    print('seat: rail top %.2f, its own largest flat %.2f, apart by %.2f units '
          '(%.1f mm)' % (seat, guessed, guessed - seat,
                         (guessed - seat) * scale * 1000.0))

    # The two lenses, and with them the optical axis they are centred on -
    # looked for on the optic alone. The mount reaches a millimetre further
    # forward than the optic does, so a search over both takes its front face
    # for the objective end and drags the axis down to the middle of a bracket.
    axis = 2
    scope = ai.merge([g for node, _, g in everything
                      if node == manifest['glass_node']])
    if not scope['indices']:
        raise ValueError('%s has no %s node; the lenses are what the optical '
                         'axis is found from'
                         % (manifest.path('source'), manifest['glass_node']))
    centre, ends = openings(scope, axis, manifest['lens_depth'] / scale)
    shell = ai.merge([g for node, _, g in everything
                      if node in manifest['body_nodes']])

    # The glass, made rather than found - there is none in the file. A disc per
    # end, on the shelf its lens would have been seated on, at the radius of
    # the bore it plugs, facing out of its own end.
    segments = manifest['lens_segments']
    lenses = {}
    for front, (bore, plane) in ends.items():
        lenses[front] = disc(centre, axis, plane, bore,
                             -1.0 if front else 1.0, segments)
        print('%s opening: %.1f units across (%.1f mm) at %.2f'
              % ('objective' if front else 'ocular', bore * 2.0,
                 bore * 2.0 * scale * 1000.0, plane))
    glass = ai.merge(list(lenses.values()))

    # The aim point: the axis of the tube, midway between its lenses. A
    # collimated dot is projected down that axis, so this is where the eye goes
    # and the point every number in the gun's spec is measured from. The same
    # rule the carbine's tube dot uses, and for the same reason.
    origin = list(centre)
    origin[axis] = sum(plane for _, plane in ends.values()) * 0.5
    frame = ai.Frame(manifest['axes'], origin, scale)

    wanted = manifest['materials']
    mean = ai.surface_albedo(manifest.textures, wanted,
                             [(m, m, g) for _, m, g in everything
                              if m in wanted],
                             manifest['ao_strength'])
    gain = manifest['target_albedo'] / mean if mean > 0.5 else 1.0
    print('surface averages %.1f -> %.1f  (occlusion %.0f%%, gain %.2fx)'
          % (mean, mean * gain, manifest['ao_strength'] * 100.0, gain))

    # Every material first, then every primitive - the order the buffer is laid
    # out in, and the order every asset here is written in.
    primitives = ai.Primitives(manifest['generator'], frame)
    name, spec = next(iter(wanted.items()))
    shell_material = primitives.material(
        name, (1.0, 1.0, 1.0, 1.0),
        ai.texture_png(manifest.textures, spec['map'], spec.get('ao'),
                       manifest['max_texture'], gain, manifest['ao_strength']))
    glass_material = primitives.material('glass', tuple(manifest['glass_tint']))
    # The body before the glass, so the housing is in the depth buffer first.
    primitives.add('body', shell_material, shell)
    primitives.add('glass', glass_material, glass)
    primitives.write(manifest.path('output'))

    across = [c for c in range(3) if c != axis]
    side, up = across[0], across[1]
    low, high = ai.bounds([shell, glass])

    # The window is the OBJECTIVE, which is the narrower of the two openings
    # and so the one that bounds the sight picture. The ocular is wider and
    # lets the eye find the glass; what the eye can see THROUGH is this.
    objective = ends[True][0] * 2.0 * scale

    foot = [v[axis] for v in ai.vertices(shell) if abs(v[up] - seat) < 0.25]
    if not foot:
        raise ValueError('nothing of this optic reaches the seat at %.2f' % seat)
    print('scale %.6f m per unit, %.4f m long, %.4f m tall, %.4f m wide'
          % (scale, (high[axis] - low[axis]) * scale,
             (high[up] - low[up]) * scale, (high[side] - low[side]) * scale))
    ai.write_measured(FOLDER, {
        'reticle_height': (origin[up] - seat) * scale,
        'window_width': objective,
        'window_height': objective,
        'ocular_width': ends[False][0] * 2.0 * scale,
        'hood_front': frame.along(axis, low[axis]),
        'body_back': frame.along(axis, high[axis]),
        'rail_seat_from': frame.along(axis, min(foot)),
        'rail_seat_to': frame.along(axis, max(foot)),
        'lowest_point': (low[up] - origin[up]) * scale,
    })


if __name__ == '__main__':
    main()
