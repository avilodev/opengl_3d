#!/usr/bin/env python3
"""Converts the SORVO-19 open reflex sight into the binary glTF the engine loads.

What every import does - node transforms, axes and scale, the glb write - is in
tools/asset_import.py. What is true of an optic and of nothing else is in
tools/scope_to_glb.py's docstring and holds here too: the origin on the aim
point, the rail seat measured so the gun's half and the sight's half can be
added, the window kept translucent. What is different about THIS optic:

  * The aim point is AUTHORED. Every other optic here infers its origin - the
    centre of a pane, the axis of a tube - because nothing in the file says
    where the dot goes. This artist left a 14-triangle plane flat against the
    lens, on the optical axis, at the pane's mid-thickness, and that is a
    better answer than any box centre because it is the answer the person who
    modelled the sight gave. It is read for its position and then thrown away;
    the dot itself is drawn by the renderer.

  * It is an open reflex, not a tube and not a hologram. The glass is a single
    curved pane with nothing around it, so there is no hood to occlude with and
    the window is the widest of the four optics - which is the argument for
    this kind of sight and the reason it is worth carrying beside the EXPS2.

  * The materials have no maps. The FBX declares nine materials and not one
    texture link, so which image belongs to which material is settled in the
    manifest by UV range rather than read out of the file. The five that tile
    all read the same scratched-steel map, so they are merged into one
    primitive the way the EXPS2's atlas materials are - one image, one draw -
    and the one whose UVs are pinned to a single point is drawn flat, because
    there is no unwrap there to sample.

The reticle is not in this file. It is drawn by the renderer, at the centre of
the screen, because the dot has to sit exactly where the bullet goes and the
bullet leaves the eye.

Everything else is stated in models/attachments/sight_reflex/import.json.

Usage:  python3 tools/reflex_to_glb.py   # needs Pillow, for the textures only
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import asset_import as ai

FOLDER = 'models/attachments/sight_reflex'


def flat_colour(folder, name, gain):
    """One image's mean, as a colour, for a part with no unwrap to sample.

    Its UVs are all pinned to a single point, so a map on it would read one
    texel across the whole part. This is that texel, taken honestly - the mean
    rather than whichever corner the UVs happen to land in - and it costs no
    texture to do."""
    from PIL import Image, ImageStat
    image = Image.open(os.path.join(folder, name)).convert('RGB')
    mean = ImageStat.Stat(image).mean
    return tuple(min(1.0, c / 255.0 * gain) for c in mean) + (1.0,)


def main():
    manifest = ai.Manifest(FOLDER)
    everything = ai.load_parts(manifest.path('source'))

    axis = manifest['length_axis']
    whole = ai.bounds([g for _, _, g in everything])
    scale = ai.scale_for(whole[0], whole[1], axis, manifest['overall_length'])

    # The aim point, off the marker the artist left against the lens. Read for
    # its centre and then dropped: a quad in the mesh would be a second dot
    # that disagreed with the drawn one the moment the gun moved.
    marker = [g for _, material, g in everything
              if material == manifest['reticle']]
    if not marker:
        raise ValueError('no material named %r; the reticle marker is what the '
                         'origin is measured from' % manifest['reticle'])
    mark = ai.vertices(ai.merge(marker))
    origin = [(min(v[c] for v in mark) + max(v[c] for v in mark)) * 0.5
              for c in range(3)]
    frame = ai.Frame(manifest['axes'], origin, scale)

    # The seat, over the mount alone. Run over the whole model the rule lands
    # on the underside of the sight body where it meets its own riser - a
    # broader flat, higher up, that outweighs the foot. See the manifest.
    mount = [g for node, _, g in everything if node == manifest['seat_node']]
    if not mount:
        raise ValueError('no node named %r to take the seat from'
                         % manifest['seat_node'])
    seat = ai.rail_seat(mount)

    wanted = manifest['materials']
    parts = [(material, group) for _, material, group in everything
             if material in wanted]
    missing = set(wanted) - {m for m, _ in parts}
    if missing:
        raise ValueError('%s names materials the mesh does not have: %s'
                         % (os.path.join(FOLDER, 'import.json'),
                            ', '.join(sorted(missing))))

    glass_name = manifest['glass']
    glass = ai.merge([g for _, material, g in everything
                      if material == glass_name])
    if not glass['indices']:
        raise ValueError('%s has no %s material; the window is what the sight '
                         'is seen through' % (manifest.path('source'), glass_name))

    # What the surface averages, and the lift that brings it to the figure
    # every other asset is matched to. An optic bolted to a rifle has to read
    # as the same material under the same lamp.
    mean = ai.surface_albedo(manifest.textures, wanted,
                             [(m, m, g) for m, g in parts])
    gain = manifest['target_albedo'] / mean if mean > 0.5 else 1.0
    print('surface averages %.1f -> %.1f  (gain %.2fx)'
          % (mean, mean * gain, gain))

    # Every material first, then every primitive - the order the buffer is laid
    # out in, and the order every asset here is written in. The ones that share
    # a map share a material too, which is what makes them one draw.
    primitives = ai.Primitives(manifest['generator'], frame)
    material_ids, merged = {}, {}
    for name, spec in wanted.items():
        group = spec.get('merge')
        if group and group in merged:
            material_ids[name] = merged[group]
            continue
        if 'flat' in spec:
            ident = primitives.material(
                name, flat_colour(manifest.textures, spec['flat'], gain))
        else:
            png = ai.texture_png(manifest.textures, spec['map'],
                                 spec.get('ao'), manifest['max_texture'], gain)
            ident = primitives.material(name, (1.0, 1.0, 1.0, 1.0), png)
        material_ids[name] = ident
        if group:
            merged[group] = ident
    glass_material = primitives.material('glass', tuple(manifest['glass_tint']))

    # The merged ones as a single primitive, then whatever stands alone, then
    # the glass - last, so the housing is in the depth buffer before anything
    # translucent draws over it.
    for group in dict.fromkeys(s['merge'] for s in wanted.values()
                               if 'merge' in s):
        names = [n for n, s in wanted.items() if s.get('merge') == group]
        primitives.add(group, material_ids[names[0]],
                       ai.merge([g for m, g in parts if m in names]))
    for name, spec in wanted.items():
        if 'merge' in spec:
            continue
        primitives.add(name, material_ids[name],
                       ai.merge([g for m, g in parts if m == name]))
    primitives.add('glass', glass_material, glass)
    primitives.write(manifest.path('output'))

    across = [c for c in range(3) if c != axis]
    side, up = across[0], across[1]
    groups = [g for _, g in parts] + [glass]
    low, high = ai.bounds(groups)

    # The window: the glass AROUND THE RETICLE, which on this optic is not all
    # of the glass. The emitter's lens carries the same material and sits 36mm
    # behind the pane, and averaged in it would report a window twice as deep
    # as the aperture actually is.
    reach = manifest['window_from_reticle'] / scale
    pane = [v for v in ai.vertices(glass) if abs(v[axis] - origin[axis]) < reach]
    if not pane:
        raise ValueError('no glass within %.1fmm of the reticle'
                         % (manifest['window_from_reticle'] * 1000.0))

    # How much of the rail the foot covers, so a sight can be checked against
    # the stretch of rail it is sitting on.
    foot = [v[axis] for g in groups for v in ai.vertices(g)
            if abs(v[up] - seat) < 0.25]
    if not foot:
        raise ValueError('nothing of this optic reaches the seat at %.2f' % seat)
    print('scale %.6f m per unit, %.4f m long, %.4f m tall, %.4f m wide'
          % (scale, (high[axis] - low[axis]) * scale,
             (high[up] - low[up]) * scale, (high[side] - low[side]) * scale))
    ai.write_measured(FOLDER, {
        'reticle_height': (origin[up] - seat) * scale,
        'window_width': (max(v[side] for v in pane)
                         - min(v[side] for v in pane)) * scale,
        'window_height': (max(v[up] for v in pane)
                          - min(v[up] for v in pane)) * scale,
        'hood_front': frame.along(axis, low[axis]),
        'body_back': frame.along(axis, high[axis]),
        'rail_seat_from': frame.along(axis, min(foot)),
        'rail_seat_to': frame.along(axis, max(foot)),
        'lowest_point': (low[up] - origin[up]) * scale,
    })


if __name__ == '__main__':
    main()
