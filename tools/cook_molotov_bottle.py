#!/usr/bin/env python3
"""Cook the molotov's bottle out of the supplied Quequis House GLB.

Run from the repository root:

    python3 tools/cook_molotov_bottle.py \\
      assets/models/buildings/churchofwaffles/source/Quequis_House/Models/Quequis_House.glb

Writes src/app/molotov_bottle_mesh.h, which is CHECKED IN. That is the whole
point of this cooker and it is worth stating plainly:

*   The supplied archive lives under `assets/models/`, which `.gitignore`
    excludes, because the source models are proprietary and stay local. Every
    other import in this tree therefore cooks to an ignored `.emesh` beside its
    source, and the game reads that file at runtime.
*   A bottle is fifty triangles. Emitting it as a header costs eight kilobytes
    of tracked source, needs no runtime file, no missing-asset fallback and no
    loader, and a headless test can build the exact geometry the renderer
    uploads. `src/city/map.h` makes the same argument for the road network, and
    `src/app/` already holds hand-written prop meshes (`snowplow_mesh.h`,
    `road_sign_mesh.h`, `traffic_signal_mesh.h`) in exactly this shape.

**THE TEXTURE IS DELIBERATELY NOT COOKED.** The source material is a printed
soda label, and its image is part of the private archive: extracting it to a
tracked path would push supplied asset content into git, which is the one thing
the ignore rule exists to stop. The geometry is the part that was wanted — a
contoured glass bottle silhouette nothing in this tree had — and the app paints
it flat bottle-green. `tools/cook_church_of_waffles.py` drops four textures for
a related reason and says so in the same terms.

WHICH BOTTLE, AND WHY `Soda_01`. The Quequis kitchen holds eight lathe-turned
bottles (`Soda` .. `Soda_07`) plus jars and squeeze bottles. Six of the eight
are straight cylinders with a neck stuck on top, which read as plastic. Soda_01
is the only one with a CONTOUR: it bulges at the base, pulls in to a waist and
bulges again at the shoulder, which is the profile the eye reads as thick glass
from across a street. That is the whole reason a specific mesh is named here
rather than "the first bottle in the file".

The emitted mesh is the source geometry with its node SCALE applied and nothing
else: the node's quarter-turn about Y and its position in the restaurant are
dropped, the axis is recentred on x = z = 0, and the base is dropped to y = 0.
A thrown prop wants its own origin, not the one it had on a shelf.
"""
import hashlib
import json
import math
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'src/app/molotov_bottle_mesh.h'
MESH_NAME = 'Soda_01'

COMPONENT = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2),
             5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
COUNT = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}


def load_glb(path):
    blob = path.read_bytes()
    if blob[:4] != b'glTF':
        raise SystemExit(f'{path} is not a binary glTF')
    offset, meta, binary = 12, None, None
    while offset < len(blob):
        length, kind = struct.unpack_from('<II', blob, offset)
        offset += 8
        if kind == 0x4E4F534A:
            meta = json.loads(blob[offset:offset + length].decode('utf-8'))
        elif kind == 0x004E4942:
            binary = blob[offset:offset + length]
        offset += length
    if meta is None or binary is None:
        raise SystemExit('GLB is missing its JSON or BIN chunk')
    return meta, binary


def accessor(meta, binary, index):
    spec = meta['accessors'][index]
    view = meta['bufferViews'][spec['bufferView']]
    fmt, size = COMPONENT[spec['componentType']]
    lanes = COUNT[spec['type']]
    base = view.get('byteOffset', 0) + spec.get('byteOffset', 0)
    stride = view.get('byteStride') or size * lanes
    return [struct.unpack_from('<' + fmt * lanes, binary, base + i * stride)
            for i in range(spec['count'])]


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    source = Path(sys.argv[1])
    meta, binary = load_glb(source)

    index = next((i for i, m in enumerate(meta['meshes'])
                  if m.get('name') == MESH_NAME), None)
    if index is None:
        raise SystemExit(f'no mesh named {MESH_NAME} in {source}')
    primitives = meta['meshes'][index]['primitives']
    if len(primitives) != 1:
        raise SystemExit(f'{MESH_NAME} has {len(primitives)} primitives; expected 1')
    primitive = primitives[0]

    positions = accessor(meta, binary, primitive['attributes']['POSITION'])
    normals = accessor(meta, binary, primitive['attributes']['NORMAL'])
    indices = [v[0] for v in accessor(meta, binary, primitive['indices'])]
    if len(positions) != len(normals):
        raise SystemExit('POSITION and NORMAL disagree on vertex count')

    node = next(n for n in meta['nodes'] if n.get('mesh') == index)
    scale = node.get('scale', [1.0, 1.0, 1.0])
    if not (math.isclose(scale[0], scale[1], rel_tol=1e-6) and
            math.isclose(scale[1], scale[2], rel_tol=1e-6)):
        # A non-uniform node scale would need the inverse transpose on the
        # normals. The supplied bottle is uniform; refuse rather than emit
        # normals that are quietly wrong on a re-cook of a different mesh.
        raise SystemExit(f'{MESH_NAME} has a non-uniform node scale {scale}')
    factor = scale[0]

    scaled = [(p[0] * factor, p[1] * factor, p[2] * factor) for p in positions]
    min_y = min(p[1] for p in scaled)
    mid_x = (min(p[0] for p in scaled) + max(p[0] for p in scaled)) * .5
    mid_z = (min(p[2] for p in scaled) + max(p[2] for p in scaled)) * .5
    placed = [(p[0] - mid_x, p[1] - min_y, p[2] - mid_z) for p in scaled]
    height = max(p[1] for p in placed)
    radius = max(math.hypot(p[0], p[2]) for p in placed)

    lines = []
    for (px, py, pz), (nx, ny, nz) in zip(placed, normals):
        length = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
        lines.append('    {{{:.5f}f,{:.5f}f,{:.5f}f, {:.4f}f,{:.4f}f,{:.4f}f}},'
                     .format(px, py, pz, nx / length, ny / length, nz / length))

    triangles = [', '.join(str(v) for v in indices[i:i + 12])
                 for i in range(0, len(indices), 12)]

    header = f'''#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "gfx/primitives.h"

namespace apricot {{

// The molotov's bottle, cooked out of the supplied Quequis House GLB by
// tools/cook_molotov_bottle.py. GENERATED — edit the cooker, not this file.
//
// Source mesh `{MESH_NAME}`, one of the eight lathe-turned bottles on the
// Quequis kitchen shelves and the only one with a contour rather than a
// straight cylinder: it bulges at the base, pulls in to a waist and bulges
// again at the shoulder. That profile is what reads as thick glass at throwing
// distance, and it is why this mesh is named rather than picked.
//
// Emitted with the source node's uniform scale applied and NOTHING else. The
// quarter-turn it had on its shelf and its position in the restaurant are
// dropped, the axis is recentred on x = z = 0 and the base sits at y = 0,
// because a thrown prop owns its own origin. Source metre scale is retained:
// {height:.3f} m tall, {radius * 2:.3f} m across the body.
//
// UNTEXTURED ON PURPOSE. The source material is a printed soda label whose
// image belongs to the private archive under the ignored assets/models/ tree;
// cooking it to a tracked path would push supplied asset content into git. The
// silhouette was the part that was wanted. The caller paints it bottle green.
inline constexpr float kMolotovBottleSourceHeightM = {height:.5f}f;
inline constexpr float kMolotovBottleSourceRadiusM = {radius:.5f}f;

struct MolotovBottleVertex {{
    float px, py, pz;
    float nx, ny, nz;
}};

inline constexpr std::array<MolotovBottleVertex, {len(placed)}> kMolotovBottleVertices{{{{
{chr(10).join(lines)}
}}}};

inline constexpr std::array<uint32_t, {len(indices)}> kMolotovBottleIndices{{{{
    {(chr(10) + '    ').join(t + ',' for t in triangles)}
}}}};

// Upload-ready geometry, base at the origin and the neck up local +Y. Scale it
// by the caller: the cooked bottle keeps the source's metre scale, which is a
// litre bottle, and a molotov is a smaller one. app/molotov_visual.cpp does the
// rescaling and says what it picked and why.
inline MeshData make_molotov_bottle() {{
    MeshData mesh;
    mesh.vertices.reserve(kMolotovBottleVertices.size());
    mesh.indices.assign(kMolotovBottleIndices.begin(), kMolotovBottleIndices.end());
    // These primitives are not terrain, and the vertex shader zeroes the splat
    // weights for every non-terrain material anyway; the channel is picked
    // rather than left uninitialised for the same reason make_box() picks one.
    constexpr glm::vec4 solid{{1.f, 0.f, 0.f, 0.f}};
    for (const auto& v : kMolotovBottleVertices) {{
        const glm::vec3 position{{v.px, v.py, v.pz}};
        // The lathe's own UVs addressed the source label atlas, which is not
        // cooked. A cylindrical unwrap keeps the field meaningful for any
        // later tiling material without pretending the old atlas still exists.
        const glm::vec2 uv{{
            .5f + std::atan2(position.z, position.x) / (2.f * 3.14159265f),
            position.y / kMolotovBottleSourceHeightM}};
        mesh.vertices.push_back({{position, glm::vec3{{v.nx, v.ny, v.nz}}, uv, solid}});
        mesh.bounds.expand(position);
    }}
    return mesh;
}}

}}  // namespace apricot
'''
    OUT.write_text(header)
    print(json.dumps(dict(
        source=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        mesh=MESH_NAME, vertices=len(placed), triangles=len(indices) // 3,
        node_scale=factor, height_m=round(height, 5),
        body_diameter_m=round(radius * 2, 5), output=str(OUT.relative_to(ROOT))),
        indent=2))


if __name__ == '__main__':
    main()
