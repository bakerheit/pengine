#include "render/model.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "core/emesh_format.h"
#include "core/log.h"

namespace pengine {

bool Model::load(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { PE_ERROR("Model: cannot open %s", path.c_str()); return false; }

    EmeshHeader hdr{};
    if (std::fread(&hdr, sizeof(hdr), 1, f) != 1) {
        PE_ERROR("Model: truncated header in %s", path.c_str());
        std::fclose(f); return false;
    }
    if (hdr.magic != EMESH_MAGIC || hdr.version != EMESH_VERSION) {
        PE_ERROR("Model: bad magic/version in %s", path.c_str());
        std::fclose(f); return false;
    }

    std::vector<EmeshVertex>  raw_verts(hdr.vertex_count);
    std::vector<uint32_t>     raw_idx(hdr.index_count);
    std::vector<EmeshSubmesh> raw_subs(hdr.submesh_count);
    std::vector<char>         string_block(hdr.string_block_size);

    auto rd = [&](void* dst, std::size_t n) {
        return n == 0 || std::fread(dst, n, 1, f) == 1;
    };
    bool ok =
        rd(raw_verts.data(),  raw_verts.size()  * sizeof(EmeshVertex)) &&
        rd(raw_idx.data(),    raw_idx.size()    * sizeof(uint32_t))    &&
        rd(raw_subs.data(),   raw_subs.size()   * sizeof(EmeshSubmesh)) &&
        rd(string_block.data(), string_block.size());
    std::fclose(f);
    if (!ok) { PE_ERROR("Model: truncated data in %s", path.c_str()); return false; }

    // Convert EmeshVertex → engine Vertex and upload per-submesh.
    submeshes_.resize(hdr.submesh_count);
    for (uint32_t si = 0; si < hdr.submesh_count; ++si) {
        const EmeshSubmesh& es = raw_subs[si];

        // Slice the vertex range referenced by this submesh's indices.
        // Indices may reference any vertex; just rebuild a local vertex list.
        uint32_t idx_end = es.index_offset + es.index_count;
        uint32_t vmin = hdr.vertex_count, vmax = 0;
        for (uint32_t ii = es.index_offset; ii < idx_end; ++ii) {
            if (raw_idx[ii] < vmin) vmin = raw_idx[ii];
            if (raw_idx[ii] > vmax) vmax = raw_idx[ii];
        }

        std::vector<Vertex> verts(vmax - vmin + 1);
        for (uint32_t vi = vmin; vi <= vmax; ++vi) {
            const EmeshVertex& ev = raw_verts[vi];
            Vertex& v = verts[vi - vmin];
            v.position = {ev.px, ev.py, ev.pz};
            v.normal   = {ev.nx, ev.ny, ev.nz};
            v.uv       = {ev.u,  ev.v};
            v.tangent  = {ev.tx, ev.ty, ev.tz, ev.tw};
        }

        std::vector<uint32_t> local_idx(es.index_count);
        for (uint32_t ii = 0; ii < es.index_count; ++ii)
            local_idx[ii] = raw_idx[es.index_offset + ii] - vmin;

        submeshes_[si].mesh.upload(verts, local_idx);

        if (es.material_name_offset < hdr.string_block_size)
            submeshes_[si].material_name = string_block.data() + es.material_name_offset;
        else
            submeshes_[si].material_name = "default";
    }

    PE_INFO("Model loaded: %s  (%u submeshes, %u verts, %u idx)",
            path.c_str(), hdr.submesh_count, hdr.vertex_count, hdr.index_count);
    return true;
}

void Model::draw() const {
    for (const auto& sub : submeshes_)
        sub.mesh.draw();
}

} // namespace pengine
