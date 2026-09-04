#!/usr/bin/env python3
"""Converts the EOTech EXPS2 FBX into the binary glTF the engine loads.

What every import does - node transforms, axes and scale, the glb write - is
in tools/asset_import.py, which the rifle and the katana call too. What is
here is what is true of an optic and of nothing else:

  * The origin sits on the RETICLE: the point the shooter's eye has to end up
    behind. A gun is measured from its bore, because that is what a muzzle
    flash and a tracer hang off; an optic is measured from the dot. Baking the
    origin there means the two numbers that mount the sight are literally
    where the aim point is, and aiming down the sight is one translation by
    exactly those.

  * The rail seat, and so the reticle's height above it. That is the optic's
    half of the mount height; the gun's half is measured by fbx_to_glb.py the
    same way, and weapon_spec_sight_height adds them. It used to be PRINTED
    for somebody to copy into sights.c. It is written to placement.json now,
    which is the difference between a measurement and a transcription.

  * One primitive out of eight materials. The artist supplied a single 2048px
    atlas and nine materials that all index into it, so nine primitives would
    be nine decodes of the same image and nine copies of it in video memory.
    Merged, it is one texture and one draw. The materials that turn out not to
    land on an island at all are drawn flat instead of sampling bare canvas.

  * The gain. Ambient occlusion folded in at part strength, then the whole map
    lifted until the surface that reads it averages what the rifle's does -
    measured over the triangles that actually sample it, not over the image.

  * The window, kept as its own translucent primitive with no map.

The reticle itself is not in this file. It is drawn by the HUD, at the centre
of the screen, because that is the only place it can be and stay honest: the
dot has to sit exactly where the bullet goes, and the bullet leaves the eye.

Everything else is stated in models/attachments/sight_close/import.json.

Usage:  python3 tools/scope_to_glb.py
"""
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import asset_import as ai

FOLDER = 'models/attachments/sight_close'
MANIFEST = ai.Manifest(FOLDER)

TEXTURE_DIR = MANIFEST.textures
OUTPUT = MANIFEST.path('output')
OVERALL_LENGTH = MANIFEST['overall_length']
GLASS = MANIFEST['glass']
GLASS_TINT = tuple(MANIFEST['glass_tint'])
BASE_COLOUR = MANIFEST['base_colour']
OCCLUSION = MANIFEST['occlusion']
AO_STRENGTH = MANIFEST['ao_strength']
TARGET_ALBEDO = MANIFEST['target_albedo']
UNMAPPED_TEXEL = MANIFEST['unmapped_texel']
UNMAPPED_SHARE = MANIFEST['unmapped_share']
FLAT_TINT = tuple(MANIFEST['flat_tint'])
MAX_TEXTURE = MANIFEST['max_texture']

# The shared ones, under the names this file already used.
merge = ai.merge
vertices = ai.vertices


def rail_seat(groups):
    """Y of the flat the mount rests on when it is clamped to a rail.

    Not the model's lowest point: the clamp jaw wraps around the sides of the
    rail and hangs several millimetres below its top face, so the bounding box
    would put the sight that much too low. The seat is the largest horizontal
    downward-facing surface in the bottom half, which on this mesh is the only
    thing it could be.
    """
    area = {}
    for group in groups:
        points, normals = vertices(group), group['normals']
        for i in range(0, len(group['indices']), 3):
            a, b, c = group['indices'][i:i + 3]
            if normals[a * 3 + 1] > -0.9:
                continue
            triangle = [points[a], points[b], points[c]]
            height = sum(v[1] for v in triangle) / 3.0
            u = [triangle[1][j] - triangle[0][j] for j in range(3)]
            v = [triangle[2][j] - triangle[0][j] for j in range(3)]
            cross = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                     u[0] * v[1] - u[1] * v[0])
            size = 0.5 * sum(c * c for c in cross) ** 0.5
            area[round(height, 1)] = area.get(round(height, 1), 0.0) + size
    low = min(v[1] for g in groups for v in vertices(g))
    high = max(v[1] for g in groups for v in vertices(g))
    candidates = {y: a for y, a in area.items() if y < (low + high) * 0.5}
    return max(candidates, key=candidates.get)


def samples(group, pixels, width, height):
    """(triangle area, the texel it reads) for every triangle in the group.

    Sampled at the centroid. A triangle covers many texels, but what is being
    asked here is which island a triangle sits on and roughly how bright that
    island is, and for both of those one sample in the middle is the answer."""
    P, UV, I = group['positions'], group['uvs'], group['indices']
    for i in range(0, len(I), 3):
        a, b, c = I[i], I[i + 1], I[i + 2]
        va, vb, vc = P[a*3:a*3+3], P[b*3:b*3+3], P[c*3:c*3+3]
        e1 = [vb[k] - va[k] for k in range(3)]
        e2 = [vc[k] - va[k] for k in range(3)]
        cross = (e1[1]*e2[2] - e1[2]*e2[1], e1[2]*e2[0] - e1[0]*e2[2],
                 e1[0]*e2[1] - e1[1]*e2[0])
        area = 0.5 * sum(v * v for v in cross) ** 0.5
        u = (UV[a*2] + UV[b*2] + UV[c*2]) / 3.0
        v = (UV[a*2+1] + UV[b*2+1] + UV[c*2+1]) / 3.0
        x = min(width - 1, max(0, int(u % 1.0 * (width - 1))))
        y = min(height - 1, max(0, int(v % 1.0 * (height - 1))))
        yield area, pixels[x, y]


def bare_share(group, pixels, width, height):
    """How much of a group's area reads bare canvas rather than an island."""
    total = bare = 0.0
    for area, texel in samples(group, pixels, width, height):
        total += area
        if texel <= UNMAPPED_TEXEL:
            bare += area
    return bare / total if total else 1.0


def shaded_colour():
    """The flat colour for the materials with no island to read."""
    scale = TARGET_ALBEDO / 255.0 / (sum(FLAT_TINT) / 3.0)
    return tuple(min(1.0, c * scale) for c in FLAT_TINT) + (1.0,)


def texture_png(mapped):
    """The shell's base colour as PNG bytes: the AO map folded in at part
    strength, then lifted until the surface that reads it averages what the
    rifle's does."""
    from PIL import Image, ImageChops
    image = Image.open(os.path.join(TEXTURE_DIR, BASE_COLOUR)).convert('RGB')
    occlusion = Image.open(os.path.join(TEXTURE_DIR, OCCLUSION)).convert('L')
    if occlusion.size != image.size:
        occlusion = occlusion.resize(image.size, Image.LANCZOS)
    # Pulled toward white by however much strength is left off, so the crevices
    # still darken and the flats stop being crushed with them.
    occlusion = occlusion.point(
        lambda v: int(round(255.0 - AO_STRENGTH * (255.0 - v))))
    image = ImageChops.multiply(image, Image.merge('RGB', (occlusion,) * 3))

    # The mean over the surface, not over the image: only the triangles that
    # landed on an island get a vote, because they are the only ones this map
    # will ever be read through.
    grey = image.convert('L')
    pixels, (width, height) = grey.load(), grey.size
    total = weighted = 0.0
    for _, group in mapped:
        for area, texel in samples(group, pixels, width, height):
            total += area
            weighted += area * texel
    mean = weighted / total if total else 1.0
    gain = TARGET_ALBEDO / mean if mean > 0.5 else 1.0
    image = image.point(lambda v: min(255, int(round(v * gain))))
    print('  mapped surface averages %.1f -> %.1f  (occlusion %.0f%%, gain %.2fx)'
          % (mean, mean * gain, AO_STRENGTH * 100.0, gain))

    if max(image.size) > MAX_TEXTURE:
        scale = MAX_TEXTURE / max(image.size)
        image = image.resize((round(image.width * scale),
                              round(image.height * scale)), Image.LANCZOS)
    buffer = io.BytesIO()
    image.save(buffer, 'PNG', optimize=True)
    return buffer.getvalue()


def main():
    from PIL import Image
    parts = [(material, group)
             for _, material, group in ai.load_parts(MANIFEST.path('source'))]
    glass = merge([g for name, g in parts if name == GLASS])
    if not glass['indices']:
        raise ValueError('%s has no %s material; the window is what the '
                         'origin is measured from' % (MANIFEST.path('source'), GLASS))

    # Which materials actually have an island under them, measured against the
    # atlas as it ships rather than after the occlusion and the gain, so the
    # test is about where the UVs point and not about how dark anything is.
    canvas = Image.open(os.path.join(TEXTURE_DIR, BASE_COLOUR)).convert('L')
    pixels, (width, height) = canvas.load(), canvas.size
    mapped, unmapped = [], []
    for name, group in parts:
        if name == GLASS:
            continue
        share = bare_share(group, pixels, width, height)
        (unmapped if share > UNMAPPED_SHARE else mapped).append((name, group))
        print('  %-24s %5.1f%% of its area reads bare canvas%s'
              % (name, share * 100.0, '   <- flat shaded'
                 if share > UNMAPPED_SHARE else ''))
    if not mapped:
        raise ValueError('no material on %s indexes into %s; the atlas and the '
                         'mesh do not belong together' % (MANIFEST.path('source'), BASE_COLOUR))

    shell = merge([g for _, g in mapped])
    plate = merge([g for _, g in unmapped])
    everything = [g for g in (shell, plate, glass) if g['indices']]
    low = [min(v[c] for g in everything for v in vertices(g)) for c in range(3)]
    high = [max(v[c] for g in everything for v in vertices(g)) for c in range(3)]
    scale = OVERALL_LENGTH / (high[0] - low[0])
    seat = rail_seat(everything)

    # The reticle: the centre of the window, on the window's own plane. The
    # dot of a holographic sight is projected onto that glass and centred in
    # it, so this is the point the eye lines up with and the point every
    # number in weapon_specs.c is measured from.
    pane = vertices(glass)
    origin = [sum(v[0] for v in pane) / len(pane),
              (min(v[1] for v in pane) + max(v[1] for v in pane)) * 0.5,
              (min(v[2] for v in pane) + max(v[2] for v in pane)) * 0.5]

    frame = ai.Frame(MANIFEST['axes'], origin, scale)

    # Every material first, then every primitive - the order the buffer is
    # laid out in, and the order this asset has always been written in.
    primitives = ai.Primitives(MANIFEST['generator'], frame)
    shell_material = primitives.material('shell', (1.0, 1.0, 1.0, 1.0),
                                         texture_png(mapped))
    plate_material = primitives.material('plate', shaded_colour())
    glass_material = primitives.material('glass', GLASS_TINT)

    for label, group, material in (('shell', shell, shell_material),
                                   ('plate', plate, plate_material),
                                   ('glass', glass, glass_material)):
        primitives.add(label, material, group)
    primitives.write(OUTPUT)

    # What this import MEASURED, written where the code can read it. The
    # reticle height above the seat is the optic's half of the mount; it used
    # to be printed for somebody to copy into sights.c.
    seat_x = [v[0] for g in everything for v in vertices(g)
              if abs(v[1] - seat) < 0.25]
    pane_z = [v[2] for v in pane]
    pane_y = [v[1] for v in pane]
    print('scale %.6f m per unit, %.4f m long, %.4f m tall, %.4f m wide'
          % (scale, OVERALL_LENGTH, (high[1] - low[1]) * scale,
             (high[2] - low[2]) * scale))
    ai.write_placement(FOLDER, {
        'reticle_height': (origin[1] - seat) * scale,
        'window_width': (max(pane_z) - min(pane_z)) * scale,
        'window_height': (max(pane_y) - min(pane_y)) * scale,
        'hood_front': frame.along(0, low[0]),
        'body_back': frame.along(0, high[0]),
        'rail_seat_from': frame.along(0, min(seat_x)),
        'rail_seat_to': frame.along(0, max(seat_x)),
        'lowest_point': (low[1] - origin[1]) * scale,
    })


if __name__ == '__main__':
    main()
