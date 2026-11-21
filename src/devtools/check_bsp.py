#!/usr/bin/env python3
"""Checks a Source BSP map (version 19-21) the way the client does when it loads it.

The engine rejects a map whose lumps reference each other out of range (it
disconnects with "Map ... is corrupted"), and silently drops some optional data
(lightmaps of single faces, overlays, detail props). This script runs the same
checks without the game, so maps can be verified before playing:

    check_bsp.py cstrike/maps/*.bsp
    check_bsp.py cstrike/maps cstrike/download/maps
    check_bsp.py -v de_dust2.bsp

Exit code: 0 if no map has errors, 1 otherwise. Warnings don't change it.
"""

import argparse
import glob
import os
import lzma
import struct
import sys

MIN_VERSION, MAX_VERSION = 19, 21
HEADER_LUMPS = 64

(LUMP_ENTITIES, LUMP_PLANES, LUMP_TEXDATA, LUMP_VERTEXES, LUMP_VISIBILITY, LUMP_NODES,
 LUMP_TEXINFO, LUMP_FACES, LUMP_LIGHTING, LUMP_OCCLUSION, LUMP_LEAFS) = range(11)
LUMP_EDGES, LUMP_SURFEDGES, LUMP_MODELS, LUMP_WORLDLIGHTS = 12, 13, 14, 15
LUMP_LEAFFACES, LUMP_LEAFBRUSHES, LUMP_BRUSHES, LUMP_BRUSHSIDES = 16, 17, 18, 19
LUMP_AREAS, LUMP_AREAPORTALS = 20, 21
LUMP_DISPINFO, LUMP_PHYSDISP = 26, 28
LUMP_VERTNORMALS, LUMP_VERTNORMALINDICES = 30, 31
LUMP_DISP_VERTS, LUMP_DISP_LIGHTMAP_SAMPLE_POSITIONS, LUMP_GAME_LUMP = 33, 34, 35
LUMP_LEAFWATERDATA, LUMP_PRIMITIVES, LUMP_PRIMVERTS, LUMP_PRIMINDICES = 36, 37, 38, 39
LUMP_CLIPPORTALVERTS, LUMP_TEXDATA_STRING_DATA, LUMP_TEXDATA_STRING_TABLE = 41, 43, 44
LUMP_OVERLAYS, LUMP_LEAFMINDISTTOWATER, LUMP_DISP_TRIS = 45, 46, 48
LUMP_WATEROVERLAYS, LUMP_LEAF_AMBIENT_INDEX_HDR, LUMP_LEAF_AMBIENT_INDEX = 50, 51, 52
LUMP_LIGHTING_HDR, LUMP_LEAF_AMBIENT_LIGHTING_HDR, LUMP_LEAF_AMBIENT_LIGHTING = 53, 55, 56
LUMP_FACES_HDR, LUMP_OVERLAY_FADES = 58, 60

# sizes as compiled from public/bspfile.h and public/gamebspfile.h
SIZE = dict(plane=20, vertex=12, edge=4, face=56, texinfo=72, texdata=32, node=32, leaf=32,
            leaf_v0=56, model=48, brush=12, brushside=8, area=8, areaportal=12, prim=10,
            primvert=12, leafwater=12, dispinfo=176, dispvert=20, disptri=2, overlay=352,
            wateroverlay=1120, overlayfade=8, occluder=40, occluder_v1=36, occluderpoly=12,
            ambientindex=4, ambientsample=28, lightcube=24, gamelump=16)
STATIC_PROP_SIZES = {4: 56, 5: 60, 6: 64, 7: 72, 9: 72, 10: 72, 11: 80}
STATIC_PROP_V10_21 = 76

MAX_MAP_CLUSTERS = 65536
MAX_AREA_STATE_BITS = 32 * 8
MAX_AREA_PORTAL_STATE_BITS = 24 * 8
MAX_MAP_DISPINFO = 2048
OVERLAY_BSP_FACE_COUNT, WATEROVERLAY_BSP_FACE_COUNT = 64, 256
SURF_NOLIGHT, SURF_BUMPLIGHT = 0x0400, 0x0800
NUM_BUMP_VECTS = 3
MAXLIGHTMAPS = 4
# MSurf_MaxLightmapSizeWithBorder: the limits including the border
MAX_BRUSH_LIGHTMAP_DIM, MAX_DISP_LIGHTMAP_DIM = 35, 128
GAMELUMP_STATIC_PROPS = struct.unpack('>i', b'sprp')[0]
GAMELUMP_DETAIL_PROPS = struct.unpack('>i', b'dprp')[0]
DETAIL_PROP_TYPE_MODEL, DETAIL_PROP_TYPE_SPRITE = 0, 1
DETAIL_PROP_TYPE_SHAPE_CROSS, DETAIL_PROP_TYPE_SHAPE_TRI = 2, 3


class MapError(Exception):
    pass


def records(data, size, fmt, what):
    if len(data) % size:
        raise MapError('%s: lump size %d is not a multiple of %d' % (what, len(data), size))
    return [struct.unpack_from(fmt, data, i) for i in range(0, len(data), size)]


def decompress(data):
    """Lumps can be LZMA compressed (Source's own header)."""
    if data[:4] != b'LZMA':
        return data
    actual, packed = struct.unpack_from('<II', data, 4)
    props = data[12:17]
    alone = props + struct.pack('<Q', actual) + data[17:17 + packed]
    return lzma.decompress(alone, format=lzma.FORMAT_ALONE)[:actual]


class Bsp:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.raw = f.read()
        if len(self.raw) < 8 + HEADER_LUMPS * 16 + 4:
            raise MapError('incomplete header')
        ident, self.version = struct.unpack_from('<4si', self.raw, 0)
        if ident != b'VBSP':
            raise MapError('not a VBSP file')
        if not MIN_VERSION <= self.version <= MAX_VERSION:
            raise MapError('unsupported version %d' % self.version)
        self.lumps = []
        for i in range(HEADER_LUMPS):
            ofs, length, ver, _ = struct.unpack_from('<iiii', self.raw, 8 + i * 16)
            if length and (length < 0 or ofs < 0 or ofs + length > len(self.raw)):
                raise MapError('lump %d is outside the file' % i)
            self.lumps.append((ofs, length, ver))
        self._cache = {}

    def lump(self, i):
        if i not in self._cache:
            ofs, length, _ = self.lumps[i]
            self._cache[i] = decompress(self.raw[ofs:ofs + length]) if length else b''
        return self._cache[i]

    def version_of(self, i):
        return self.lumps[i][2]


class Checker:
    def __init__(self, bsp, verbose):
        self.b = bsp
        self.verbose = verbose
        self.warnings = []
        self.stats = {}

    def fail(self, msg):
        raise MapError(msg)

    def warn(self, msg):
        self.warnings.append(msg)

    def run(self):
        self.load_basic()
        self.check_collision()
        self.check_render()
        self.check_displacements()
        self.check_game_lumps()
        self.check_overlays()

    # ---- lumps shared by the collision and render loaders --------------------------------
    def load_basic(self):
        b = self.b
        self.planes = records(b.lump(LUMP_PLANES), SIZE['plane'], '<3ffi', 'planes')
        self.vertexes = records(b.lump(LUMP_VERTEXES), SIZE['vertex'], '<3f', 'vertexes')
        self.edges = records(b.lump(LUMP_EDGES), SIZE['edge'], '<HH', 'edges')
        self.surfedges = records(b.lump(LUMP_SURFEDGES), 4, '<i', 'surfedges')
        self.texinfo = records(b.lump(LUMP_TEXINFO), SIZE['texinfo'], '<16fii', 'texinfo')
        self.texdata = records(b.lump(LUMP_TEXDATA), SIZE['texdata'], '<3fiiiii', 'texdata')
        faces_lump = LUMP_FACES
        self.faces = records(b.lump(faces_lump), SIZE['face'], '<HBBihhhh4BifiiiiiHHI', 'faces')
        self.faces_hdr = records(b.lump(LUMP_FACES_HDR), SIZE['face'], '<HBBihhhh4BifiiiiiHHI', 'faces (HDR)')
        self.nodes = records(b.lump(LUMP_NODES), SIZE['node'], '<iii3h3hHHhh', 'nodes')
        if b.version_of(LUMP_LEAFS) == 0:
            self.leafs = records(b.lump(LUMP_LEAFS), SIZE['leaf_v0'], '<ihh3h3hHHHHhh24x', 'leafs')
        else:
            self.leafs = records(b.lump(LUMP_LEAFS), SIZE['leaf'], '<ihh3h3hHHHHhh', 'leafs')
        self.models = records(b.lump(LUMP_MODELS), SIZE['model'], '<9fiii', 'models')
        self.stats.update(faces=len(self.faces), leafs=len(self.leafs), nodes=len(self.nodes))

    # ---- engine/cmodel_bsp.cpp ------------------------------------------------------------
    def check_collision(self):
        b = self.b
        strdata = b.lump(LUMP_TEXDATA_STRING_DATA)
        strtable = records(b.lump(LUMP_TEXDATA_STRING_TABLE), 4, '<i', 'texdata string table')
        if not self.texdata:
            self.fail('map has no textures')
        for i, td in enumerate(self.texdata):
            sid = td[3]
            if sid < 0 or sid >= len(strtable) or not 0 <= strtable[sid][0] < len(strdata):
                self.fail('bad texture name index %d (texdata %d)' % (sid, i))

        planes = len(self.planes)
        texinfo = len(self.texinfo)
        brushes = records(b.lump(LUMP_BRUSHES), SIZE['brush'], '<iii', 'brushes')
        sides = records(b.lump(LUMP_BRUSHSIDES), SIZE['brushside'], '<Hhhh', 'brush sides')
        for i, s in enumerate(sides):
            if s[0] >= planes or s[1] >= texinfo:
                self.fail('bad plane or texinfo in brush side %d' % i)
        for i, br in enumerate(brushes):
            # the engine keeps both as unsigned short
            first, num = br[0] & 0xffff, br[1] & 0xffff
            if first + num > len(sides):
                self.fail('bad side range in brush %d' % i)

        for i, n in enumerate(self.nodes):
            if not 0 <= n[0] < planes:
                self.fail('bad plane in node %d' % i)

        nleafs = len(self.leafs)
        leafbrushes = records(b.lump(LUMP_LEAFBRUSHES), 2, '<H', 'leaf brushes')
        areas = records(b.lump(LUMP_AREAS), SIZE['area'], '<ii', 'areas')
        areaportals = records(b.lump(LUMP_AREAPORTALS), SIZE['areaportal'], '<HHHHi', 'area portals')
        self.areas, self.areaportals = areas, areaportals
        nareas = len(areas)
        clusters = 0
        for i, lf in enumerate(self.leafs):
            area = (lf[2] & 0x1ff)
            if area & 0x100:
                area -= 0x200          # 9-bit signed bitfield
            if lf[11] + lf[12] > len(leafbrushes):
                self.fail('bad leaf brush range in leaf %d' % i)
            if area < 0 or (area > 0 and area >= nareas):
                self.fail('bad area in leaf %d' % i)
            clusters = max(clusters, lf[1] + 1)
        for i, lb in enumerate(leafbrushes):
            if lb[0] >= len(brushes):
                self.fail('bad leaf brush %d' % i)
        for i, n in enumerate(self.nodes):
            for child in n[1:3]:
                if (child >= len(self.nodes)) if child >= 0 else (-1 - child >= nleafs):
                    self.fail('bad child in node %d' % i)
        for i, m in enumerate(self.models):
            head = m[9]
            if (head >= len(self.nodes)) if head >= 0 else (-1 - head >= nleafs):
                self.fail('bad head node in model %d' % i)

        # trees: one parent per node, every node reachable from a root
        parents = [0] * len(self.nodes)
        for n in self.nodes:
            for child in n[1:3]:
                if child >= 0:
                    parents[child] += 1
                    if parents[child] > 1:
                        self.fail('node %d has more than one parent' % child)
        visited = 0
        for root in range(len(self.nodes)):
            if parents[root]:
                continue
            stack = [root]
            while stack:
                node = stack.pop()
                visited += 1
                stack.extend(c for c in self.nodes[node][1:3] if c >= 0)
        if visited != len(self.nodes):
            self.fail('nodes contain a loop')

        nportalopen = len(areaportals) + 1
        for i, a in enumerate(areas):
            if a[1] < 0 or a[0] < 0 or a[1] + a[0] > nportalopen:
                self.fail('bad area portal range in area %d' % i)
        for i, p in enumerate(areaportals):
            if p[0] >= nportalopen or (nareas and p[1] >= nareas):
                self.fail('bad area portal %d' % i)

        vis = b.lump(LUMP_VISIBILITY)
        if vis:
            if len(vis) < 4:
                self.fail('bad visibility header')
            nclusters = struct.unpack_from('<i', vis)[0]
            if nclusters < 0 or nclusters > MAX_MAP_CLUSTERS or 4 + nclusters * 8 > len(vis):
                self.fail('bad visibility header')
            for c in range(nclusters):
                for ofs in struct.unpack_from('<ii', vis, 4 + c * 8):
                    if not 0 <= ofs < len(vis):
                        self.fail('bad visibility offset for cluster %d' % c)
            if nclusters < clusters:
                self.warn('leafs use %d clusters, visibility has %d (those see everything)' % (clusters, nclusters))
        self.stats['clusters'] = clusters

    # ---- engine/modelloader.cpp -----------------------------------------------------------
    def check_render(self):
        b = self.b
        nverts = len(self.vertexes)
        for i, se in enumerate(self.surfedges):
            e = se[0]
            idx = 1 if e < 0 else 0
            e = -e if e < 0 else e
            if e < 0 or e >= len(self.edges) or self.edges[e][idx] >= nverts:
                self.fail('bad edge %d in surfedge %d' % (se[0], i))

        prims = records(b.lump(LUMP_PRIMITIVES), SIZE['prim'], '<BxHHHH', 'primitives')
        nprimverts = len(b.lump(LUMP_PRIMVERTS)) // SIZE['primvert']
        primindices = records(b.lump(LUMP_PRIMINDICES), 2, '<H', 'primitive indices')

        # the engine uses the HDR faces if the map has them and HDR is on; check both
        for faces, name in ((self.faces, 'face'), (self.faces_hdr, 'HDR face')):
            for i, f in enumerate(faces):
                planenum, firstedge, numedges, ti = f[0], f[3], f[4], f[5]
                numprims, firstprim = f[19] & 0x7fff, f[20]
                if (firstedge < 0 or numedges < 0 or numedges > 255 or firstedge + numedges > len(self.surfedges)
                        or planenum >= len(self.planes) or firstprim + numprims > len(prims)):
                    self.fail('bad %s %d' % (name, i))
                if not 0 <= ti < len(self.texinfo):
                    self.fail('bad texinfo in %s %d' % (name, i))
                for p in prims[firstprim:firstprim + numprims]:
                    _, first_index, index_count, first_vert, vert_count = p
                    if first_index + index_count > len(primindices) or first_vert + vert_count > nprimverts:
                        self.fail('bad primitive for %s %d' % (name, i))
                    for (index,) in primindices[first_index:first_index + index_count]:
                        bad = (not first_vert <= index < first_vert + vert_count) if vert_count else index >= numedges
                        if bad:
                            self.fail('bad primitive index for %s %d' % (name, i))
        self.check_face_lighting(self.faces, b.lump(LUMP_LIGHTING), 'LDR')
        if self.faces_hdr or b.lump(LUMP_LIGHTING_HDR):
            self.check_face_lighting(self.faces_hdr or self.faces, b.lump(LUMP_LIGHTING_HDR), 'HDR')

        normals = len(b.lump(LUMP_VERTNORMALS)) // 12
        nidx = records(b.lump(LUMP_VERTNORMALINDICES), 2, '<H', 'vertex normal indices')
        if any(n[0] >= normals for n in nidx):
            self.fail('bad vertex normal index')
        if sum(f[4] for f in self.faces) > len(nidx):
            self.fail('too few vertex normal indices')

        nsurf = len(self.faces)
        for i, n in enumerate(self.nodes):
            if n[9] + n[10] > nsurf:
                self.fail('bad face range in node %d' % i)
        for i, m in enumerate(self.models):
            if m[10] < 0 or m[11] < 0 or m[10] + m[11] > nsurf:
                self.fail('bad face range in model %d' % i)

        leaffaces = records(b.lump(LUMP_LEAFFACES), 2, '<H', 'leaf faces')
        if any(lf[0] >= nsurf for lf in leaffaces):
            self.fail('bad leaf face')
        for i, lf in enumerate(self.leafs):
            if lf[9] + lf[10] > len(leaffaces):
                self.fail('bad leaf face range in leaf %d' % i)

        if b.version_of(LUMP_LEAFS) == 1:
            for samples_lump, index_lump, name in ((LUMP_LEAF_AMBIENT_LIGHTING, LUMP_LEAF_AMBIENT_INDEX, 'LDR'),
                                                   (LUMP_LEAF_AMBIENT_LIGHTING_HDR, LUMP_LEAF_AMBIENT_INDEX_HDR, 'HDR')):
                table = b.lump(index_lump)
                if b.version_of(samples_lump) != 1 or not table:
                    continue       # converted from the old format: no indices
                nsamples = len(b.lump(samples_lump)) // SIZE['ambientsample']
                entries = records(table, SIZE['ambientindex'], '<HH', 'leaf ambient index')
                if len(entries) < len(self.leafs):
                    self.fail('bad %s leaf ambient lighting' % name)
                for count, first in entries[:len(self.leafs)]:
                    # a leaf without samples borrows them from the leaf in "first"
                    if (first + count > nsamples) if count else (first >= len(self.leafs)):
                        self.fail('bad %s leaf ambient lighting' % name)

        water = records(b.lump(LUMP_LEAFWATERDATA), SIZE['leafwater'], '<ffhh', 'leaf water data')
        for w in water:
            if w[2] < -1 or w[2] >= len(self.texinfo):
                self.fail('bad texinfo in leaf water data')
        bad_water = sum(1 for lf in self.leafs if lf[13] >= len(water))
        if bad_water:
            self.warn('%d leafs reference missing water data (treated as dry)' % bad_water)

        clipverts = len(b.lump(LUMP_CLIPPORTALVERTS)) // 12
        if len(self.areas) > MAX_AREA_STATE_BITS:
            self.fail('too many areas')
        for i, a in enumerate(self.areas):
            if a[1] + a[0] > len(self.areaportals):
                self.fail('bad area portal range in area %d (render)' % i)
        for i, p in enumerate(self.areaportals):
            key, other, firstclip, nclip, planenum = p
            if (key >= MAX_AREA_PORTAL_STATE_BITS or other >= len(self.areas) or not 0 <= planenum < len(self.planes)
                    or firstclip + nclip > clipverts):
                self.fail('bad area portal %d (render)' % i)
        self.check_occlusion()

    def check_face_lighting(self, faces, light, name):
        """The engine drops the lightmap of faces whose samples are outside the lump."""
        dropped = 0
        for i, f in enumerate(faces):
            lightofs = f[12]
            styles = f[8:12]
            if lightofs == -1 or not light:
                continue
            ti = self.texinfo[f[5]]
            flags = ti[16]
            extents = (f[16], f[17])
            if not flags & SURF_NOLIGHT:
                limit = MAX_DISP_LIGHTMAP_DIM if f[6] != -1 else MAX_BRUSH_LIGHTMAP_DIM
                if extents[0] > limit or extents[1] > limit:
                    self.fail('bad lightmap extents %dx%d in %s face %d (the engine quits with "Bad surface extents")'
                              % (extents[0], extents[1], name, i))
            luxels = (extents[0] + 1) * (extents[1] + 1)
            if flags & SURF_BUMPLIGHT and not flags & SURF_NOLIGHT:
                luxels *= NUM_BUMP_VECTS + 1
            nstyles = sum(1 for s in styles if s != 255)
            if lightofs < nstyles * 4 or lightofs + luxels * max(nstyles, 1) * 4 > len(light):
                dropped += 1
        if dropped:
            self.warn('%d faces have %s lightmap data outside the lump (drawn without lightmap)' % (dropped, name))

    def check_occlusion(self):
        data = self.b.lump(LUMP_OCCLUSION)
        ver = self.b.version_of(LUMP_OCCLUSION)
        if not data or ver == 0:
            return
        if ver not in (1, 2):
            self.fail('unknown occlusion lump version %d' % ver)
        pos = 0

        def count(elem):
            nonlocal pos
            if pos + 4 > len(data):
                self.fail('bad occlusion data')
            n = struct.unpack_from('<i', data, pos)[0]
            pos += 4
            if n < 0 or pos + n * elem > len(data):
                self.fail('bad occlusion data')
            start = pos
            pos += n * elem
            return n, start

        occ_size = SIZE['occluder'] if ver == 2 else SIZE['occluder_v1']
        nocc, occ_start = count(occ_size)
        npoly, poly_start = count(SIZE['occluderpoly'])
        nvi, vi_start = count(4)
        for i in range(nocc):
            flags, first, num = struct.unpack_from('<iii', data, occ_start + i * occ_size)
            area = struct.unpack_from('<i', data, occ_start + i * occ_size + 36)[0] if ver == 2 else 1
            if first < 0 or num < 0 or first + num > npoly or not 0 <= area < MAX_AREA_STATE_BITS:
                self.fail('bad occluder %d' % i)
        for i in range(npoly):
            first, num, planenum = struct.unpack_from('<iii', data, poly_start + i * 12)
            if first < 0 or num < 0 or first + num > nvi or not 0 <= planenum < len(self.planes):
                self.fail('bad occluder polygon %d' % i)
        for i in range(nvi):
            if not 0 <= struct.unpack_from('<i', data, vi_start + i * 4)[0] < len(self.vertexes):
                self.fail('bad occluder vertex %d' % i)

    # ---- engine/cmodel_bsp.cpp, engine/disp_mapload.cpp, engine/disp.cpp, cmodel_disp.cpp --
    def check_displacements(self):
        b = self.b
        data = b.lump(LUMP_DISPINFO)
        if len(data) % SIZE['dispinfo']:
            self.fail('dispinfo: bad lump size')
        n = len(data) // SIZE['dispinfo']
        self.stats['displacements'] = n
        if not n:
            return
        if n > MAX_MAP_DISPINFO:
            self.fail('too many displacements')
        disp_faces = {i for i, f in enumerate(self.faces) if f[6] != -1}
        used = set()
        vert_total = tri_total = 0
        for i in range(n):
            base = i * SIZE['dispinfo']
            power = struct.unpack_from('<i', data, base + 20)[0]
            face = struct.unpack_from('<H', data, base + 36)[0]
            if not 2 <= power <= 4:
                self.fail('invalid displacement power %d' % power)
            if face >= len(self.faces) or face not in disp_faces:
                self.fail('invalid parent face %d for displacement %d' % (face, i))
            if face in used:
                self.fail('face %d has more than one displacement' % face)
            used.add(face)
            f = self.faces[face]
            if f[4] <= 4 and (f[3] < 0 or f[3] + f[4] > len(self.surfedges)):
                self.fail('bad face data for displacement %d' % i)
            side = (1 << power) + 1
            vert_total += side * side
            tri_total += 2 * (1 << power) * (1 << power)
        if disp_faces - used:
            self.fail('face %d has no displacement' % min(disp_faces - used))
        if vert_total * SIZE['dispvert'] > len(b.lump(LUMP_DISP_VERTS)):
            self.fail('displacement vertices are outside the lump')
        if tri_total * SIZE['disptri'] > len(b.lump(LUMP_DISP_TRIS)):
            self.fail('displacement triangles are outside the lump')

        phys = b.lump(LUMP_PHYSDISP)
        if phys:
            ok = len(phys) >= 2
            if ok:
                count = struct.unpack_from('<H', phys)[0]
                ok = count == n and 2 + count * 2 <= len(phys)
                if ok:
                    sizes = struct.unpack_from('<%dH' % count, phys, 2)
                    ok = 2 + count * 2 + sum(s for s in sizes if s != 0xffff) <= len(phys)
            if not ok:
                self.warn('invalid displacement collision data (ignored, built at runtime)')

    # ---- engine/staticpropmgr.cpp, game/client/detailobjectsystem.cpp ----------------------
    def check_game_lumps(self):
        data = self.b.lump(LUMP_GAME_LUMP)
        if not data:
            return
        count = struct.unpack_from('<i', data)[0]
        if count < 0 or 4 + count * SIZE['gamelump'] > len(data):
            self.fail('bad game lump directory')
        file_ofs = self.b.lumps[LUMP_GAME_LUMP][0]
        for i in range(count):
            gid, flags, ver, ofs, length = struct.unpack_from('<iHHii', data, 4 + i * 16)
            if flags & 1:
                self.warn('compressed game lump %d not checked' % gid)
                continue
            # offsets are relative to the file
            if length and (ofs < 0 or ofs + length > len(self.b.raw)):
                self.fail('game lump %d is outside the file' % gid)
            body = self.b.raw[ofs:ofs + length]
            if gid == GAMELUMP_STATIC_PROPS:
                self.check_static_props(body, ver)
            elif gid == GAMELUMP_DETAIL_PROPS:
                self.check_detail_props(body, ver)
        del file_ofs

    def check_static_props(self, body, ver):
        pos = 0

        def count(elem):
            nonlocal pos
            n = struct.unpack_from('<i', body, pos)[0] if pos + 4 <= len(body) else -1
            pos += 4
            if n < 0 or pos + n * elem > len(body):
                self.fail('invalid static prop data (count)')
            start = pos
            pos += n * elem
            return n, start

        ndict, _ = count(128)
        nleaves, leaf_start = count(2)
        for i in range(nleaves):
            if struct.unpack_from('<H', body, leaf_start + i * 2)[0] >= len(self.leafs):
                self.fail('invalid static prop data (leaf %d)' % i)
        if ver < 4:
            self.warn('really old static prop format (not loaded)')
            return
        if ver not in STATIC_PROP_SIZES:
            self.warn('unknown static prop lump version %d (not loaded)' % ver)
            return
        size = STATIC_PROP_V10_21 if ver in (7, 10) and self.b.version == 21 else STATIC_PROP_SIZES[ver]
        nprops, start = count(size)
        for i in range(nprops):
            ptype, first, num = struct.unpack_from('<HHH', body, start + i * size + 24)
            if ptype >= ndict or first + num > nleaves:
                self.fail('invalid static prop data (prop %d)' % i)
        self.stats['static props'] = nprops

    def check_detail_props(self, body, ver):
        if ver < 4:
            return
        pos = 0

        def count(elem):
            nonlocal pos
            n = struct.unpack_from('<i', body, pos)[0] if pos + 4 <= len(body) else -1
            pos += 4
            if n < 0 or pos + n * elem > len(body):
                return None, None
            start = pos
            pos += n * elem
            return n, start

        nmodels, _ = count(128)
        if nmodels is None:
            self.warn('invalid detail prop data (detail props not loaded)')
            return
        if ver != 4:
            return
        nsprites, _ = count(32)
        nobjects, start = count(52) if nsprites is not None else (None, None)
        if nobjects is None:
            self.warn('invalid detail prop data (detail props not loaded)')
            return
        for i in range(nobjects):
            model, leaf = struct.unpack_from('<HH', body, start + i * 52 + 24)
            ptype = body[start + i * 52 + 44]
            bad = leaf >= len(self.leafs)
            if ptype == DETAIL_PROP_TYPE_MODEL:
                bad = bad or model >= nmodels
            elif ptype in (DETAIL_PROP_TYPE_SPRITE, DETAIL_PROP_TYPE_SHAPE_CROSS, DETAIL_PROP_TYPE_SHAPE_TRI):
                bad = bad or model >= nsprites
            if bad:
                self.warn('invalid detail prop %d (detail props not loaded)' % i)
                return
        self.stats['detail props'] = nobjects

    # ---- engine/Overlay.cpp ---------------------------------------------------------------
    def check_overlays(self):
        b = self.b
        for lump, size, maxfaces, name in ((LUMP_OVERLAYS, SIZE['overlay'], OVERLAY_BSP_FACE_COUNT, 'overlay'),
                                           (LUMP_WATEROVERLAYS, SIZE['wateroverlay'], WATEROVERLAY_BSP_FACE_COUNT, 'water overlay')):
            data = b.lump(lump)
            if len(data) % size:
                self.warn('%s lump has a bad size (overlays not loaded)' % name)
                return
            for i in range(len(data) // size):
                texinfo, facecount = struct.unpack_from('<hH', data, i * size + 4)
                facecount &= 0x3fff
                faces = struct.unpack_from('<%di' % min(facecount, maxfaces), data, i * size + 8)
                if (not 0 <= texinfo < len(self.texinfo) or facecount > maxfaces
                        or any(not 0 <= f < len(self.faces) for f in faces)):
                    self.warn('invalid %s %d (overlays not loaded)' % (name, i))
                    return


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('maps', nargs='+', help='.bsp files, masks (*.bsp) or folders')
    parser.add_argument('-v', '--verbose', action='store_true', help='print statistics')
    args = parser.parse_args()

    # the Windows command line doesn't expand masks, and a folder means all maps in it
    paths = []
    for arg in args.maps:
        if os.path.isdir(arg):
            paths += sorted(glob.glob(os.path.join(arg, '*.bsp')))
        elif glob.has_magic(arg):
            found = sorted(glob.glob(arg))
            if not found:
                print('%s: no maps found' % arg)
            paths += found
        else:
            paths.append(arg)

    failed = 0
    for path in paths:
        try:
            checker = Checker(Bsp(path), args.verbose)
            checker.run()
        except MapError as e:
            print('%s: ERROR: %s' % (path, e))
            failed += 1
            continue
        except (OSError, struct.error, lzma.LZMAError) as e:
            print('%s: ERROR: cannot read the map: %s' % (path, e))
            failed += 1
            continue
        status = 'OK' if not checker.warnings else 'OK with warnings'
        print('%s: %s' % (path, status))
        for w in checker.warnings:
            print('  warning: %s' % w)
        if args.verbose:
            print('  ' + ', '.join('%s %d' % kv for kv in sorted(checker.stats.items())))
    if len(paths) > 1:
        print('%d maps checked, %d with errors' % (len(paths), failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
