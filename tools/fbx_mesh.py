"""Turning an FBX scene's mesh nodes into flat triangle lists.

FBX stores a mesh the way a modelling package wants it -- shared positions,
per-corner normals and UVs in their own indexed arrays, and polygons of any
size. GL wants one flat vertex array. This does that conversion and nothing
else: no scene graph interpretation beyond each node's own transform, and no
opinion about which way is up. The caller decides that.
"""
import math
import fbx


def _euler_matrix(rx, ry, rz):
    """FBX's default eXYZ order: X first, so the matrix is Rz * Ry * Rx."""
    rx, ry, rz = math.radians(rx), math.radians(ry), math.radians(rz)
    cx, sx, cy, sy, cz, sz = (math.cos(rx), math.sin(rx), math.cos(ry),
                              math.sin(ry), math.cos(rz), math.sin(rz))
    return [
        [cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
        [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
        [-sy,     cy * sx,                cy * cx],
    ]


def _matmul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)]
            for i in range(3)]


def _apply(m, v):
    return [sum(m[i][j] * v[j] for j in range(3)) for i in range(3)]


def node_matrix(model):
    """The node's local transform as (3x3 linear part, translation).

    FBX does not store one matrix per node, it stores the recipe

        T . Roff . Rp . Rpre . R . Rpost^-1 . Rp^-1 . Soff . Sp . S . Sp^-1

    and a modeller that never touched a pivot leaves every term but T, R and S
    at identity - which is every node in the rifle, and why this used to read
    only those three. The katana does not: Maya froze its transform and left a
    scaling pivot 36 units down the blade, so dropping the pivot terms there
    would put the sword thirty-odd centimetres from where the file says it is.
    Refusing them instead of ignoring them was the right call at the time; this
    is the same call made once there is an asset to make it for.

    The geometric transform is still refused. It sits outside this chain -- it
    applies to the mesh and is not inherited by children -- so a node carrying
    one wants handling at the scene level, not here, and no file in the project
    has one.
    """
    for unsupported in ('GeometricTranslation', 'GeometricRotation',
                        'GeometricScaling'):
        if fbx.prop70(model, unsupported) is not None:
            raise ValueError('%s carries an unsupported %s' %
                             (fbx._clean(model.props[1]), unsupported))
    t = fbx.prop70(model, 'Lcl Translation', [0.0, 0.0, 0.0])
    r = fbx.prop70(model, 'Lcl Rotation', [0.0, 0.0, 0.0])
    s = fbx.prop70(model, 'Lcl Scaling', [1.0, 1.0, 1.0])
    roff = fbx.prop70(model, 'RotationOffset', [0.0, 0.0, 0.0])
    rp = fbx.prop70(model, 'RotationPivot', [0.0, 0.0, 0.0])
    soff = fbx.prop70(model, 'ScalingOffset', [0.0, 0.0, 0.0])
    sp = fbx.prop70(model, 'ScalingPivot', [0.0, 0.0, 0.0])
    pre = fbx.prop70(model, 'PreRotation', [0.0, 0.0, 0.0])
    post = fbx.prop70(model, 'PostRotation', [0.0, 0.0, 0.0])

    # Rpost is inverted in the chain, and the inverse of a rotation is its
    # transpose, so no matrix has to be inverted anywhere here.
    rot = _matmul(_euler_matrix(*pre), _euler_matrix(*r))
    post_matrix = _euler_matrix(*post)
    rot = _matmul(rot, [[post_matrix[j][i] for j in range(3)]
                        for i in range(3)])

    linear = [[rot[i][j] * s[j] for j in range(3)] for i in range(3)]
    # Walking the chain right to left on a point: unpivot the scale, scale,
    # re-pivot, offset, unpivot the rotation, rotate, re-pivot, offset,
    # translate. Everything but the two rotations is a translation, so what
    # survives as the constant term is this.
    pivoted = _apply(rot, [sp[i] + soff[i] - rp[i] for i in range(3)])
    scaled_pivot = _apply(linear, sp)
    translation = [pivoted[i] - scaled_pivot[i] + rp[i] + roff[i] + t[i]
                   for i in range(3)]
    return linear, translation


def _layer(geometry, element, values_key, count_key=None):
    """(values, index or None, mapping) for one layer element."""
    node = geometry.find(element)
    if node is None:
        return None, None, None
    values = node.find(values_key)
    index = node.find(count_key) if count_key else None
    return (values.props[0] if values else None,
            index.props[0] if index else None,
            node.find('MappingInformationType').props[0])


def _lookup(values, index, mapping, stride, polygon_vertex, polygon, control):
    if mapping == 'ByPolygonVertex':   at = polygon_vertex
    elif mapping == 'ByPolygon':       at = polygon
    elif mapping == 'ByControlPoint':  at = control
    elif mapping == 'AllSame':         at = 0
    else: raise ValueError('unsupported mapping %r' % mapping)
    if index is not None:
        at = index[at]
    return values[at * stride:at * stride + stride]


def extract(geometry, linear, translation):
    """Triangles in the node's parent space, grouped by material index.

    Returns {material_index: {'positions': [...], 'normals': [...],
    'uvs': [...], 'indices': [...]}} with positions and normals already
    through the node transform.
    """
    control = geometry.find('Vertices').props[0]
    corners = geometry.find('PolygonVertexIndex').props[0]
    normals, normal_index, normal_map = _layer(
        geometry, 'LayerElementNormal', 'Normals', 'NormalsIndex')
    uvs, uv_index, uv_map = _layer(geometry, 'LayerElementUV', 'UV', 'UVIndex')
    materials, _, material_map = _layer(
        geometry, 'LayerElementMaterial', 'Materials')

    # Normals rotate with the node but must not pick up its scale as a
    # stretch. Every node here scales uniformly, so dividing the rotated
    # normal by that scale is exact and avoids inverting a matrix.
    scale = [math.sqrt(sum(linear[i][c] ** 2 for i in range(3))) or 1.0
             for c in range(3)]

    def place(v):
        return [sum(linear[i][j] * v[j] for j in range(3)) + translation[i]
                for i in range(3)]

    def rotate(n):
        out = [sum(linear[i][j] * n[j] / scale[j] for j in range(3))
               for i in range(3)]
        length = math.sqrt(sum(c * c for c in out)) or 1.0
        return [c / length for c in out]

    groups, unique = {}, {}
    polygon, start = 0, 0
    for end in range(len(corners)):
        if corners[end] >= 0:
            continue
        # A negative index closes the polygon; the real index is its complement.
        fan = list(range(start, end + 1))
        material = 0 if material_map is None else _lookup(
            materials, None, material_map, 1, start, polygon, 0)[0]
        group = groups.setdefault(material, {'positions': [], 'normals': [],
                                             'uvs': [], 'indices': []})
        seen = unique.setdefault(material, {})

        corner_vertices = []
        for pv in fan:
            ctrl = corners[pv] if corners[pv] >= 0 else ~corners[pv]
            position = place(control[ctrl * 3:ctrl * 3 + 3])
            normal = rotate(_lookup(normals, normal_index, normal_map, 3,
                                    pv, polygon, ctrl))
            uv = _lookup(uvs, uv_index, uv_map, 2, pv, polygon, ctrl) \
                if uvs else [0.0, 0.0]
            # glTF's V axis points down from the top-left; FBX's points up.
            key = (tuple(round(c, 6) for c in position),
                   tuple(round(c, 5) for c in normal),
                   tuple(round(c, 6) for c in uv))
            index = seen.get(key)
            if index is None:
                index = len(group['positions']) // 3
                seen[key] = index
                group['positions'] += position
                group['normals'] += normal
                group['uvs'] += [uv[0], 1.0 - uv[1]]
            corner_vertices.append(index)

        for i in range(1, len(corner_vertices) - 1):
            group['indices'] += [corner_vertices[0], corner_vertices[i],
                                 corner_vertices[i + 1]]
        polygon += 1
        start = end + 1
    return groups
