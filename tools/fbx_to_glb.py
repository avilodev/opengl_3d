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
    where the viewmodel offsets in src/gameplay/weapons/ are measured from. Found
off
    the barrel rather than off the bounding box, because a gun's mesh includes
    a stock and a magazine and the bore is neither.

  * The irons, dropped. This used to fold the rear leaf down and keep the front
    post, on the argument that a post landing a hair under the dot is the
    co-witness the mount height was chosen for. That argument is gone: the gun
    wears whichever of four optics the player fits, their reticles sit at four
    different heights, and a post that co-witnesses one of them clips through
    the rest. What is drawn is the gun; the sight picture belongs entirely to
    the optic. A material the manifest names is kept and one it does not is
    dropped, which is the rule cz_to_glb.py already worked to.

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


def main():
    manifest = ai.Manifest(FOLDER)
    parts = ai.load_parts(manifest.path('source'))

    # Everything is measured in the FBX's own frame first, so the bounds that
    # decide the scale and the origin are across the whole gun.
    low, high = ai.bounds([g for _, _, g in parts])

    # And only now is anything dropped. A part whose material this manifest
    # does not name is not part of the gun - the irons, here - and it goes
    # after the bounds rather than before, because the bounds are the whole
    # gun's and must not move when furniture comes off: the scale and the
    # muzzle in the gun's spec are measured from them.
    wanted = manifest['materials']
    parts = [(name, material, group) for name, material, group in parts
             if material in wanted]
    missing = set(wanted) - {m for _, m, _ in parts}
    if missing:
        raise ValueError('%s names materials the mesh does not have: %s'
                         % (os.path.join(FOLDER, 'import.json'),
                            ', '.join(sorted(missing))))

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
    for name, spec in wanted.items():
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
