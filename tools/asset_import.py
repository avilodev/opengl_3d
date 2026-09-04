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
somebody to copy into weapon_specs.c; the saya's depth reached a probe the same
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

def load_parts(path):
    """[(node name, material name, triangle group)] for every mesh in the file.

    Node transforms are folded into the vertices here - including scaling
    pivots, which the katana's node has and which fbx_mesh.node_matrix handles -
    so what comes back is already in the file's own world space and the runtime
    has no scene graph to walk.

    An FBX material index is an offset into the MODEL's own material
    connections, in the order they appear in the file, which is why the names
    are gathered per node rather than globally."""
    root, _ = fbx.load(path)
    objects, names = fbx.index_objects(root)
    children, _ = fbx.connections(root)
    parts = []
    for oid, node_name in names.items():
        node = objects[oid]
        if node.name != 'Model' or node.props[2] != 'Mesh':
            continue
        linked = [objects[k] for k in children.get(oid, [])]
        geometry = next(o for o in linked if o.name == 'Geometry')
        materials = [fbx._clean(o.props[1]) for o in linked
                     if o.name == 'Material']
        linear, translation = fbx_mesh.node_matrix(node)
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
# Textures

def texture_png(folder, name, ao=None, max_size=1024, gain=None):
    """A base colour map as PNG bytes, or None if the file is not there.

    `ao` is multiplied in. Fixed-function lighting has one lamp and no
    shadowing, so a model lit by it reads as a plastic toy; folding in the
    occlusion map the artist supplied is the cheapest way to put the crevices
    back, and it costs nothing at runtime.

    `max_size` caps the edge. These are sampled by a fixed-function pipeline on
    something that fills a fraction of the screen, so past a point the extra
    detail is not resolvable and the file is twice the size."""
    from PIL import Image, ImageChops
    path = os.path.join(folder, name)
    if not os.path.exists(path):
        return None
    image = Image.open(path).convert('RGB')
    if ao:
        occlusion = Image.open(os.path.join(folder, ao)).convert('L')
        if occlusion.size != image.size:
            occlusion = occlusion.resize(image.size, Image.LANCZOS)
        image = ImageChops.multiply(image, Image.merge('RGB', (occlusion,) * 3))
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
    are baked back into weapon_specs.c. Some numbers appear in both, which is
    exactly the interesting case: it means a measurement and a tuned value can
    be compared.

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
