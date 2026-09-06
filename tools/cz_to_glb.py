#!/usr/bin/env python3
"""Converts the CZ 806 Bren 2 FBX into the binary glTF the engine loads.

What every import does - node transforms, axes and scale, the texture folding
and the glb write - is in tools/asset_import.py, which the M4, the katana and
both optics call too. What is here is the three things that are true of THIS
gun and of nothing else:

  * The optic left out. The artist modelled a tube red dot into the same
    scene as the rifle, two mesh nodes of it, and it is not furniture: it is
    a sight, it mounts to a rail, and every other gun in the game should be
    able to carry it. So it is imported separately by tools/reddot_to_glb.py
    out of this same file, and what tells the two scripts apart is that each
    manifest names the materials its asset is made of. A part whose material
    this manifest does not name is not this gun.

  * The origin, moved onto the bore line at the back of the receiver, which is
    where the viewmodel offsets in src/gameplay/weapons/ are measured from. The
    M4's file happened to have its own zero already there and its importer
    could leave the origin alone on that axis; this file's zero is out at the
    magwell, near enough the middle of the gun, and an origin there would have
    the viewmodel holding the rifle by its balance point. So the landmark is
    measured rather than assumed: the rearmost point of the receiver shell,
    which is where the stock takes over.

  * The rail's own height above the bore, measured and WRITTEN to
    measured.json rather than printed for somebody to copy. That number is the
    gun's half of the sight mount; the optic's half is measured by
    reddot_to_glb.py the same way, and weapon_spec_sight_height adds them.

The rear iron sight is NOT folded here, and that is the difference from the
M4. This gun's irons are a single front block on the gas tube - there is no
rear leaf standing in the sight picture to take out.

Everything else about the import is stated in models/weapons/cz_806_carbine/
import.json, beside the asset it describes.

Usage:  python3 tools/cz_to_glb.py   # needs Pillow, for the textures only
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import asset_import as ai

FOLDER = 'models/weapons/cz_806_carbine'


def main():
    manifest = ai.Manifest(FOLDER)
    everything = ai.load_parts(manifest.path('source'))

    # The gun is what the manifest names. Done before the bounds, unlike the
    # M4's rear-sight fold, because the optic is a separate ASSET rather than a
    # piece of this one taken off: leaving it in would put the scale and the
    # muzzle in the gun's spec on a bounding box that includes a sight.
    wanted = manifest['materials']
    parts = [p for p in everything if p[1] in wanted]
    dropped = sorted({m for _, m, _ in everything if m not in wanted})
    if not parts:
        raise ValueError('%s holds nothing this manifest names'
                         % manifest.path('source'))
    print('not this gun: %s' % ', '.join(dropped))

    axis = manifest['length_axis']
    low, high = ai.bounds([g for _, _, g in parts])
    scale = ai.scale_for(low, high, axis, manifest['overall_length'])

    # The bore, not the mesh's centre: the barrel is the only part of the gun
    # whose height the viewmodel's numbers care about, and the last stretch of
    # the length axis is barrel and nothing else.
    length = high[axis] - low[axis]
    barrel = [p for _, _, g in parts for p in ai.vertices(g)
              if p[axis] > high[axis] - length * 0.05]
    across = [c for c in range(3) if c != axis]
    bore = {c: (min(p[c] for p in barrel) + max(p[c] for p in barrel)) * 0.5
            for c in across}

    # Along the length, the back of the receiver shell - see the manifest.
    receiver = [p[axis] for _, material, g in parts if material == manifest['receiver']
                for p in ai.vertices(g)]
    if not receiver:
        raise ValueError('no material named %r to measure the receiver from'
                         % manifest['receiver'])
    origin = [0.0, 0.0, 0.0]
    for c in across:
        origin[c] = bore[c]
    origin[axis] = min(receiver)
    frame = ai.Frame(manifest['axes'], origin, scale)

    # What the maps average across the surface that reads them, and the lift
    # that brings it to the figure every other asset is matched to. Measured
    # over this gun's own triangles rather than over its images - see the
    # manifest for why there is one gain and not one per material.
    mean = ai.surface_albedo(manifest.textures, wanted, parts)
    gain = manifest['target_albedo'] / mean if mean > 0.5 else 1.0
    print('surface averages %.1f -> %.1f  (gain %.2fx)'
          % (mean, mean * gain, gain))

    primitives = ai.Primitives(manifest['generator'], frame)
    # Every material first, then every primitive - the order the buffer is laid
    # out in, and the order every asset here is written in.
    material_ids = {}
    for name, spec in wanted.items():
        png = ai.texture_png(manifest.textures, spec['map'], spec.get('ao'),
                             manifest['max_texture'],
                             gain) if 'map' in spec else None
        material_ids[name] = primitives.material(
            name, spec.get('color', (1.0, 1.0, 1.0, 1.0)), png)
    for name, material, group in parts:
        primitives.add(name, material_ids[material], group)
    primitives.write(manifest.path('output'))

    # What an optic clamps to: the top of the material the manifest names as
    # the rail. The M4's importer has to hunt for this - a strip near the bore
    # plane, behind the front sight - because on that mesh the rail is not a
    # material of its own and the highest thing on the gun is whatever the
    # bounding box says. Here it is named, so there is nothing to hunt: the
    # rail's own highest point is the rail top, and neither the block at the
    # stock junction nor the front sight can be mistaken for it.
    side, up = across[0], across[1]
    rail = [p[up] for _, material, g in parts if material == manifest['rail']
            for p in ai.vertices(g)]
    if not rail:
        raise ValueError('no material named %r to measure the rail from'
                         % manifest['rail'])
    print('rail top at file %s %.2f - an optic modelled seated on this gun '
          'has to agree' % ('xyz'[up], max(rail)))

    muzzle = frame.place(*[high[axis] if c == axis else bore[c] for c in range(3)])
    print('scale %.6f m per unit, %.3f m overall'
          % (scale, manifest['overall_length']))
    ai.write_measured(FOLDER, {
        'muzzle_forward': -muzzle[2],
        'muzzle_up': muzzle[1],
        'rail_height': (max(rail) - bore[up]) * scale,
        'butt_back': -frame.along(axis, low[axis]),
        'lowest_point': (low[up] - bore[up]) * scale,
        'half_width': (high[side] - low[side]) * 0.5 * scale,
    })


if __name__ == '__main__':
    main()
