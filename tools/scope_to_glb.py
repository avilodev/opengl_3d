#!/usr/bin/env python3
"""Converts the EOTech EXPS2 FBX into the binary glTF the engine loads.

The companion to fbx_to_glb.py, and it bakes the same three things -- node
transforms, axes and scale, and an origin -- for the same reason: so the
renderer can draw the optic with one translation and no correction of its own.

Where it differs is the origin. A gun is measured from its bore, because that
is what a muzzle flash and a tracer hang off. An optic is measured from its
RETICLE: the point the shooter's eye has to end up behind. Baking the origin
there means the two numbers in weapon_specs.c that mount the sight --
sight_height and sight_forward -- are literally where the aim point is, and
aiming down the sight is one translation by exactly those. There is no second
place where "how far up is the dot" is written down, so there is nothing to
fall out of step.

What else it bakes, and why here rather than in C:

  * One primitive out of eight materials. The artist supplied a single 2048px
    atlas for the whole sight and nine materials that all index into it, so
    nine primitives would be nine decodes of the same image and nine copies of
    it in video memory. Merged, it is one texture and one draw.

  * Ambient occlusion, multiplied into the base colour, exactly as the rifle
    does it and for the same reason: one lamp and no shadowing reads as a
    plastic toy without it.

  * The window, kept as its own translucent primitive with no map. It is the
    one surface here that must not be opaque -- the whole point of a
    holographic sight is that the target stays visible through it -- and the
    atlas has no glass on it to sample anyway.

The reticle itself is not in this file. It is drawn by the HUD, at the centre
of the screen, because that is the only place it can be and stay honest: the
dot has to sit exactly where the bullet goes, and the bullet leaves the eye.

Usage:  python3 tools/scope_to_glb.py
"""
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import fbx
import fbx_mesh
import glb_write

SOURCE = 'models/Scopes/Sight_close/EOTECH+HWS+EXPS2.fbx'
TEXTURE_DIR = 'models/Scopes/Sight_close/Textures'
OUTPUT = 'models/Scopes/Sight_close/sight_close.glb'

# Front of the hood to the back of the body, in metres. An EOTech EXPS2 is
# 3.8 inches, and every other dimension of the model is in proportion to that,
# so one measured length sets the scale for all of them. The height that comes
# out of it -- 72mm against a published 73.7 -- is the check that it is right.
OVERALL_LENGTH = 0.0965

# The one material that is not part of the shell. Everything else is merged.
GLASS = 'holo_glass'
# Anti-reflective coating on the combiner, which is what makes a holographic
# window read as glass rather than as a hole. Low alpha on purpose: this is a
# tint over the world, not a filter in front of it.
GLASS_TINT = (0.42, 0.60, 0.62, 0.16)

BASE_COLOUR = 'Base Color.png'
OCCLUSION = 'Ambient Occlusion.png'

# How much of the occlusion map to fold in.
#
# The rifle's importer folds its AO in whole. This one cannot: the map that
# ships here averages 79 out of 255, and multiplying that in crushes the flats
# along with the crevices. Half strength keeps the crevices and stops the panels
# going with them. A fixed-function pipeline has one lamp, no exposure and no
# tone mapping, so whatever arrives is what is seen.
AO_STRENGTH = 0.5

# What the finished map has to average across the surface that samples it.
#
# This is measured the way it has to be measured: area-weighted, at the texels
# the mesh actually reads. Averaging the whole image instead is what fooled me
# the first time -- half of this atlas is bare canvas between the islands, so
# the image mean says 16 while the surface that reads it says something quite
# different, and correcting the wrong number left the sight two thirds too dark
# even where its map was right. 37.7 is the same measurement taken on the
# rifle, so matching it makes the two halves of the gun one object.
TARGET_ALBEDO = 37.7

# Bare canvas, and how much of it makes a material unmapped.
#
# The artist's FBX names a separate base colour map per material and only one
# set shipped. Six of the nine materials happen to index into it correctly;
# three -- main_color, main_color_metalic_litt and plastic -- have UVs that
# land in the empty space between islands. They are not black parts, they are
# unmapped parts, and sampling bare canvas is what turned the body of the sight
# into a hole in the rifle. They get a flat colour, exactly as the rifle's iron
# sights do and for exactly the same reason.
#
# Detected rather than listed by name, so a fuller texture set dropped into
# Textures/ later simply stops triggering it.
UNMAPPED_TEXEL = 2
UNMAPPED_SHARE = 0.5
# Cool grey, scaled so it averages TARGET_ALBEDO and sits beside the mapped
# parts rather than in front of them.
FLAT_TINT = (0.94, 0.97, 1.09)

# Same bargain as the rifle's: past this the extra detail is not resolvable on
# something that fills a fraction of the screen, and the file is four times
# the size.
MAX_TEXTURE = 1024


def load_mesh(path):
    """(material name, triangle group) for every material in the one mesh."""
    root, _ = fbx.load(path)
    objects, _ = fbx.index_objects(root)
    children, _ = fbx.connections(root)
    for oid, node in objects.items():
        if node.name != 'Model' or node.props[2] != 'Mesh':
            continue
        linked = [objects[k] for k in children.get(oid, [])]
        geometry = next(o for o in linked if o.name == 'Geometry')
        names = [fbx._clean(o.props[1]) for o in linked if o.name == 'Material']
        linear, translation = fbx_mesh.node_matrix(node)
        groups = fbx_mesh.extract(geometry, linear, translation)
        return [(names[i] if i < len(names) else names[0], g)
                for i, g in sorted(groups.items())]
    raise ValueError('%s holds no mesh' % path)


def merge(groups):
    """Several triangle groups as one, with the indices rebased."""
    out = {'positions': [], 'normals': [], 'uvs': [], 'indices': []}
    for group in groups:
        base = len(out['positions']) // 3
        for key in ('positions', 'normals', 'uvs'):
            out[key] += group[key]
        out['indices'] += [i + base for i in group['indices']]
    return out


def vertices(group):
    p = group['positions']
    return [p[i:i + 3] for i in range(0, len(p), 3)]


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
    parts = load_mesh(SOURCE)
    glass = merge([g for name, g in parts if name == GLASS])
    if not glass['indices']:
        raise ValueError('%s has no %s material; the window is what the '
                         'origin is measured from' % (SOURCE, GLASS))

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
                         'mesh do not belong together' % (SOURCE, BASE_COLOUR))

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

    def place(x, y, z):
        """FBX (back, up, left) to viewmodel (right, up, back), in metres."""
        return (-(z - origin[2]) * scale,
                (y - origin[1]) * scale,
                (x - origin[0]) * scale)

    glb = glb_write.Glb('tools/scope_to_glb.py')
    shell_material = glb.material('shell', (1.0, 1.0, 1.0, 1.0),
                                  glb.image(texture_png(mapped)))
    plate_material = glb.material('plate', shaded_colour())
    glass_material = glb.material('glass', GLASS_TINT)

    triangles = 0
    for name, group, material in (('shell', shell, shell_material),
                                  ('plate', plate, plate_material),
                                  ('glass', glass, glass_material)):
        if not group['indices']:
            continue
        positions, normals = [], []
        source_p, source_n = group['positions'], group['normals']
        for i in range(0, len(source_p), 3):
            positions += place(*source_p[i:i + 3])
            # The mapping is a rotation, so normals go through it unscaled.
            nx, ny, nz = source_n[i:i + 3]
            normals += [-nz, ny, nx]
        glb.primitive(positions, normals, group['uvs'], group['indices'],
                      material)
        triangles += len(group['indices']) // 3
        print('  %-6s %6d vertices %6d triangles'
              % (name, len(positions) // 3, len(group['indices']) // 3))

    size = glb.write(OUTPUT)

    # The landmarks weapon_specs.c is set from. Printed rather than written
    # anywhere, because a spec is tuning and wants a person to approve it.
    seat_x = [v[0] for g in everything for v in vertices(g)
              if abs(v[1] - seat) < 0.25]
    print('\n%s: %d triangles, %.1f KB' % (OUTPUT, triangles, size / 1024.0))
    print('scale %.6f m per unit, %.4f m long, %.4f m tall, %.4f m wide'
          % (scale, OVERALL_LENGTH, (high[1] - low[1]) * scale,
             (high[2] - low[2]) * scale))
    print('landmarks in viewmodel space (+x right, +y up, -z forward),')
    print('measured from the reticle, which is the origin:')
    print('  reticle above the rail seat  %+.4f  (add the rail\'s own height '
          'above the bore for sight_height)' % ((origin[1] - seat) * scale))
    print('  window                       %.4f m wide, %.4f m tall'
          % ((max(v[2] for v in pane) - min(v[2] for v in pane)) * scale,
             (max(v[1] for v in pane) - min(v[1] for v in pane)) * scale))
    print('  front of the hood          z %+.4f' % ((low[0] - origin[0]) * scale))
    print('  back of the body           z %+.4f' % ((high[0] - origin[0]) * scale))
    print('  rail seat                  z %+.4f to %+.4f  (must land on rail)'
          % ((min(seat_x) - origin[0]) * scale,
             (max(seat_x) - origin[0]) * scale))
    print('  lowest point               y %+.4f  (clamp jaw, below the seat)'
          % ((low[1] - origin[1]) * scale))


if __name__ == '__main__':
    main()
