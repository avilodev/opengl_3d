"""A small glTF 2.0 binary writer: enough of the format to carry a static prop.

Only what the engine's reader consumes -- one mesh of triangle primitives with
positions, normals and UVs, materials with a base colour and an optional
embedded PNG. No scene graph, because the converter bakes node transforms into
the vertices before it gets here.
"""
import json
import struct

FLOAT, UNSIGNED_INT = 5126, 5125
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963


class Glb:
    def __init__(self, generator='tools/fbx_to_glb.py'):
        self.blob = bytearray()
        self.json = {
            'asset': {'version': '2.0', 'generator': generator},
            'scene': 0,
            'scenes': [{'nodes': [0]}],
            'nodes': [{'mesh': 0}],
            'meshes': [{'primitives': []}],
            'accessors': [], 'bufferViews': [], 'materials': [],
        }

    def _view(self, data, target=None):
        # glTF requires an accessor's view to start on a multiple of its
        # component size; four covers every type used here.
        while len(self.blob) % 4:
            self.blob.append(0)
        view = {'buffer': 0, 'byteOffset': len(self.blob), 'byteLength': len(data)}
        if target is not None:
            view['target'] = target
        self.blob += data
        self.json['bufferViews'].append(view)
        return len(self.json['bufferViews']) - 1

    def _accessor(self, values, kind, components, component_type, target):
        pack = '<%d%s' % (len(values), 'f' if component_type == FLOAT else 'I')
        view = self._view(struct.pack(pack, *values), target)
        accessor = {'bufferView': view, 'componentType': component_type,
                    'count': len(values) // components, 'type': kind}
        if kind == 'VEC3' and component_type == FLOAT:
            # Required on POSITION, and the engine reads it as the prop's
            # bounding box rather than walking the vertices again.
            axes = [values[i::3] for i in range(3)]
            accessor['min'] = [min(a) for a in axes]
            accessor['max'] = [max(a) for a in axes]
        self.json['accessors'].append(accessor)
        return len(self.json['accessors']) - 1

    def image(self, png_bytes):
        view = self._view(png_bytes)
        self.json.setdefault('images', []).append(
            {'bufferView': view, 'mimeType': 'image/png'})
        self.json.setdefault('samplers', [{'wrapS': 10497, 'wrapT': 10497}])
        index = len(self.json['images']) - 1
        self.json.setdefault('textures', []).append({'sampler': 0, 'source': index})
        return len(self.json['textures']) - 1

    def material(self, name, base_color, texture=None):
        pbr = {'baseColorFactor': list(base_color),
               'metallicFactor': 0.0, 'roughnessFactor': 1.0}
        if texture is not None:
            pbr['baseColorTexture'] = {'index': texture}
        material = {'name': name, 'pbrMetallicRoughness': pbr}
        # The engine reads the alpha off baseColorFactor and draws anything
        # under one in a second, blended pass. Stating the mode as well costs
        # nothing and keeps the file honest for anything else that opens it.
        if len(base_color) > 3 and base_color[3] < 1.0:
            material['alphaMode'] = 'BLEND'
            material['doubleSided'] = True
        self.json['materials'].append(material)
        return len(self.json['materials']) - 1

    def primitive(self, positions, normals, uvs, indices, material):
        attributes = {
            'POSITION': self._accessor(positions, 'VEC3', 3, FLOAT, ARRAY_BUFFER),
            'NORMAL': self._accessor(normals, 'VEC3', 3, FLOAT, ARRAY_BUFFER),
            'TEXCOORD_0': self._accessor(uvs, 'VEC2', 2, FLOAT, ARRAY_BUFFER),
        }
        self.json['meshes'][0]['primitives'].append({
            'attributes': attributes,
            'indices': self._accessor(indices, 'SCALAR', 1, UNSIGNED_INT,
                                      ELEMENT_ARRAY_BUFFER),
            'material': material,
        })

    def write(self, path):
        self.json['buffers'] = [{'byteLength': len(self.blob)}]
        text = json.dumps(self.json, separators=(',', ':')).encode('utf-8')
        text += b' ' * (-len(text) % 4)
        blob = bytes(self.blob) + b'\0' * (-len(self.blob) % 4)
        total = 12 + 8 + len(text) + 8 + len(blob)
        with open(path, 'wb') as f:
            f.write(struct.pack('<III', 0x46546C67, 2, total))
            f.write(struct.pack('<II', len(text), 0x4E4F534A) + text)
            f.write(struct.pack('<II', len(blob), 0x004E4942) + blob)
        return total
