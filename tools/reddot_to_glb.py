#!/usr/bin/env python3
"""Converts the tube red dot into the binary glTF the engine loads.

The optic was modelled into the carbine's scene rather than shipped on its
own, so this reads the rifle's FBX and keeps two nodes out of twelve. That is
the whole reason this is a second script and not a second copy of the asset:
one file on disk, two things imported out of it, and no way for a re-export to
leave the gun and the sight on different vintages of the same mesh.

What every import does is in tools/asset_import.py. What is true of an optic
and of nothing else is in tools/scope_to_glb.py's docstring and holds here
too - the origin on the aim point, the rail seat, the window kept translucent.
What is different about THIS optic, against the holographic sight:

  * It is a tube. The EXPS2's window is a flat pane and its origin is the
    centre of that pane; this one's glass is a cylinder, and the point the eye
    lines up with is the axis of it. So the origin is the tube's axis at its
    midpoint, and the window measurements come off the tube's diameter rather
    than off a rectangle.

  * Its materials are not an atlas. The EXPS2 arrived as nine materials all
    indexing into one 2048px image, which is why that script measures which
    material lands on an island, merges what does into a single primitive and
    flat-shades what does not. This one has a map per material like the guns
    do, so none of that applies and none of it is here.

  * The scale is the rifle's. Metres per unit is a property of the file, and
    this file is mostly rifle - see the manifest for why measuring it off the
    optic alone would be wrong.

The reticle itself is not in this file; it is drawn by the renderer, at the
centre of the screen, because the dot has to sit exactly where the bullet goes
and the bullet leaves the eye.

Everything else is stated in models/attachments/sight_dot/import.json.

Usage:  python3 tools/reddot_to_glb.py   # needs Pillow, for the textures only
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import asset_import as ai

FOLDER = 'models/attachments/sight_dot'


def main():
    manifest = ai.Manifest(FOLDER)
    everything = ai.load_parts(manifest.path('source'))

    # The scale comes off the WHOLE file - it is the rifle's length that is
    # known, and the optic is in proportion to it - and only then is the optic
    # taken out of it. Doing it the other way round would scale this mesh by a
    # dimension nobody measured.
    axis = manifest['length_axis']
    whole = ai.bounds([g for _, _, g in everything])
    scale = ai.scale_for(whole[0], whole[1], axis, manifest['overall_length'])

    wanted = manifest['materials']
    parts = [(material, group) for _, material, group in everything
             if material in wanted]
    if not parts:
        raise ValueError('%s holds nothing this manifest names'
                         % manifest.path('source'))

    glass_name = manifest['glass']
    glass = ai.merge([g for material, g in parts if material == glass_name])
    if not glass['indices']:
        raise ValueError('%s has no %s material; the window is what the origin '
                         'is measured from' % (manifest.path('source'), glass_name))

    groups = [g for _, g in parts]
    low, high = ai.bounds(groups)

    # The seat: the top of the rail this optic is modelled sitting on. Not
    # inferred from the optic's own shape, because it does not have to be -
    # gun and sight are in one file and one set of coordinates, and where the
    # artist rested it IS the seat. See the manifest for why the other optic's
    # rule does not survive the trip.
    rail = [v[1] for _, material, g in everything
            if material == manifest['seat_rail'] for v in ai.vertices(g)]
    if not rail:
        raise ValueError('no material named %r to take the seat from'
                         % manifest['seat_rail'])
    seat = max(rail)

    # And the same question asked the other optic's way, printed beside it. The
    # two agreeing is the check that the sight is still ON the rail: an export
    # that floats it, or a mount rebuilt with a different foot, shows up here as
    # a disagreement instead of quietly shifting the mount height.
    guessed = ai.rail_seat(groups)
    print('seat: rail top %.2f, the optic\'s own largest flat %.2f, apart by '
          '%.2f units (%.1f mm)'
          % (seat, guessed, guessed - seat, (guessed - seat) * scale * 1000.0))

    # The aim point: the axis of the tube, at its middle. A collimated dot is
    # projected down that axis, so this is where the eye goes and the point
    # every number in the gun's spec is measured from. Taken as the centre of
    # the glass's box on the two axes across the tube, and its midpoint along.
    tube = ai.vertices(glass)
    origin = [(min(v[c] for v in tube) + max(v[c] for v in tube)) * 0.5
              for c in range(3)]
    frame = ai.Frame(manifest['axes'], origin, scale)

    # What the body averages across the surface that reads it, and the lift
    # that brings it to the figure every other asset is matched to. An optic
    # bolted to a rifle has to read as the same material under the same lamp,
    # and this one arrives a third darker than the gun it sits on.
    mean = ai.surface_albedo(manifest.textures, wanted,
                             [(m, m, g) for m, g in parts])
    gain = manifest['target_albedo'] / mean if mean > 0.5 else 1.0
    print('surface averages %.1f -> %.1f  (gain %.2fx)'
          % (mean, mean * gain, gain))

    # Every material first, then every primitive - the order the buffer is laid
    # out in, and the order every asset here is written in.
    primitives = ai.Primitives(manifest['generator'], frame)
    material_ids = {}
    for name, spec in wanted.items():
        # The window is a tint and no map: it has to stay translucent, and the
        # renderer builds its stencil mask out of whatever draws translucent.
        if name == glass_name:
            material_ids[name] = primitives.material(
                name, tuple(manifest['glass_tint']))
            continue
        png = ai.texture_png(manifest.textures, spec['map'], spec.get('ao'),
                             manifest['max_texture'],
                             gain) if 'map' in spec else None
        material_ids[name] = primitives.material(
            name, spec.get('color', (1.0, 1.0, 1.0, 1.0)), png)
    # The body before the glass, so the hood is in the depth buffer first.
    for material, group in sorted(parts, key=lambda p: p[0] == glass_name):
        primitives.add(material, material_ids[material], group)
    primitives.write(manifest.path('output'))

    across = [c for c in range(3) if c != axis]
    side, up = across[0], across[1]

    # How much of the rail the foot actually covers: this optic's own vertices
    # down at the seat. What the gun's spec clamps with rail_forward, so that a
    # sight can be checked against the stretch of rail it is sitting on.
    foot = [v[axis] for g in groups for v in ai.vertices(g)
            if abs(v[up] - seat) < 0.25]
    if not foot:
        raise ValueError('nothing of this optic reaches the seat at %.2f' % seat)
    print('scale %.6f m per unit, %.4f m long, %.4f m tall, %.4f m wide'
          % (scale, (high[axis] - low[axis]) * scale,
             (high[up] - low[up]) * scale, (high[side] - low[side]) * scale))
    ai.write_measured(FOLDER, {
        'reticle_height': (origin[up] - seat) * scale,
        'window_width': (max(v[side] for v in tube)
                         - min(v[side] for v in tube)) * scale,
        'window_height': (max(v[up] for v in tube)
                          - min(v[up] for v in tube)) * scale,
        'hood_front': -frame.along(axis, high[axis]),
        'body_back': -frame.along(axis, low[axis]),
        'rail_seat_from': -frame.along(axis, max(foot)),
        'rail_seat_to': -frame.along(axis, min(foot)),
        'lowest_point': (low[up] - origin[up]) * scale,
    })


if __name__ == '__main__':
    main()
