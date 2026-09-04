#!/usr/bin/env python3
"""Converts the Japanese Katana FBX into the binary glTF the engine loads.

What every import does - node transforms including the scaling pivot this
node has, the axes and scale, the texture folding, the glb write - is in
tools/asset_import.py. What is here is the two things that are true of this
asset and of nothing else:

  * The origin sits at the middle of the tsuka, the wrapped grip, on the
    sword's own axis, because that is the point a hand closes around and so
    the point every viewmodel offset wants to be measured from. A gun is
    measured from its bore because that is what a bullet hangs off; a sword
    has no bore, and what it hangs off is the hand.

  * The scabbard comes out as a second file with an origin of its own, at the
    koiguchi - the mouth, where the blade enters and leaves. It is modelled
    as part of the same mesh and parked alongside the sword rather than
    around it. Two meshes with two origins is the only shape in which a
    scabbard worn on the hip and a sword held in the hand can be posed
    independently.

Seven materials, seven base colour maps, and no atlas: nothing merges here,
because that is also what makes the parts list mean something - the blade, the
guard and the scabbard can each be hidden or drawn on their own. No ambient
occlusion map ships with this set, so unlike the rifle and the optic there is
nothing to fold in and the base colour goes in as it is.

Everything else is stated in models/weapons/katana/import.json.

Usage:  python3 tools/katana_to_glb.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import asset_import as ai

FOLDER = 'models/weapons/katana'


def build(manifest, parts, frame, output):
    """One glb out of some of the materials, about the frame's own origin.

    Two files come out of this asset and they differ only in which materials
    go in and where the origin sits, so the baking is written once and called
    twice rather than duplicated with one line changed."""
    primitives = ai.Primitives(manifest['generator'], frame)
    for name, group in parts:
        png = ai.texture_png(manifest.textures,
                             manifest['texture_pattern'] % name,
                             max_size=manifest['max_texture'])
        material = primitives.material(
            name, (1.0, 1.0, 1.0, 1.0) if png else manifest['untextured'], png)
        primitives.add(name, material, group)
    primitives.write(output)


def main():
    manifest = ai.Manifest(FOLDER)
    source = manifest.path('source')
    # One mesh, split by material: the material name is the part name here.
    parts = [(material, group)
             for _, material, group in ai.load_parts(source)]
    names = [name for name, _ in parts]

    grip, scabbard = manifest['grip'], manifest['scabbard']
    if grip not in names:
        raise ValueError('%s has no %s material; the grip is what the origin '
                         'is measured from' % (source, grip))
    if scabbard not in names:
        raise ValueError('%s has no %s material; the scabbard is a file of its '
                         'own and cannot be split out of nothing'
                         % (source, scabbard))

    sword_parts = [(n, g) for n, g in parts if n != scabbard]
    saya_parts = [(n, g) for n, g in parts if n == scabbard]

    # The scale is set by the sword, not by the bounding box: the scabbard
    # reaches a centimetre past the tip and measuring across both would make
    # every part of the sword that much too small. Both files take the same
    # scale, because a scabbard measured in its own units would not fit the
    # sword that goes into it.
    low, high = ai.bounds([g for _, g in sword_parts])
    scale = ai.scale_for(low, high, manifest['length_axis'],
                         manifest['overall_length'])

    grip_low, grip_high = ai.bounds([g for n, g in parts if n == grip])
    origin = [(grip_low[c] + grip_high[c]) * 0.5 for c in range(3)]

    # The mouth of the scabbard, where the blade enters and leaves. The origin
    # goes here for the reason the optic's goes on its reticle: it is the one
    # point the draw, the sheathed sword and this mesh all have to agree on,
    # and an origin anywhere else means each of them derives it separately.
    # The mouth is the tsuka end, which is -Z in FBX before the half turn, so
    # the low corner.
    cover_low, cover_high = ai.bounds([g for _, g in saya_parts])
    koiguchi = [(cover_low[0] + cover_high[0]) * 0.5,
                (cover_low[1] + cover_high[1]) * 0.5,
                cover_low[2]]

    sword_frame = ai.Frame(manifest['axes'], origin, scale)
    saya_frame = ai.Frame(manifest['axes'], koiguchi, scale)

    build(manifest, sword_parts, sword_frame, manifest.path('output'))
    build(manifest, saya_parts, saya_frame, manifest.path('scabbard_output'))

    print('scale %.6f m per unit, %.4f m long, %.4f m tall, %.4f m wide'
          % (scale, manifest['overall_length'],
             (high[1] - low[1]) * scale, (high[0] - low[0]) * scale))
    ai.write_measured(FOLDER, {
        'blade_tip_forward': sword_frame.along(2, high[2]),
        'grip_butt_back': -sword_frame.along(2, low[2]),
        'grip_length': (grip_high[2] - grip_low[2]) * scale,
        'guard_forward': sword_frame.along(2, grip_high[2]),
        # What sheath_probe used to carry as `#define SAYA_DEPTH 0.7907f`,
        # transcribed by hand from a printed line. It measures the scabbard
        # mesh itself now and this agrees with it; both beat a constant.
        'scabbard_depth': (cover_high[2] - cover_low[2]) * scale,
        'koiguchi_in_sword_space': [
            -sword_frame.along(0, koiguchi[0]),
            sword_frame.along(1, koiguchi[1]),
            -sword_frame.along(2, koiguchi[2]),
        ],
    })


if __name__ == '__main__':
    main()
