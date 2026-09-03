#!/usr/bin/env python3
"""Converts the Japanese Katana FBX into the binary glTF the engine loads.

Third of its kind, after the rifle and the optic, and it bakes the same three
things for the same reason: node transforms, axes and scale, and an origin, so
the renderer draws the sword with one translation and no correction of its own.

Where it differs from the two before it:

  * The node has a scaling pivot. Maya froze the transform and left the pivot
    36 units up the blade, so the eighteen-times scale on this node is applied
    about a point rather than about the origin. fbx_mesh.node_matrix used to
    refuse a pivot instead of ignoring it, which is what turned this asset into
    the reason it now handles the whole chain.

  * Seven materials, seven base colour maps, and no atlas. Nothing merges here:
    each material keeps its own primitive and its own texture, because that is
    also what makes the parts list mean something -- the blade, the guard and
    the scabbard can each be hidden or drawn on their own.

  * No ambient occlusion map ships with it. The rifle and the optic both fold
    one in to stop a single-lamp pipeline reading them as plastic; this set has
    height, normal, roughness and metallic and no AO, so there is nothing to
    fold and the base colour goes in as it is.

The origin sits at the middle of the tsuka -- the wrapped grip -- on the
sword's own axis, because that is the point a hand closes around and so the
point every viewmodel offset wants to be measured from. A gun is measured from
its bore because that is what a bullet hangs off; a sword has no bore, and what
it hangs off is the hand.

Usage:  python3 tools/katana_to_glb.py
"""
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import fbx
import fbx_mesh
import glb_write

SOURCE = 'models/Swords/Katana/Japanese Katana.fbx'
TEXTURE_DIR = 'models/Swords/Katana/Japanese Katana Texture'
OUTPUT = 'models/Swords/Katana/katana.glb'

# The scabbard leaves in a file of its own. It is a separate object in every
# sense that matters at draw time -- worn on the hip while the sword is in the
# hand, and left behind on the hip while the sword is swung -- and two meshes
# with two origins is the only shape in which those two things can be posed
# independently. Splitting it here rather than hiding it at draw time also
# means the sword's own origin and bounds stop counting a part that is not
# attached to it.
SAYA_OUTPUT = 'models/Swords/Katana/katana_saya.glb'

# Butt of the grip to the tip of the blade, in metres. A katana runs about a
# metre over all -- a 70cm nagasa on a 26cm tsuka -- and every other dimension
# of the model is in proportion to that, so one measured length sets the scale
# for all of them. The blade that comes out of it is 0.79m, which is long for
# the real thing and about right for whatever this one is.
OVERALL_LENGTH = 1.02

# The wrapped grip. Its bounding box is the origin, and its length is the check
# that the scale is sane, so it is named rather than guessed at.
GRIP = 'BladesPart5'

# The scabbard: modelled as part of the same mesh and parked alongside the
# sword rather than around it, which is why it is split into SAYA_OUTPUT with
# an origin of its own rather than drawn where the artist left it.
SCABBARD = 'KatanaCover'

# The artist's naming, which is regular enough to derive rather than tabulate.
TEXTURE = 'scifi katana_%s_BaseColor.png'

# White, so the map is what is seen. A material whose map is missing keeps this
# and draws flat rather than drawing black.
UNTEXTURED = (0.72, 0.72, 0.74, 1.0)

# Same bargain as the rifle's: past this the extra detail is not resolvable on
# something that fills a fraction of the screen, and seven maps at full size
# would be twenty megabytes of glb.
MAX_TEXTURE = 1024


def load_mesh(path):
    """[(material name, triangle group)] for every material in the one mesh."""
    root, _ = fbx.load(path)
    objects, _ = fbx.index_objects(root)
    children, _ = fbx.connections(root)
    for oid, node in objects.items():
        if node.name != 'Model' or node.props[2] != 'Mesh':
            continue
        linked = [objects[k] for k in children.get(oid, [])]
        geometry = next(o for o in linked if o.name == 'Geometry')
        # An FBX material index is an offset into this model's own list, in
        # the order the connections arrive.
        names = [fbx._clean(o.props[1]) for o in linked if o.name == 'Material']
        linear, translation = fbx_mesh.node_matrix(node)
        groups = fbx_mesh.extract(geometry, linear, translation)
        return [(names[i] if i < len(names) else names[0], g)
                for i, g in sorted(groups.items())]
    raise ValueError('%s holds no mesh' % path)


def bounds(groups):
    """(low, high) corners of the bounding box over several triangle groups."""
    low = [min(min(g['positions'][c::3]) for g in groups) for c in range(3)]
    high = [max(max(g['positions'][c::3]) for g in groups) for c in range(3)]
    return low, high


def texture_png(material):
    """The material's base colour as PNG bytes, or None if it has no map."""
    from PIL import Image
    path = os.path.join(TEXTURE_DIR, TEXTURE % material)
    if not os.path.exists(path):
        return None
    image = Image.open(path).convert('RGB')
    if max(image.size) > MAX_TEXTURE:
        scale = MAX_TEXTURE / max(image.size)
        image = image.resize((round(image.width * scale),
                              round(image.height * scale)), Image.LANCZOS)
    buffer = io.BytesIO()
    image.save(buffer, 'PNG', optimize=True)
    return buffer.getvalue()


def write_parts(parts, origin, scale, output):
    """Bakes some of the materials into one glb about a stated origin.

    Two files come out of this asset and they differ only in which materials
    go in and where the origin sits, so the baking is written once and called
    twice rather than duplicated with one line changed."""

    def place(x, y, z):
        """FBX (right, up, tipward) to viewmodel (right, up, back), in metres.

        A half turn about the vertical: the blade points down -Z, which is
        where the camera looks, and the grip ends up nearest the eye."""
        return (-(x - origin[0]) * scale,
                (y - origin[1]) * scale,
                -(z - origin[2]) * scale)

    glb = glb_write.Glb('tools/katana_to_glb.py')
    triangles = 0
    for name, group in parts:
        png = texture_png(name)
        material = glb.material(name, (1.0, 1.0, 1.0, 1.0) if png
                                else UNTEXTURED,
                                glb.image(png) if png else None)
        positions, normals = [], []
        source_p, source_n = group['positions'], group['normals']
        for i in range(0, len(source_p), 3):
            positions += place(*source_p[i:i + 3])
            # The mapping is a rotation, so normals go through it unscaled.
            nx, ny, nz = source_n[i:i + 3]
            normals += [-nx, ny, -nz]
        glb.primitive(positions, normals, group['uvs'], group['indices'],
                      material)
        triangles += len(group['indices']) // 3
        print('  %-14s %6d vertices %6d triangles  %s'
              % (name, len(positions) // 3, len(group['indices']) // 3,
                 '%d KB map' % (len(png) / 1024.0) if png else 'no map'))

    size = glb.write(output)
    print('%s: %d triangles across %d materials, %.1f KB\n'
          % (output, triangles, len(parts), size / 1024.0))


def main():
    parts = load_mesh(SOURCE)
    names = [name for name, _ in parts]
    if GRIP not in names:
        raise ValueError('%s has no %s material; the grip is what the origin '
                         'is measured from' % (SOURCE, GRIP))
    if SCABBARD not in names:
        raise ValueError('%s has no %s material; the scabbard is a file of its '
                         'own and cannot be split out of nothing'
                         % (SOURCE, SCABBARD))

    sword_parts = [(n, g) for n, g in parts if n != SCABBARD]
    saya_parts = [(n, g) for n, g in parts if n == SCABBARD]

    # The scale is set by the sword, not by the bounding box: the scabbard
    # reaches a centimetre past the tip and measuring across both would make
    # every part of the sword that much too small. Both files take the same
    # scale, because a scabbard measured in its own units would not fit the
    # sword that goes into it.
    low, high = bounds([g for _, g in sword_parts])
    scale = OVERALL_LENGTH / (high[2] - low[2])

    grip_low, grip_high = bounds([g for n, g in parts if n == GRIP])
    origin = [(grip_low[c] + grip_high[c]) * 0.5 for c in range(3)]

    # The mouth of the scabbard, which is where the blade enters and leaves.
    # The origin goes here for the reason the optic's goes on its reticle: it
    # is the one point the draw, the sheathed sword and this mesh all have to
    # agree on, and an origin anywhere else means each of them derives it
    # separately. The mouth is the tsuka end, which is -Z in FBX before the
    # half turn, so the low corner.
    cover_low, cover_high = bounds([g for _, g in saya_parts])
    koiguchi = [(cover_low[0] + cover_high[0]) * 0.5,
                (cover_low[1] + cover_high[1]) * 0.5,
                cover_low[2]]

    write_parts(sword_parts, origin, scale, OUTPUT)
    write_parts(saya_parts, koiguchi, scale, SAYA_OUTPUT)

    # The landmarks a viewmodel spec would be set from. Printed rather than
    # written anywhere, because a spec is tuning and wants a person to approve
    # it.
    all_low, all_high = bounds([g for _, g in sword_parts])
    print('scale %.6f m per unit, %.4f m long, %.4f m tall, %.4f m wide'
          % (scale, OVERALL_LENGTH, (all_high[1] - all_low[1]) * scale,
             (all_high[0] - all_low[0]) * scale))
    print('landmarks in viewmodel space (+x right, +y up, -z forward),')
    print('measured from the middle of the grip, which is the sword origin:')
    print('  tip of the blade           z %+.4f'
          % (-(high[2] - origin[2]) * scale))
    print('  butt of the grip           z %+.4f'
          % (-(low[2] - origin[2]) * scale))
    print('  grip                       %.4f m long'
          % ((grip_high[2] - grip_low[2]) * scale))
    print('  guard, forward of the grip z %+.4f'
          % (-(grip_high[2] - origin[2]) * scale))
    # Where the mouth of the scabbard falls if the sword is left sheathed in
    # the pose the artist modelled. Task 9 hangs the saya off the hip instead,
    # but this is the depth the blade has to travel to clear it.
    print('  koiguchi, in sword space   %+.4f %+.4f %+.4f'
          % (-(koiguchi[0] - origin[0]) * scale,
             (koiguchi[1] - origin[1]) * scale,
             -(koiguchi[2] - origin[2]) * scale))
    print('  saya                       %.4f m deep'
          % ((cover_high[2] - cover_low[2]) * scale))


if __name__ == '__main__':
    main()
