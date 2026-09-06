#!/usr/bin/env python3
"""What every asset import does, so that a new asset does not restate it.

There were three of these scripts - one for the rifle, one for the katana, one
for the optic - and between them 826 lines that baked the same three things:
node transforms folded into the vertices, the axes and the scale brought into
the viewmodel's frame and into metres, and an origin moved onto whatever point
the spec's numbers are measured from. Weapon four would have been script four.

They were not pure duplication, which is why this is a library and not one
script with a wall of flags. Each asset really does have something of its own:
the M4's rear sight is modelled deployed and has to be folded flat, the optic's
atlas needs a gain applied and its glass kept translucent, the katana's saya is
a second file split out of the same mesh. Those stay in the per-asset script,
which is now short enough to read in one go. What is here is the eighty per
cent that was copied.

The other thing this exists for: a number a machine computed and a human
retyped is a number that can be retyped wrong, and nothing downstream can tell.
tools/scope_to_glb.py used to MEASURE the optic's rail height and print it for
somebody to copy into a weapon's spec; the saya's depth reached a probe the same
way, as `#define SAYA_DEPTH 0.7907f`. write_placement is how a measurement gets
to the code that needs it without passing through a person.

Nothing here reads FBX or writes glTF itself - fbx.py, fbx_mesh.py and
glb_write.py already do, and are unchanged.
"""
import io
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import fbx
import fbx_mesh
import glb_write


# --------------------------------------------------------------------------
# The manifest

class Manifest:
    """An asset's import.json: where its source is, how big the thing is, and
    which map goes on which material.

    Beside the asset rather than in the script, because it describes the FILE -
    if the artist hands over a new export with a different scale or a renamed
    material, what changes is a fact about that folder. The script describes
    how this KIND of thing is imported, which does not change when the file
    does."""

    def __init__(self, folder):
        self.folder = folder
        with open(os.path.join(folder, 'import.json'), encoding='utf-8') as f:
            self.data = json.load(f)

    def __getitem__(self, key):
        return self.data[key]

    def get(self, key, default=None):
        return self.data.get(key, default)

    def path(self, key):
        """A path stated relative to the asset's own folder."""
        return os.path.join(self.folder, self.data[key])

    @property
    def textures(self):
        return os.path.join(self.folder, self.data.get('textures', 'textures'))


# --------------------------------------------------------------------------
# Reading the source

def _compose(outer, inner):
    """Two (linear, translation) transforms as one, outer applied second."""
    ol, ot = outer
    il, it = inner
    linear = [[sum(ol[i][k] * il[k][j] for k in range(3)) for j in range(3)]
              for i in range(3)]
    translation = [sum(ol[i][j] * it[j] for j in range(3)) + ot[i]
                   for i in range(3)]
    return linear, translation


def _world_matrix(objects, parents, oid, cache):
    """A node's transform all the way up to the scene root.

    A node's own matrix places it in its PARENT's space, not in the file's, and
    those are the same thing only while the scene is flat. Every asset before
    the carbine was flat, so folding one matrix was folding all of them, and
    this walk would have been a loop that never ran twice. The carbine's optic
    is a lens parented to the sight body: fold only the lens's own matrix and
    the glass lands at the world origin at a quarter scale, inside nothing,
    which is a wrong answer that draws without erroring."""
    if oid in cache:
        return cache[oid]
    local = fbx_mesh.node_matrix(objects[oid])
    parent = parents.get(oid)
    if parent is not None and parent in objects and objects[parent].name == 'Model':
        local = _compose(_world_matrix(objects, parents, parent, cache), local)
    cache[oid] = local
    return local


def load_parts(path):
    """[(node name, material name, triangle group)] for every mesh in the file.

    Node transforms are folded into the vertices here - including scaling
    pivots, which the katana's node has and which fbx_mesh.node_matrix handles,
    and including every ancestor's - so what comes back is in the file's own
    world space and the runtime has no scene graph to walk.

    An FBX material index is an offset into the MODEL's own material
    connections, in the order they appear in the file, which is why the names
    are gathered per node rather than globally."""
    root, _ = fbx.load(path)
    objects, names = fbx.index_objects(root)
    children, _ = fbx.connections(root)
    parents = {child: parent
               for parent, kids in children.items() for child in kids}
    cache, parts = {}, []
    for oid, node_name in names.items():
        node = objects[oid]
        if node.name != 'Model' or node.props[2] != 'Mesh':
            continue
        linked = [objects[k] for k in children.get(oid, [])]
        geometry = next(o for o in linked if o.name == 'Geometry')
        materials = [fbx._clean(o.props[1]) for o in linked
                     if o.name == 'Material']
        linear, translation = _world_matrix(objects, parents, oid, cache)
        groups = fbx_mesh.extract(geometry, linear, translation)
        for index, group in sorted(groups.items()):
            material = materials[index] if index < len(materials) else materials[0]
            parts.append((node_name, material, group))
    if not parts:
        raise ValueError('%s holds no mesh' % path)
    return parts


def vertices(group):
    """The group's positions as [x, y, z] triples."""
    p = group['positions']
    return [p[i:i + 3] for i in range(0, len(p), 3)]


def bounds(groups):
    """(low, high) corners of the box around several triangle groups."""
    low = [min(min(g['positions'][c::3]) for g in groups) for c in range(3)]
    high = [max(max(g['positions'][c::3]) for g in groups) for c in range(3)]
    return low, high


def merge(groups):
    """Several triangle groups as one, with the indices rebased.

    For an asset whose materials all index into a single atlas: nine primitives
    off one image would be nine decodes of it and nine copies in video memory,
    where merged it is one texture and one draw."""
    out = {'positions': [], 'normals': [], 'uvs': [], 'indices': []}
    for group in groups:
        base = len(out['positions']) // 3
        for key in ('positions', 'normals', 'uvs'):
            out[key] += group[key]
        out['indices'] += [i + base for i in group['indices']]
    return out


# --------------------------------------------------------------------------
# The frame

class Frame:
    """The one transform an import applies: an axis remap, a scale into metres,
    and an origin.

    The axes are stated as a triple like ('+z', '+y', '-x'), read as "the
    viewmodel's +x is the file's +z, its +y is the file's +y, its -z is the
    file's -x". Every asset arrives in whatever frame its authoring package
    used and they are all different; what they have in common is that the
    remap is a rotation, which is why normals go through it unscaled and
    without the origin.

    Where the origin GOES is the one thing this cannot decide, because it is
    the point every number on the spec is measured from and it means something
    different for each kind of thing: a gun's is on the bore at the back of the
    receiver, because that is what a muzzle flash and a tracer hang off; a
    sword's is the middle of the grip, because a sword has no bore and what it
    hangs off is the hand; an optic's is its reticle, because that is the point
    the eye lines up with. The per-asset script finds it and hands it here."""

    AXES = {'x': 0, 'y': 1, 'z': 2}

    def __init__(self, axes, origin, scale):
        self.axes = [(self.AXES[a[1]], -1.0 if a[0] == '-' else 1.0)
                     for a in axes]
        self.origin = origin
        self.scale = scale

    def place(self, x, y, z):
        """One position, into the viewmodel's frame and into metres."""
        source = (x, y, z)
        out = []
        for index, sign in self.axes:
            v = (source[index] - self.origin[index]) * self.scale
            out.append(-v if sign < 0.0 else v)
        return tuple(out)

    def direction(self, x, y, z):
        """One normal. The remap is a rotation, so no origin and no scale."""
        source = (x, y, z)
        out = []
        for index, sign in self.axes:
            v = source[index]
            out.append(-v if sign < 0.0 else v)
        return tuple(out)

    def along(self, index, value):
        """One coordinate of the source frame, as a distance in metres from the
        origin. For the landmark lines, which want a number rather than a
        point."""
        return (value - self.origin[index]) * self.scale


def scale_for(low, high, axis, overall_length):
    """Metres per unit, from one measured dimension.

    One length sets the scale for everything else, because a model is in
    proportion to itself: an M4A1 is 838mm from muzzle to buttplate and every
    other dimension of that mesh follows from it."""
    extent = high[axis] - low[axis]
    if extent <= 0.0:
        raise ValueError('the model has no extent along axis %d' % axis)
    return overall_length / extent


# --------------------------------------------------------------------------
# Optics

def rail_seat(groups):
    """Y of the flat an optic rests on when it is clamped to a rail.

    Not the model's lowest point: the clamp jaw wraps around the sides of the
    rail and hangs several millimetres below its top face, so the bounding box
    would put the sight that much too low. The seat is the largest horizontal
    downward-facing surface in the bottom half, which on both optics so far is
    the only thing it could be.

    Here rather than in one optic's script because there are two of them now,
    and because this is the sight's half of a mount height whose other half is
    measured off a gun - a pair of numbers that has to be taken the same way at
    both ends or their sum means nothing."""
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


# --------------------------------------------------------------------------
# Occlusion

def _occluded(folder, name, ao=None, strength=1.0):
    """A base colour map with the artist's occlusion multiplied into it.

    One function because the map that is MEASURED and the map that is WRITTEN
    have to be the same map. surface_albedo and texture_png each used to fold
    the occlusion in themselves, which was fine while they agreed and is the
    kind of duplication that stops agreeing the moment one of them learns
    something - as one of them just did.

    `strength` is what it learned, lifted out of tools/scope_to_glb.py where
    the optic had been doing it alone. An occlusion map at full strength
    darkens the flats along with the crevices; the MP7's halves its whole
    surface, and the albedo gain then spends itself undoing that rather than
    lighting the gun. Pulling the map toward white by however much strength is
    left off keeps the crevices and gives the flats back."""
    from PIL import Image, ImageChops
    image = Image.open(os.path.join(folder, name)).convert('RGB')
    if not ao:
        return image
    occlusion = Image.open(os.path.join(folder, ao)).convert('L')
    if occlusion.size != image.size:
        occlusion = occlusion.resize(image.size, Image.LANCZOS)
    if strength < 1.0:
        occlusion = occlusion.point(
            lambda v: int(round(255.0 - strength * (255.0 - v))))
    return ImageChops.multiply(image, Image.merge('RGB', (occlusion,) * 3))


# --------------------------------------------------------------------------
# Albedo

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


def surface_albedo(folder, materials, parts, ao_strength=1.0):
    """The mean grey this asset's SURFACE reads, area-weighted, over the maps
    as they will be baked - occlusion folded in, gain not yet applied.

    Over the surface and not over the images, because a 4096px map on a part
    the size of a trigger guard says almost nothing about what the gun looks
    like and a 1024px one wrapped round the whole receiver says nearly all of
    it. Only the triangles that actually read a texel get a vote, weighted by
    how much of the gun they are.

    This is what makes two assets comparable. Fixed-function lighting has one
    lamp, no shadowing and no exposure, so how bright a thing looks is decided
    entirely by the numbers in its map - and an artist's idea of mid-grey is
    not portable between deliveries. The M4 landed at 37.6 by luck and the
    EXPS2 was pulled to 37.7 to match it; anything imported since is measured
    against that same figure rather than eyeballed against a screenshot."""
    from PIL import Image
    import os as _os
    total = weighted = 0.0
    for name, spec in materials.items():
        if 'map' not in spec:
            continue
        groups = [g for _, material, g in parts if material == name]
        if not groups:
            continue
        image = _occluded(folder, spec['map'], spec.get('ao'), ao_strength)
        grey = image.convert('L')
        pixels, (width, height) = grey.load(), grey.size
        for area, texel in samples(merge(groups), pixels, width, height):
            total += area
            weighted += area * texel
    return weighted / total if total else 0.0


# --------------------------------------------------------------------------
# Textures

def texture_png(folder, name, ao=None, max_size=1024, gain=None,
                ao_strength=1.0):
    """A base colour map as PNG bytes, or None if the file is not there.

    `ao` is multiplied in. Fixed-function lighting has one lamp and no
    shadowing, so a model lit by it reads as a plastic toy; folding in the
    occlusion map the artist supplied is the cheapest way to put the crevices
    back, and it costs nothing at runtime.

    `max_size` caps the edge. These are sampled by a fixed-function pipeline on
    something that fills a fraction of the screen, so past a point the extra
    detail is not resolvable and the file is twice the size."""
    from PIL import Image
    if not os.path.exists(os.path.join(folder, name)):
        return None
    image = _occluded(folder, name, ao, ao_strength)
    if gain is not None:
        image = image.point(lambda v: min(255, round(v * gain)))
    if max(image.size) > max_size:
        factor = max_size / max(image.size)
        image = image.resize((round(image.width * factor),
                              round(image.height * factor)), Image.LANCZOS)
    buffer = io.BytesIO()
    image.save(buffer, 'PNG', optimize=True)
    return buffer.getvalue()


# --------------------------------------------------------------------------
# Writing

class Primitives:
    """One glb under construction: the materials and the triangles going into
    it, in the order the caller adds them.

    A primitive is baked into the buffer the moment it is added rather than
    collected and written at the end, and that is not an implementation
    detail. glb_write lays materials, images and vertex data into one buffer in
    call order, so WHEN a material is made relative to the primitives that use
    it decides where everything lands and how much padding falls between. Two
    scripts that make the same materials and the same triangles in a different
    order produce two files that differ in size and agree in every way that
    matters.

    That is worth preserving rather than normalising, because it is what lets
    this refactor be checked: run the old importer, run the new one, compare
    the bytes. A library that quietly reordered the writes would produce a
    correct file that could not be compared against anything."""

    def __init__(self, generator, frame):
        self.glb = glb_write.Glb(generator)
        self.frame = frame
        self.triangles = 0

    def material(self, name, colour, png=None):
        """A material, and its image if it has one."""
        return self.glb.material(name, tuple(colour),
                                 self.glb.image(png) if png else None)

    def add(self, label, material, group):
        """One primitive, placed into the viewmodel's frame and in metres."""
        if not group['indices']:
            return
        positions, normals = [], []
        source_p, source_n = group['positions'], group['normals']
        for i in range(0, len(source_p), 3):
            positions += self.frame.place(*source_p[i:i + 3])
            # The remap is a rotation, so normals go through it unscaled.
            normals += self.frame.direction(*source_n[i:i + 3])
        self.glb.primitive(positions, normals, group['uvs'], group['indices'],
                           material)
        self.triangles += len(group['indices']) // 3
        print('  %-18s %6d vertices %6d triangles'
              % (label, len(positions) // 3, len(group['indices']) // 3))

    def write(self, output):
        size = self.glb.write(output)
        print('%s: %d triangles, %.1f KB'
              % (output, self.triangles, size / 1024.0))
        return size


def write_measured(folder, values):
    """The numbers this import MEASURED off the mesh, written where the code
    can read them.

    This is the point of the whole file. tools/scope_to_glb.py measured the
    optic's rail height and printed it for a person to copy into sights.c;
    tools/sheath_probe.c carried the saya's depth as a #define somebody had
    transcribed. Both were right, and both were right only until the asset was
    exported again - at which point the mesh moved and the number did not, and
    nothing anywhere could tell.

    measured.json and not placement.json, because the two are different things
    and keeping them apart is what stops either being half-trusted. This file
    is FACTS ABOUT THE MESH, written by a machine, overwritten every import,
    and never edited by hand. placement.json beside it is the tuning overlay -
    spec fields a person is nudging, hand-edited, and deleted once its numbers
    are baked back into the weapon's own file in src/gameplay/weapons/. Some
    numbers appear in both, which is exactly the interesting case: it means a
    measurement and a tuned value can be compared.

    Written sorted and with a trailing newline so that re-running an import
    that changed nothing leaves no diff."""
    path = os.path.join(folder, "measured.json")
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(values, f, indent=2, sort_keys=True)
        f.write('\n')
    print('\n%s:' % path)
    for key in sorted(values):
        value = values[key]
        if isinstance(value, float):
            print('  %-28s %+.4f' % (key, value))
        else:
            print('  %-28s %s' % (key, value))
    return path
