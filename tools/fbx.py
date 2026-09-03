"""Minimal binary FBX 7.x reader: node tree with typed properties."""
import struct, zlib

class Node:
    __slots__ = ('name', 'props', 'children')
    def __init__(self, name, props, children):
        self.name, self.props, self.children = name, props, children
    def find(self, name):
        for c in self.children:
            if c.name == name:
                return c
        return None
    def findall(self, name):
        return [c for c in self.children if c.name == name]
    def __repr__(self):
        return f'<{self.name} props={len(self.props)} kids={len(self.children)}>'

def _array(data, off, item, size):
    length, encoding, comp_len = struct.unpack_from('<III', data, off)
    off += 12
    raw = data[off:off + comp_len]
    off += comp_len
    if encoding == 1:
        raw = zlib.decompress(raw)
    return list(struct.unpack('<%d%s' % (length, item), raw[:length * size])), off

def _prop(data, off):
    t = data[off:off + 1].decode('latin-1'); off += 1
    if t == 'Y': v = struct.unpack_from('<h', data, off)[0]; off += 2
    elif t == 'C': v = data[off] != 0; off += 1
    elif t == 'I': v = struct.unpack_from('<i', data, off)[0]; off += 4
    elif t == 'F': v = struct.unpack_from('<f', data, off)[0]; off += 4
    elif t == 'D': v = struct.unpack_from('<d', data, off)[0]; off += 8
    elif t == 'L': v = struct.unpack_from('<q', data, off)[0]; off += 8
    elif t == 'f': v, off = _array(data, off, 'f', 4)
    elif t == 'd': v, off = _array(data, off, 'd', 8)
    elif t == 'l': v, off = _array(data, off, 'q', 8)
    elif t == 'i': v, off = _array(data, off, 'i', 4)
    elif t == 'b': v, off = _array(data, off, 'b', 1)
    elif t in 'SR':
        n = struct.unpack_from('<I', data, off)[0]; off += 4
        v = data[off:off + n]; off += n
        if t == 'S': v = v.decode('utf-8', 'replace')
    else:
        raise ValueError('unknown property type %r at %d' % (t, off - 1))
    return v, off

def _node(data, off, ver):
    fmt, hdr = ('<QQQB', 25) if ver >= 7500 else ('<IIIB', 13)
    end, nprops, plen, nlen = struct.unpack_from(fmt, data, off)
    off += hdr
    if end == 0:
        return None, off
    name = data[off:off + nlen].decode('utf-8', 'replace'); off += nlen
    props = []
    for _ in range(nprops):
        v, off = _prop(data, off)
        props.append(v)
    children = []
    while off < end - (hdr - 1):
        c, off = _node(data, off, ver)
        if c is None:
            break
        children.append(c)
    return Node(name, props, children), end

def load(path):
    data = open(path, 'rb').read()
    assert data[:21] == b'Kaydara FBX Binary  \x00', 'not a binary FBX'
    ver = struct.unpack_from('<I', data, 23)[0]
    off = 27
    roots = []
    while off < len(data) - 20:
        n, off = _node(data, off, ver)
        if n is None:
            break
        roots.append(n)
    return Node('Root', [], roots), ver


# ---- scene extraction -----------------------------------------------------

def _clean(raw):
    """FBX object names arrive as 'Name\\x00\\x01ClassName'."""
    return raw.split('\x00')[0]

def index_objects(root):
    """{id: node} for everything under Objects, plus {id: short name}."""
    objects, names = {}, {}
    for c in root.find('Objects').children:
        if c.props and isinstance(c.props[0], int):
            objects[c.props[0]] = c
            if len(c.props) > 1 and isinstance(c.props[1], str):
                names[c.props[0]] = _clean(c.props[1])
    return objects, names

def connections(root):
    """(children_of, property_links): id -> [child ids], (id, prop) -> [src ids]."""
    kids, props = {}, {}
    for c in root.find('Connections').children:
        kind, src, dst = c.props[0], c.props[1], c.props[2]
        if kind == 'OO':
            kids.setdefault(dst, []).append(src)
        elif kind == 'OP':
            props.setdefault((dst, c.props[3]), []).append(src)
    return kids, props

def prop70(node, key, default=None):
    p70 = node.find('Properties70')
    if not p70:
        return default
    for p in p70.children:
        if p.props and p.props[0] == key:
            return p.props[4:]
    return default
