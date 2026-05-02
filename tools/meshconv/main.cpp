// meshconv — converts OBJ/FBX/glTF to .emesh binary format.
// Links Assimp; must never be linked into the engine runtime.
//
// Usage: meshconv <input> <output.emesh>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/emesh_format.h"

using namespace pengine;

static void die(const char* msg) {
    std::fprintf(stderr, "meshconv error: %s\n", msg);
    std::exit(1);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: meshconv <input> <output.emesh>\n");
        return 1;
    }
    const char* in_path  = argv[1];
    const char* out_path = argv[2];

    Assimp::Importer imp;
    // Note: no aiProcess_FlipUVs. Our texture loader already flips images
    // vertically for OpenGL's bottom-left origin (stbi_set_flip_vertically_on_load),
    // and our procedural / FBX UVs come out OpenGL-correct without a flip.
    const aiScene* scene = imp.ReadFile(in_path,
        aiProcess_Triangulate       |
        aiProcess_GenSmoothNormals  |
        aiProcess_CalcTangentSpace  |
        aiProcess_JoinIdenticalVertices |
        aiProcess_SortByPType);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        std::fprintf(stderr, "meshconv: Assimp error: %s\n", imp.GetErrorString());
        return 1;
    }

    // Diagnostics: bones / animations are not yet preserved in .emesh, but
    // surface them so we know whether the asset has any.
    std::printf("meshconv: scene meshes=%u  animations=%u\n",
                scene->mNumMeshes, scene->mNumAnimations);
    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* m = scene->mMeshes[mi];
        std::printf("  mesh[%u]: '%s' verts=%u faces=%u bones=%u\n",
                    mi, m->mName.C_Str(), m->mNumVertices, m->mNumFaces, m->mNumBones);
    }
    for (unsigned int ai = 0; ai < scene->mNumAnimations; ++ai) {
        const aiAnimation* a = scene->mAnimations[ai];
        std::printf("  anim[%u]: '%s' duration=%.2f tps=%.1f channels=%u\n",
                    ai, a->mName.C_Str(), a->mDuration, a->mTicksPerSecond,
                    a->mNumChannels);
    }

    std::vector<EmeshVertex>  vertices;
    std::vector<uint32_t>     indices;
    std::vector<EmeshSubmesh> submeshes;
    std::vector<std::string>  mat_names;

    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* m = scene->mMeshes[mi];

        EmeshSubmesh sub{};
        sub.index_offset = static_cast<uint32_t>(indices.size());
        sub.index_count  = 0;

        // Material name
        std::string mat_name = "default";
        if (m->mMaterialIndex < scene->mNumMaterials) {
            aiString name;
            scene->mMaterials[m->mMaterialIndex]->Get(AI_MATKEY_NAME, name);
            if (name.length > 0) mat_name = name.C_Str();
        }
        mat_names.push_back(mat_name);

        uint32_t base_vertex = static_cast<uint32_t>(vertices.size());

        for (unsigned int vi = 0; vi < m->mNumVertices; ++vi) {
            EmeshVertex v{};
            v.px = m->mVertices[vi].x;
            v.py = m->mVertices[vi].y;
            v.pz = m->mVertices[vi].z;
            v.nx = m->mNormals[vi].x;
            v.ny = m->mNormals[vi].y;
            v.nz = m->mNormals[vi].z;
            if (m->mTextureCoords[0]) {
                v.u = m->mTextureCoords[0][vi].x;
                v.v = m->mTextureCoords[0][vi].y;
            }
            if (m->mTangents) {
                v.tx = m->mTangents[vi].x;
                v.ty = m->mTangents[vi].y;
                v.tz = m->mTangents[vi].z;
                // Bitangent sign via cross-product check
                aiVector3D bt = m->mBitangents[vi];
                aiVector3D cross = m->mNormals[vi] ^ m->mTangents[vi];
                v.tw = (cross.x*bt.x + cross.y*bt.y + cross.z*bt.z) < 0.f ? -1.f : 1.f;
            } else {
                v.tw = 1.f;
            }
            vertices.push_back(v);
        }

        for (unsigned int fi = 0; fi < m->mNumFaces; ++fi) {
            const aiFace& face = m->mFaces[fi];
            for (unsigned int idx = 0; idx < face.mNumIndices; ++idx) {
                indices.push_back(base_vertex + face.mIndices[idx]);
            }
            sub.index_count += face.mNumIndices;
        }

        submeshes.push_back(sub);
    }

    // Build string block
    std::vector<char> string_block;
    for (std::size_t i = 0; i < submeshes.size(); ++i) {
        submeshes[i].material_name_offset = static_cast<uint32_t>(string_block.size());
        const std::string& name = mat_names[i];
        string_block.insert(string_block.end(), name.begin(), name.end());
        string_block.push_back('\0');
    }

    EmeshHeader hdr{};
    hdr.magic             = EMESH_MAGIC;
    hdr.version           = EMESH_VERSION;
    hdr.vertex_count      = static_cast<uint32_t>(vertices.size());
    hdr.index_count       = static_cast<uint32_t>(indices.size());
    hdr.submesh_count     = static_cast<uint32_t>(submeshes.size());
    hdr.string_block_size = static_cast<uint32_t>(string_block.size());

    std::FILE* f = std::fopen(out_path, "wb");
    if (!f) die("cannot open output file");

    std::fwrite(&hdr,                sizeof(hdr),            1,                  f);
    std::fwrite(vertices.data(),     sizeof(EmeshVertex),    vertices.size(),    f);
    std::fwrite(indices.data(),      sizeof(uint32_t),       indices.size(),     f);
    std::fwrite(submeshes.data(),    sizeof(EmeshSubmesh),   submeshes.size(),   f);
    std::fwrite(string_block.data(), 1,                      string_block.size(), f);
    std::fclose(f);

    std::printf("meshconv: wrote %s\n", out_path);
    std::printf("  %u vertices  %u indices  %u submeshes\n",
                hdr.vertex_count, hdr.index_count, hdr.submesh_count);
    for (std::size_t i = 0; i < submeshes.size(); ++i) {
        std::printf("  submesh[%zu]: %u tris  material='%s'\n",
                    i, submeshes[i].index_count / 3,
                    string_block.data() + submeshes[i].material_name_offset);
    }
    return 0;
}
