// meshconv — converts OBJ/FBX/glTF to .emesh + .eskel + .eanim binary formats.
// Links Assimp; must never be linked into the engine runtime.
//
// Usage: meshconv <input> <output_basename>
//   Writes <basename>.emesh, and if rigged: <basename>.eskel + <basename>.eanim

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/anim_format.h"
#include "core/emesh_format.h"
#include "core/skeleton_format.h"

using namespace pengine;

static void die(const char* msg) {
    std::fprintf(stderr, "meshconv error: %s\n", msg);
    std::exit(1);
}

// aiMatrix4x4 is row-major. glm/OpenGL matrices are column-major.
// Copy + transpose into a 16-float column-major buffer.
static void aimat_to_colmajor(const aiMatrix4x4& m, float out[16]) {
    out[0]  = m.a1; out[1]  = m.b1; out[2]  = m.c1; out[3]  = m.d1;
    out[4]  = m.a2; out[5]  = m.b2; out[6]  = m.c2; out[7]  = m.d2;
    out[8]  = m.a3; out[9]  = m.b3; out[10] = m.c3; out[11] = m.d3;
    out[12] = m.a4; out[13] = m.b4; out[14] = m.c4; out[15] = m.d4;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: meshconv <input> <output_basename>\n");
        return 1;
    }
    const char* in_path     = argv[1];
    std::string out_base    = argv[2];
    // Strip a trailing .emesh if the user passed one — for backward compat
    // with existing build invocations.
    if (out_base.size() > 6 &&
        out_base.compare(out_base.size() - 6, 6, ".emesh") == 0)
        out_base.resize(out_base.size() - 6);

    Assimp::Importer imp;
    const aiScene* scene = imp.ReadFile(in_path,
        aiProcess_Triangulate       |
        aiProcess_GenSmoothNormals  |
        aiProcess_CalcTangentSpace  |
        aiProcess_JoinIdenticalVertices |
        aiProcess_LimitBoneWeights  |  // cap to 4 bones per vertex
        aiProcess_PreTransformVertices | // bake node transforms into static
                                         // mesh vertices; Assimp safely skips
                                         // this for skinned meshes (bone
                                         // hierarchy needs to stay intact).
                                         // Without it, multi-submesh static
                                         // models like the Glock 17 collapse
                                         // each submesh to its node-local
                                         // origin and the slide/barrel/grip
                                         // pile up at the wrong positions.
        aiProcess_SortByPType);

    // Anim-only FBXs (Mixamo "in-place animation" exports) carry no mesh
    // and so trip AI_SCENE_FLAGS_INCOMPLETE — that's fine, we just emit
    // the .eanim and skip the .emesh / .eskel writers below. Hard fail
    // only if Assimp returned nothing at all or no root node.
    if (!scene || !scene->mRootNode) {
        std::fprintf(stderr, "meshconv: Assimp error: %s\n", imp.GetErrorString());
        return 1;
    }

    std::printf("meshconv: scene meshes=%u  animations=%u\n",
                scene->mNumMeshes, scene->mNumAnimations);
    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* m = scene->mMeshes[mi];
        std::printf("  mesh[%u]: '%s' verts=%u faces=%u bones=%u\n",
                    mi, m->mName.C_Str(), m->mNumVertices, m->mNumFaces, m->mNumBones);
    }
    for (unsigned int ai = 0; ai < scene->mNumAnimations; ++ai) {
        const aiAnimation* a = scene->mAnimations[ai];
        std::printf("  anim[%u]: '%s' duration=%.4f tps=%.2f channels=%u\n",
                    ai, a->mName.C_Str(), a->mDuration, a->mTicksPerSecond,
                    a->mNumChannels);
        unsigned int sample_n = a->mNumChannels < 3 ? a->mNumChannels : 3;
        for (unsigned int ci = 0; ci < sample_n; ++ci) {
            const aiNodeAnim* na = a->mChannels[ci];
            std::printf("    ch[%u] '%s' pos=%u rot=%u scl=%u",
                        ci, na->mNodeName.C_Str(),
                        na->mNumPositionKeys, na->mNumRotationKeys, na->mNumScalingKeys);
            if (na->mNumRotationKeys > 0) {
                auto& f = na->mRotationKeys[0];
                auto& l = na->mRotationKeys[na->mNumRotationKeys - 1];
                std::printf("  rot[0].t=%.4f rot[last].t=%.4f", f.mTime, l.mTime);
            }
            std::printf("\n");
        }
    }

    // ----- Build a single global bone table from all meshes -----
    std::map<std::string, uint32_t> bone_index;
    std::vector<aiMatrix4x4>        bone_inv_bind;  // index = bone_index[name]
    auto get_or_add_bone = [&](const aiBone* b) -> uint32_t {
        auto it = bone_index.find(b->mName.C_Str());
        if (it != bone_index.end()) return it->second;
        uint32_t idx = static_cast<uint32_t>(bone_inv_bind.size());
        bone_index[b->mName.C_Str()] = idx;
        bone_inv_bind.push_back(b->mOffsetMatrix);
        return idx;
    };

    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* m = scene->mMeshes[mi];
        for (unsigned int bi = 0; bi < m->mNumBones; ++bi)
            get_or_add_bone(m->mBones[bi]);
    }
    bool skinned = !bone_inv_bind.empty();

    // ----- Convert vertices + indices + submeshes -----
    std::vector<EmeshVertex>        verts_static;
    std::vector<EmeshSkinnedVertex> verts_skinned;
    std::vector<uint32_t>           indices;
    std::vector<EmeshSubmesh>       submeshes;
    std::vector<std::string>        mat_names;

    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* m = scene->mMeshes[mi];

        EmeshSubmesh sub{};
        sub.index_offset = static_cast<uint32_t>(indices.size());
        sub.index_count  = 0;

        std::string mat_name = "default";
        if (m->mMaterialIndex < scene->mNumMaterials) {
            aiString name;
            scene->mMaterials[m->mMaterialIndex]->Get(AI_MATKEY_NAME, name);
            if (name.length > 0) mat_name = name.C_Str();
        }
        mat_names.push_back(mat_name);

        uint32_t base_vertex = static_cast<uint32_t>(
            skinned ? verts_skinned.size() : verts_static.size());

        // Per-vertex top-4 bone weights (LimitBoneWeights ensures ≤4).
        std::vector<std::array<std::pair<uint32_t, float>, 4>> vw(m->mNumVertices);
        std::vector<int> vw_count(m->mNumVertices, 0);
        if (skinned) {
            for (unsigned int bi = 0; bi < m->mNumBones; ++bi) {
                const aiBone* b = m->mBones[bi];
                uint32_t global_bone = bone_index[b->mName.C_Str()];
                for (unsigned int wi = 0; wi < b->mNumWeights; ++wi) {
                    uint32_t v = b->mWeights[wi].mVertexId;
                    float    w = b->mWeights[wi].mWeight;
                    int& c = vw_count[v];
                    if (c < 4) { vw[v][c++] = {global_bone, w}; }
                }
            }
        }

        for (unsigned int vi = 0; vi < m->mNumVertices; ++vi) {
            float px = m->mVertices[vi].x, py = m->mVertices[vi].y, pz = m->mVertices[vi].z;
            float nx = m->mNormals[vi].x,  ny = m->mNormals[vi].y,  nz = m->mNormals[vi].z;
            float u = m->mTextureCoords[0] ? m->mTextureCoords[0][vi].x : 0.f;
            float vv = m->mTextureCoords[0] ? m->mTextureCoords[0][vi].y : 0.f;
            float tx = 0.f, ty = 0.f, tz = 0.f, tw = 1.f;
            if (m->mTangents) {
                tx = m->mTangents[vi].x; ty = m->mTangents[vi].y; tz = m->mTangents[vi].z;
                aiVector3D bt = m->mBitangents[vi];
                aiVector3D cross = m->mNormals[vi] ^ m->mTangents[vi];
                tw = (cross.x*bt.x + cross.y*bt.y + cross.z*bt.z) < 0.f ? -1.f : 1.f;
            }

            if (skinned) {
                EmeshSkinnedVertex sv{};
                sv.px = px; sv.py = py; sv.pz = pz;
                sv.nx = nx; sv.ny = ny; sv.nz = nz;
                sv.u  = u;  sv.v  = vv;
                sv.tx = tx; sv.ty = ty; sv.tz = tz; sv.tw = tw;

                // Renormalize the (≤4) weights to sum to 1; pad missing with bone 0 weight 0.
                float sum = 0.f;
                for (int k = 0; k < vw_count[vi]; ++k) sum += vw[vi][k].second;
                if (sum > 1e-6f) {
                    for (int k = 0; k < vw_count[vi]; ++k) vw[vi][k].second /= sum;
                } else {
                    // Unweighted vertex (shouldn't happen for a rigged mesh).
                    // Bind it to bone 0 so it doesn't collapse to origin.
                    vw[vi][0] = {0u, 1.f};
                    vw_count[vi] = 1;
                }
                for (int k = 0; k < 4; ++k) {
                    if (k < vw_count[vi]) {
                        sv.bone_idx[k]    = static_cast<uint8_t>(vw[vi][k].first);
                        sv.bone_weight[k] = vw[vi][k].second;
                    } else {
                        sv.bone_idx[k]    = 0;
                        sv.bone_weight[k] = 0.f;
                    }
                }
                verts_skinned.push_back(sv);
            } else {
                EmeshVertex sv{};
                sv.px = px; sv.py = py; sv.pz = pz;
                sv.nx = nx; sv.ny = ny; sv.nz = nz;
                sv.u  = u;  sv.v  = vv;
                sv.tx = tx; sv.ty = ty; sv.tz = tz; sv.tw = tw;
                verts_static.push_back(sv);
            }
        }

        for (unsigned int fi = 0; fi < m->mNumFaces; ++fi) {
            const aiFace& face = m->mFaces[fi];
            for (unsigned int idx = 0; idx < face.mNumIndices; ++idx)
                indices.push_back(base_vertex + face.mIndices[idx]);
            sub.index_count += face.mNumIndices;
        }

        submeshes.push_back(sub);
    }

    // String block for material names.
    std::vector<char> string_block;
    for (size_t i = 0; i < submeshes.size(); ++i) {
        submeshes[i].material_name_offset = static_cast<uint32_t>(string_block.size());
        const std::string& name = mat_names[i];
        string_block.insert(string_block.end(), name.begin(), name.end());
        string_block.push_back('\0');
    }

    // ----- Write .emesh (skip entirely for anim-only inputs) -----
    const bool has_mesh = !verts_static.empty() || !verts_skinned.empty();
    if (has_mesh) {
        EmeshHeader hdr{};
        hdr.magic             = EMESH_MAGIC;
        hdr.version           = EMESH_VERSION;
        hdr.flags             = skinned ? EMESH_FLAG_SKINNED : 0u;
        hdr.vertex_count      = static_cast<uint32_t>(
            skinned ? verts_skinned.size() : verts_static.size());
        hdr.index_count       = static_cast<uint32_t>(indices.size());
        hdr.submesh_count     = static_cast<uint32_t>(submeshes.size());
        hdr.string_block_size = static_cast<uint32_t>(string_block.size());

        std::string out_path = out_base + ".emesh";
        std::FILE* f = std::fopen(out_path.c_str(), "wb");
        if (!f) die("cannot open emesh output");
        std::fwrite(&hdr, sizeof(hdr), 1, f);
        if (skinned) {
            std::fwrite(verts_skinned.data(), sizeof(EmeshSkinnedVertex),
                        verts_skinned.size(), f);
        } else {
            std::fwrite(verts_static.data(), sizeof(EmeshVertex),
                        verts_static.size(), f);
        }
        std::fwrite(indices.data(),    sizeof(uint32_t),     indices.size(),    f);
        std::fwrite(submeshes.data(),  sizeof(EmeshSubmesh), submeshes.size(),  f);
        std::fwrite(string_block.data(), 1, string_block.size(), f);
        std::fclose(f);
        std::printf("meshconv: wrote %s  %s  %u verts  %u idx  %u submeshes\n",
                    out_path.c_str(), skinned ? "(skinned)" : "(static)",
                    hdr.vertex_count, hdr.index_count, hdr.submesh_count);
    }

    // Skip skeleton write if no skin data; we may still have animation
    // channels to emit below (anim-only FBXs go through that path).
    if (skinned) {

    // ----- Build skeleton: parent indices + bind_local from inv_bind --------
    std::vector<EskelBone>     bones(bone_inv_bind.size());
    std::vector<aiMatrix4x4>   bind_local(bone_inv_bind.size());

    // Parent indices come from the node hierarchy: walk the scene graph and
    // for each node that is also a bone, record its closest bone ancestor.
    std::function<void(aiNode*, int32_t)> visit_parents = [&](aiNode* node, int32_t parent_idx) {
        int32_t my_idx = -1;
        auto it = bone_index.find(node->mName.C_Str());
        if (it != bone_index.end()) {
            my_idx = static_cast<int32_t>(it->second);
            bones[my_idx].parent = parent_idx;
        }
        int32_t pass_parent = (my_idx != -1) ? my_idx : parent_idx;
        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            visit_parents(node->mChildren[i], pass_parent);
    };
    for (auto& b : bones) b.parent = -1;
    visit_parents(scene->mRootNode, -1);

    // bind_local is derived from inv_bind matrices, which Assimp populates
    // robustly via aiBone::mOffsetMatrix:
    //   world_bind[b]  = inverse(inv_bind[b])
    //   bind_local[b] = inv_bind[parent] * world_bind[b]   (or world_bind[b] if root)
    // This is independent of intermediate non-bone nodes in the FBX hierarchy
    // (which often carry transforms that the FBX visit traversal would miss).
    std::vector<aiMatrix4x4> world_bind(bone_inv_bind.size());
    for (size_t bi = 0; bi < bone_inv_bind.size(); ++bi) {
        world_bind[bi] = bone_inv_bind[bi];
        world_bind[bi].Inverse();
    }
    for (size_t bi = 0; bi < bone_inv_bind.size(); ++bi) {
        int32_t p = bones[bi].parent;
        if (p < 0) {
            bind_local[bi] = world_bind[bi];
        } else {
            bind_local[bi] = bone_inv_bind[static_cast<size_t>(p)] * world_bind[bi];
        }
    }

    // Pack inv_bind + bind_local + bone names.
    std::vector<char> bone_strings;
    std::vector<std::string> bone_names_by_idx(bone_inv_bind.size());
    for (auto& kv : bone_index) bone_names_by_idx[kv.second] = kv.first;

    for (uint32_t bi = 0; bi < bones.size(); ++bi) {
        bones[bi].name_offset = static_cast<uint32_t>(bone_strings.size());
        const std::string& n = bone_names_by_idx[bi];
        bone_strings.insert(bone_strings.end(), n.begin(), n.end());
        bone_strings.push_back('\0');

        aimat_to_colmajor(bone_inv_bind[bi], bones[bi].inv_bind);
        aimat_to_colmajor(bind_local[bi],    bones[bi].bind_local);
    }

    // ----- Write .eskel -----
    {
        EskelHeader hdr{};
        hdr.magic             = ESKEL_MAGIC;
        hdr.version           = ESKEL_VERSION;
        hdr.bone_count        = static_cast<uint32_t>(bones.size());
        hdr.string_block_size = static_cast<uint32_t>(bone_strings.size());

        std::string out_path = out_base + ".eskel";
        std::FILE* f = std::fopen(out_path.c_str(), "wb");
        if (!f) die("cannot open eskel output");
        std::fwrite(&hdr, sizeof(hdr), 1, f);
        std::fwrite(bones.data(),        sizeof(EskelBone), bones.size(), f);
        std::fwrite(bone_strings.data(), 1, bone_strings.size(),          f);
        std::fclose(f);
        std::printf("meshconv: wrote %s  %u bones\n",
                    out_path.c_str(), hdr.bone_count);
    }
    } // end if (skinned)

    // ----- Write .eanim (one animation per file; first one wins for now) -----
    // aiProcess_PreTransformVertices on the primary import bakes node
    // transforms into vertex positions and as a side effect strips the
    // animation channels. For anim-only FBXs (Mixamo "in-place" exports
    // that nonetheless ship a placeholder mesh, e.g. "Dying Backwards"),
    // re-import without that flag and adopt the new scene's animations.
    // imp2 is at function scope so the aiAnimation* pointers below stay
    // valid for the rest of main.
    Assimp::Importer imp2;
    if (scene->mNumAnimations == 0) {
        const aiScene* anim_scene = imp2.ReadFile(in_path,
            aiProcess_Triangulate       |
            aiProcess_GenSmoothNormals  |
            aiProcess_CalcTangentSpace  |
            aiProcess_JoinIdenticalVertices |
            aiProcess_LimitBoneWeights  |
            aiProcess_SortByPType);
        if (anim_scene && anim_scene->mNumAnimations > 0) {
            std::printf("meshconv: re-imported without PreTransformVertices"
                        " for animation extraction (%u anims)\n",
                        anim_scene->mNumAnimations);
            scene = anim_scene;
        }
    }
    if (scene->mNumAnimations == 0) return 0;
    {
        const aiAnimation* a = scene->mAnimations[0];
        // Mixamo / many FBX exporters set mTicksPerSecond=30 but already store
        // mDuration and keyframe mTimes in seconds. Heuristic: if dividing by
        // tps would give an unreasonably short duration (< 50 ms), treat
        // times as seconds directly.
        double tps = a->mTicksPerSecond > 0 ? a->mTicksPerSecond : 25.0;
        if (a->mDuration / tps < 0.05) {
            std::printf("  (treating animation timeline as seconds, not ticks)\n");
            tps = 1.0;
        }

        std::vector<EanimChannel>     chans;
        std::vector<EanimVec3Key>     pos_keys_all;
        std::vector<EanimQuatKey>     rot_keys_all;
        std::vector<EanimVec3Key>     scale_keys_all;
        std::vector<char>             chan_strings;

        for (unsigned int ci = 0; ci < a->mNumChannels; ++ci) {
            const aiNodeAnim* na = a->mChannels[ci];

            EanimChannel ch{};
            ch.bone_name_offset = static_cast<uint32_t>(chan_strings.size());
            const char* nm = na->mNodeName.C_Str();
            chan_strings.insert(chan_strings.end(), nm, nm + std::strlen(nm));
            chan_strings.push_back('\0');

            ch.pos_key_count   = na->mNumPositionKeys;
            ch.rot_key_count   = na->mNumRotationKeys;
            ch.scale_key_count = na->mNumScalingKeys;

            for (unsigned int k = 0; k < na->mNumPositionKeys; ++k) {
                const aiVectorKey& vk = na->mPositionKeys[k];
                pos_keys_all.push_back({
                    static_cast<float>(vk.mTime / tps),
                    vk.mValue.x, vk.mValue.y, vk.mValue.z});
            }
            for (unsigned int k = 0; k < na->mNumRotationKeys; ++k) {
                const aiQuatKey& qk = na->mRotationKeys[k];
                rot_keys_all.push_back({
                    static_cast<float>(qk.mTime / tps),
                    qk.mValue.x, qk.mValue.y, qk.mValue.z, qk.mValue.w});
            }
            for (unsigned int k = 0; k < na->mNumScalingKeys; ++k) {
                const aiVectorKey& vk = na->mScalingKeys[k];
                scale_keys_all.push_back({
                    static_cast<float>(vk.mTime / tps),
                    vk.mValue.x, vk.mValue.y, vk.mValue.z});
            }

            chans.push_back(ch);
        }

        EanimHeader hdr{};
        hdr.magic             = EANIM_MAGIC;
        hdr.version           = EANIM_VERSION;
        hdr.channel_count     = static_cast<uint32_t>(chans.size());
        hdr.string_block_size = static_cast<uint32_t>(chan_strings.size());
        hdr.duration          = static_cast<float>(a->mDuration / tps);

        std::string out_path = out_base + ".eanim";
        std::FILE* f = std::fopen(out_path.c_str(), "wb");
        if (!f) die("cannot open eanim output");
        std::fwrite(&hdr,        sizeof(hdr),          1,                f);
        std::fwrite(chans.data(), sizeof(EanimChannel), chans.size(),    f);
        // Per-channel keyframes, in declaration order: pos / rot / scale.
        // Replay the same channel iteration to interleave correctly.
        size_t pi = 0, ri = 0, si = 0;
        for (auto& ch : chans) {
            std::fwrite(pos_keys_all.data()   + pi, sizeof(EanimVec3Key), ch.pos_key_count,   f);
            std::fwrite(rot_keys_all.data()   + ri, sizeof(EanimQuatKey), ch.rot_key_count,   f);
            std::fwrite(scale_keys_all.data() + si, sizeof(EanimVec3Key), ch.scale_key_count, f);
            pi += ch.pos_key_count; ri += ch.rot_key_count; si += ch.scale_key_count;
        }
        std::fwrite(chan_strings.data(), 1, chan_strings.size(), f);
        std::fclose(f);
        std::printf("meshconv: wrote %s  %u channels  %.2fs\n",
                    out_path.c_str(), hdr.channel_count, hdr.duration);
    }

    return 0;
}
