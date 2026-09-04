#!/usr/bin/env python3
"""Converts the M4 carbine FBX into the binary glTF the engine loads.

The engine reads .glb and nothing else, and no FBX importer is part of the
toolchain, so this is the import step. It is checked in rather than run once
and forgotten, because the numbers it bakes -- the metre scale, which way the
gun points, where its origin sits -- are the ones the viewmodel is tuned
against, and changing one has to be repeatable.

What it bakes, and why here rather than in C:

  * Node transforms. The three mesh nodes hang straight off the scene and
    carry only translation, rotation and scale, so folding them into the
    vertices leaves the runtime with no scene graph to walk.

  * Axes and scale. The FBX is X-forward, Y-up, in the exporter's own units.
    The viewmodel is -Z forward, +Y up, +X right, in metres. Rotating ten
    thousand vertices once at import beats rotating them every frame, and it
    means the numbers in weapon_specs.c describe the model as it is drawn.

  * The origin, moved onto the bore line at the back of the receiver, which
    is where the viewmodel offsets in weapon_specs.c are measured from.

  * Ambient occlusion, multiplied into the base colour. Fixed-function
    lighting has one lamp and no shadowing, so a gun lit by it reads as a
    plastic toy; the AO map the artist supplied is the cheapest way to put
    the crevices back, and folding it in costs nothing at runtime.

The rig in the file -- six bones for the bolt, trigger, safety and magazine --
is dropped. Nothing animates those yet, and a static prop is what the renderer
wants; the bind pose is what gets baked.

So is the rear backup sight, which the artist modelled deployed. This gun
carries an optic whose reticle sits at the height the irons do, so a raised
rear sight would stand between the eye and the glass -- and at 60mm from the
eye it is not a thin blade there, it is a wall across the bottom half of the
sight picture. Every real rifle with a red dot at that height has that sight
folded flat, so folding it is what this bakes. The front post stays: it lands
a hair under the dot, which is the co-witness the height was chosen for.

Usage:  python3 tools/fbx_to_glb.py
"""
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import fbx
import fbx_mesh
import glb_write

SOURCE = 'models/weapons/m4_carbine/source.fbx'
TEXTURE_DIR = 'models/weapons/m4_carbine/textures'
OUTPUT = 'models/weapons/m4_carbine/m4_carbine.glb'

# Muzzle to buttplate, in metres. An M4A1 is 838mm with the stock out, and
# every other dimension of the model is in proportion to that, so one measured
# length sets the scale for all of them.
OVERALL_LENGTH = 0.838

# The base colour each material draws with. The FBX links normal, roughness
# and metallic maps but no diffuse for the receiver, so the pairing is by name
# rather than by connection -- stated here so a missing map is visible rather
# than silently flat. `ao` is multiplied in; `color` is the fallback tint for a
# material with no map on disk at all.
MATERIALS = {
    'M4_Mat':       {'map': 'M4_Colour.png',    'ao': 'M4_AO.png'},
    'M4Mag_Mat':    {'map': 'M4Mag_Colour.png'},
    'Bullet_Mat':   {'map': '556x45_Colour.png'},
    # The artist shipped no IronSight_*.png. Parkerised steel, then.
    'IronSightMat': {'color': (0.20, 0.21, 0.24, 1.0)},
}

# Textures are sampled by a fixed-function pipeline on a gun that fills a
# fraction of the screen. Past this the extra detail is not resolvable and the
# file is twice the size.
MAX_TEXTURE = 1024


def load_scene(path):
    """[(name, geometry node, node transform, [material names])] for each mesh."""
    root, _ = fbx.load(path)
    objects, names = fbx.index_objects(root)
    children, _ = fbx.connections(root)
    meshes = []
    for oid, name in names.items():
        node = objects[oid]
        if node.name != 'Model' or node.props[2] != 'Mesh':
            continue
        linked = [objects[k] for k in children.get(oid, [])]
        geometry = next(o for o in linked if o.name == 'Geometry')
        # An FBX material index is an offset into the model's own material
        # connections, in the order they appear in the file.
        materials = [fbx._clean(o.props[1]) for o in linked if o.name == 'Material']
        meshes.append((name, geometry, fbx_mesh.node_matrix(node), materials))
    return meshes


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


def texture_png(spec):
    """The material's base colour as PNG bytes, or None if it has no map."""
    if 'map' not in spec:
        return None
    from PIL import Image, ImageChops
    image = Image.open(os.path.join(TEXTURE_DIR, spec['map'])).convert('RGB')
    if 'ao' in spec:
        occlusion = Image.open(os.path.join(TEXTURE_DIR, spec['ao'])).convert('L')
        if occlusion.size != image.size:
            occlusion = occlusion.resize(image.size, Image.LANCZOS)
        image = ImageChops.multiply(image, Image.merge('RGB', (occlusion,) * 3))
    if max(image.size) > MAX_TEXTURE:
        scale = MAX_TEXTURE / max(image.size)
        image = image.resize((round(image.width * scale),
                              round(image.height * scale)), Image.LANCZOS)
    buffer = io.BytesIO()
    image.save(buffer, 'PNG', optimize=True)
    return buffer.getvalue()


def main():
    meshes = load_scene(SOURCE)

    # Everything is placed in the FBX's own frame first, so the bounds that
    # decide the scale and the origin are measured across the whole gun.
    parts = []
    low = [1e30] * 3
    high = [-1e30] * 3
    for name, geometry, (linear, translation), materials in meshes:
        groups = fbx_mesh.extract(geometry, linear, translation)
        for index, group in sorted(groups.items()):
            material = materials[index] if index < len(materials) else materials[0]
            parts.append((name, material, group))
            positions = group['positions']
            for i in range(0, len(positions), 3):
                for c in range(3):
                    low[c] = min(low[c], positions[i + c])
                    high[c] = max(high[c], positions[i + c])

    # The rear leaf, folded. The two iron sights sit at opposite ends of the
    # gun, so the midpoint of its length is all it takes to tell them apart and
    # nothing here has to know where either one actually is. Done after the
    # bounds, which are the whole gun's and must not move when a part is
    # dropped: the scale and the muzzle in weapon_specs.c are measured off them.
    parts = [(name, material,
              fold_rear_sight(group, (low[0] + high[0]) * 0.5)
              if name == 'IronSight' else group)
             for name, material, group in parts]

    scale = OVERALL_LENGTH / (high[0] - low[0])
    # The bore, not the mesh's centre: the barrel is the only part of the gun
    # whose height the viewmodel's numbers care about, and the last stretch of
    # X is barrel and nothing else.
    barrel = [p for _, _, g in parts for p in
              [g['positions'][i:i + 3] for i in range(0, len(g['positions']), 3)]
              if p[0] > high[0] - (high[0] - low[0]) * 0.05]
    bore_y = (min(p[1] for p in barrel) + max(p[1] for p in barrel)) * 0.5
    bore_z = (min(p[2] for p in barrel) + max(p[2] for p in barrel)) * 0.5
    origin = [0.0, bore_y, bore_z]

    def place(x, y, z):
        """FBX (forward, up, across) to viewmodel (right, up, back), in metres."""
        return ((z - origin[2]) * scale,
                (y - origin[1]) * scale,
                -(x - origin[0]) * scale)

    glb = glb_write.Glb()
    material_ids = {}
    for name, spec in MATERIALS.items():
        png = texture_png(spec)
        material_ids[name] = glb.material(
            name, spec.get('color', (1.0, 1.0, 1.0, 1.0)),
            glb.image(png) if png else None)

    triangles = 0
    for name, material, group in parts:
        positions, normals = [], []
        source_p, source_n = group['positions'], group['normals']
        for i in range(0, len(source_p), 3):
            positions += place(*source_p[i:i + 3])
            # The mapping is a rotation, so normals go through it unscaled.
            nx, ny, nz = source_n[i:i + 3]
            normals += [nz, ny, -nx]
        glb.primitive(positions, normals, group['uvs'], group['indices'],
                      material_ids[material])
        triangles += len(group['indices']) // 3
        print('  %-10s %-13s %6d vertices %6d triangles'
              % (name, material, len(positions) // 3, len(group['indices']) // 3))

    size = glb.write(OUTPUT)

    # The landmarks weapon_specs.c is set from. Printed rather than written
    # anywhere, because a spec is tuning and wants a person to approve it.
    muzzle = place(high[0], bore_y, bore_z)
    print('\n%s: %d triangles, %.1f KB' % (OUTPUT, triangles, size / 1024.0))
    print('scale %.6f m per unit, %.3f m overall' % (scale, OVERALL_LENGTH))
    print('landmarks in viewmodel space (+x right, +y up, -z forward):')
    print('  muzzle          z %+.4f  (muzzle_forward %.4f)' % (muzzle[2], -muzzle[2]))
    print('  butt            z %+.4f' % (-(low[0] - origin[0]) * scale))
    # What an optic clamps to, so it is the flat top of the receiver and not
    # whatever stands highest on the gun. Taken across the middle of the rail
    # and behind the front sight, which is the stretch a sight can sit on.
    rail = [p for _, _, g in parts for p in
            [g['positions'][i:i + 3] for i in range(0, len(g['positions']), 3)]
            if abs(p[2] - bore_z) < 0.5 and p[0] < (low[0] + high[0]) * 0.5]
    print('  top of the rail y %+.4f' % ((max(p[1] for p in rail) - bore_y) * scale))
    print('  lowest point    y %+.4f' % ((low[1] - bore_y) * scale))
    print('  half width      x %+.4f' % ((high[2] - low[2]) * 0.5 * scale))


if __name__ == '__main__':
    main()
