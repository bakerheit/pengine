#pragma once

#include <cstdint>

// Binary .eanim format — one animation, sampled per bone.
//
// File layout:
//   EanimHeader
//   EanimChannel[channel_count]                  // header + key counts per bone
//   For each channel, in declaration order:
//     EanimVec3Key[pos_key_count]                // (time, x, y, z)
//     EanimQuatKey[rot_key_count]                // (time, x, y, z, w)
//     EanimVec3Key[scale_key_count]              // (time, x, y, z)
//   char[string_block_size]                       // null-terminated bone name strings
//
// All times are in seconds, in [0, duration]. Channels are independent: a
// bone's pose at time t is interpolated separately for each track.
// Sample by linear interpolation between bracketing keys (slerp for rotations).

namespace pengine {

constexpr uint32_t EANIM_MAGIC   = 0x4D4E4145u; // 'EANM'
constexpr uint32_t EANIM_VERSION = 1u;

struct EanimVec3Key {
    float time;
    float x, y, z;
};

struct EanimQuatKey {
    float time;
    float x, y, z, w;
};

struct EanimChannel {
    uint32_t bone_name_offset;   // byte offset into string block (null-terminated)
    uint32_t pos_key_count;
    uint32_t rot_key_count;
    uint32_t scale_key_count;
};

struct EanimHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t channel_count;
    uint32_t string_block_size;
    float    duration;            // seconds
    float    _pad[3];             // 32-byte alignment
};

static_assert(sizeof(EanimHeader)   == 32, "anim header size");
static_assert(sizeof(EanimChannel)  == 16, "anim channel size");
static_assert(sizeof(EanimVec3Key)  == 16, "anim vec3 key size");
static_assert(sizeof(EanimQuatKey)  == 20, "anim quat key size");

} // namespace pengine
