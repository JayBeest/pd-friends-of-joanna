#!/usr/bin/env python3
"""Minimal binary-FBX reader. stdlib only (struct, zlib)."""
import struct, zlib

class Node:
    __slots__ = ('name','props','children')
    def __init__(s,name,props,children):
        s.name, s.props, s.children = name, props, children
    def find(s, name):
        for c in s.children:
            if c.name == name: return c
        return None
    def findall(s, name):
        return [c for c in s.children if c.name == name]
    def __repr__(s):
        return "<%s props=%d kids=%d>" % (s.name, len(s.props), len(s.children))

def _array(buf, off, itemfmt, itemsize):
    n, enc, clen = struct.unpack_from('<III', buf, off); off += 12
    raw = buf[off:off+clen]; off += clen
    if enc == 1:
        raw = zlib.decompress(raw)
    return list(struct.unpack('<%d%s' % (n, itemfmt), raw[:n*itemsize])), off

def _prop(buf, off):
    t = buf[off:off+1].decode(); off += 1
    if   t == 'Y': v, = struct.unpack_from('<h', buf, off); off += 2
    elif t == 'C': v = buf[off] != 0; off += 1
    elif t == 'I': v, = struct.unpack_from('<i', buf, off); off += 4
    elif t == 'F': v, = struct.unpack_from('<f', buf, off); off += 4
    elif t == 'D': v, = struct.unpack_from('<d', buf, off); off += 8
    elif t == 'L': v, = struct.unpack_from('<q', buf, off); off += 8
    elif t in 'SR':
        n, = struct.unpack_from('<I', buf, off); off += 4
        v = buf[off:off+n]; off += n
        if t == 'S': v = v.decode('utf-8','replace')
    elif t == 'f': v, off = _array(buf, off, 'f', 4)
    elif t == 'd': v, off = _array(buf, off, 'd', 8)
    elif t == 'l': v, off = _array(buf, off, 'q', 8)
    elif t == 'i': v, off = _array(buf, off, 'i', 4)
    elif t == 'b': v, off = _array(buf, off, 'b', 1)
    else: raise ValueError("unknown property type %r at %d" % (t, off))
    return v, off

def _node(buf, off, wide):
    hdr = '<QQQB' if wide else '<IIIB'
    hsz = 25 if wide else 13
    end, nprops, plen, namelen = struct.unpack_from(hdr, buf, off)
    off += hsz
    if end == 0:
        return None, off              # null record: end of sibling list
    name = buf[off:off+namelen].decode('utf-8','replace'); off += namelen
    props = []
    for _ in range(nprops):
        v, off = _prop(buf, off)
        props.append(v)
    kids = []
    while off < end - (hsz if wide else 13):
        k, off = _node(buf, off, wide)
        if k is None: break
        kids.append(k)
    return Node(name, props, kids), end

def load(path):
    buf = open(path,'rb').read()
    assert buf[:20] == b'Kaydara FBX Binary  ', "not a binary FBX"
    ver, = struct.unpack_from('<I', buf, 23)
    wide = ver >= 7500
    off = 27
    roots = []
    while off < len(buf) - 160:
        n, off = _node(buf, off, wide)
        if n is None: break
        roots.append(n)
    return ver, Node('<root>', [], roots)


def read_mesh(path):
    """Positions, triangles, UVs, per-face material, and each material's image.

    Returns (pos, tris, uv, uvindex, materials, material_images) where
    material_images[i] is the texture filename material i is bound to, resolved
    through the FBX connection graph in the order the mesh's model lists them.
    """
    ver, root = load(path)
    objs, conns = root.find('Objects'), root.find('Connections')
    byid = {c.props[0]: (c.name, str(c.props[1]).split('\x00')[0]) for c in objs.children}
    edges = [c.props[:3] for c in conns.children if len(c.props) >= 3]

    geos = [c for c in objs.children if c.name == 'Geometry']
    if not geos:
        raise ValueError('%s: no Geometry node' % path)
    geo = geos[0]
    geoname = str(geo.props[1]).split('\x00')[0]
    models = [c for c in objs.children if c.name == 'Model'
              and str(c.props[1]).split('\x00')[0] == geoname]
    model = models[0] if models else None

    mats = [a for (t, a, b) in edges
            if model is not None and b == model.props[0]
            and byid.get(a, ('',))[0] == 'Material']
    images = []
    for m in mats:
        texs = [a for (t, a, b) in edges if b == m and byid.get(a, ('',))[0] == 'Texture']
        vids = [byid[a][1] for tx in texs for (t, a, b) in edges
                if b == tx and byid.get(a, ('',))[0] == 'Video']
        images.append(vids[0] if vids else None)

    V = geo.find('Vertices').props[0]
    PI = geo.find('PolygonVertexIndex').props[0]
    uvl = geo.find('LayerElementUV')
    UV, UVI = uvl.find('UV').props[0], uvl.find('UVIndex').props[0]
    MAT = geo.find('LayerElementMaterial').find('Materials').props[0]

    pos = [(V[i*3], V[i*3+1], V[i*3+2]) for i in range(len(V)//3)]
    tris, cur = [], []
    for pvi, idx in enumerate(PI):
        cur.append((~idx if idx < 0 else idx, pvi))
        if idx < 0:
            if len(cur) != 3:
                raise ValueError('%s: polygon with %d vertices; triangulate first'
                                 % (path, len(cur)))
            tris.append(cur); cur = []
    uv = [(UV[i*2], UV[i*2+1]) for i in range(len(UV)//2)]
    return pos, tris, uv, UVI, MAT, images
