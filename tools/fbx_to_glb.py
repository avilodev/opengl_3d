#!/usr/bin/env python3
"""Converts the M4 carbine FBX into the binary glTF the engine loads.

The engine reads .glb and nothing else, and no FBX importer is part of the
toolchain, so this is the import step. It is checked in rather than run once
and forgotten, because the numbers it bakes -- the metre scale, which way the
gun points, where its origin sits -- are the ones the viewmodel is tuned
against, and changing one has to be repeatable.

Most of what it used to do it no longer says: the node transforms, the axes
and scale, the texture folding and the glb writing are in tools/asset_import.py
now, which the katana and the optic call too. What is left here is the three
things that are true of THIS gun and of nothing else:

  * The origin, moved onto the bore line at the back of the receiver, which is
    where the viewmodel offsets in weapon_specs.c are measured from. Found off
    the barrel rather than off the bounding box, because a gun's mesh includes
    a stock and a magazine and the bore is neither.

  * The rear backup sight, which the artist modelled deployed. This gun carries
    an optic whose reticle sits at the height the irons do, so a raised rear
    sight would stand between the eye and the glass -- and at 60mm from the eye
    it is not a thin blade there, it is a wall across the bottom half of the
    sight picture. Every real rifle with a red dot at that height has that
    sight folded flat, so folding it is what this bakes. The front post stays:
    it lands a hair under the dot, which is the co-witness the height was
    chosen for.

  * The rail's own height above the bore, measured and WRITTEN to
    placement.json rather than printed for somebody to copy. That number is the
    gun's half of the sight mount; the optic's half is measured by
    scope_to_glb.py the same way, and weapon_spec_sight_height adds them.

The rig in the file -- six bones for the bolt, trigger, safety and magazine --
is dropped. Nothing animates those yet, and a static prop is what the renderer
wants; the bind pose is what gets baked.

Everything else about the import is stated in models/weapons/m4_carbine/
import.json, beside the asset it describes.

Usage:  python3 tools/fbx_to_glb.py    # needs Pillow, for the textures only
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import asset_import as ai

FOLDER = 'models/weapons/m4_carbine'


def fold_rear_sight(group, cut):
    """The iron sight group with everything behind `cut` on the FBX's X taken
    out -- the rear leaf, leaving the front post. Rebuilt rather than filtered
    in place so the vertices the dropped triangles used go too."""
    kept = {'positions': [], 'normals': [], 'uvs': [], 'indices': []}
    moved = {}
    for i in range(0, len(group['indices']), 3):
        triangle = group['indices'][i:i + 3]
        if sum(group['positions'][v * 3] for v in triangle) / 3.0 < cut:
            continue
        for v in triangle:
            if v not in moved:
                moved[v] = len(kept['positions']) // 3
                kept['positions'] += group['positions'][v * 3:v * 3 + 3]
                kept['normals'] += group['normals'][v * 3:v * 3 + 3]
                kept['uvs'] += group['uvs'][v * 2:v * 2 + 2]
            kept['indices'].append(moved[v])
    return kept


def main():
    manifest = ai.Manifest(FOLDER)
    parts = ai.load_parts(manifest.path('source'))

    # Everything is measured in the FBX's own frame first, so the bounds that
    # decide the scale and the origin are across the whole gun.
    low, high = ai.bounds([g for _, _, g in parts])

    # The rear leaf, folded. The two iron sights sit at opposite ends of the
    # gun, so the midpoint of its length is all it takes to tell them apart and
    # nothing here has to know where either one actually is. Done after the
    # bounds, which are the whole gun's and must not move when a part is
    # dropped: the scale and the muzzle in weapon_specs.c are measured off them.
    parts = [(name, material,
              fold_rear_sight(group, (low[0] + high[0]) * 0.5)
              if name == 'IronSight' else group)
             for name, material, group in parts]

    scale = ai.scale_for(low, high, manifest['length_axis'],
                         manifest['overall_length'])

    # The bore, not the mesh's centre: the barrel is the only part of the gun
    # whose height the viewmodel's numbers care about, and the last stretch of
    # X is barrel and nothing else.
    barrel = [p for _, _, g in parts for p in ai.vertices(g)
              if p[0] > high[0] - (high[0] - low[0]) * 0.05]
    bore_y = (min(p[1] for p in barrel) + max(p[1] for p in barrel)) * 0.5
    bore_z = (min(p[2] for p in barrel) + max(p[2] for p in barrel)) * 0.5
    origin = [0.0, bore_y, bore_z]
    frame = ai.Frame(manifest['axes'], origin, scale)

    primitives = ai.Primitives(manifest['generator'], frame)
    # Every material first, then every primitive - the order the buffer is laid
    # out in, and the order this asset has always been written in.
    material_ids = {}
    for name, spec in manifest['materials'].items():
        png = ai.texture_png(manifest.textures, spec['map'], spec.get('ao'),
                             manifest['max_texture']) if 'map' in spec else None
        material_ids[name] = primitives.material(
            name, spec.get('color', (1.0, 1.0, 1.0, 1.0)), png)
    for name, material, group in parts:
        primitives.add(name, material_ids[material], group)
    primitives.write(manifest.path('output'))

    # What an optic clamps to, so it is the flat top of the receiver and not
    # whatever stands highest on the gun. Taken across the middle of the rail
    # and behind the front sight, which is the stretch a sight can sit on.
    rail = [p for _, _, g in parts for p in ai.vertices(g)
            if abs(p[2] - bore_z) < 0.5 and p[0] < (low[0] + high[0]) * 0.5]

    print('scale %.6f m per unit, %.3f m overall'
          % (scale, manifest['overall_length']))
    ai.write_measured(FOLDER, {
        'muzzle_forward': -frame.place(high[0], bore_y, bore_z)[2],
        'muzzle_up': frame.place(high[0], bore_y, bore_z)[1],
        'rail_height': (max(p[1] for p in rail) - bore_y) * scale,
        'butt_back': -frame.along(0, low[0]),
        'lowest_point': (low[1] - bore_y) * scale,
        'half_width': (high[2] - low[2]) * 0.5 * scale,
    })


if __name__ == '__main__':
    main()
