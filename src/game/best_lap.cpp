#include "game/best_lap.h"

#include <cstdio>
#include <cstring>

namespace apricot {
namespace {

// Eight bytes, no terminator: the magic is a fixed-width field, not a string.
constexpr char kMagic[8] = {'A', 'P', 'R', 'R', 'A', 'L', 'L', 'Y'};

// Bytes on disk per recorded input frame. Used to bound-check a frame count
// BEFORE reserving anything, so a corrupt length field cannot ask for a
// terabyte.
constexpr std::size_t kFrameBytes = 32;
constexpr std::size_t kSplitBytes = 8;

// Everything is written little-endian by explicit shift rather than by dumping
// the struct. InputFrame is trivially copyable and the header even asserts its
// size, but a memcpy'd tape is a tape that stops loading the day someone
// builds for a big-endian target or the compiler pads differently. Shifts cost
// nothing here and are the same bytes everywhere.
class Writer {
public:
    void raw(const char* p, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            bytes_.push_back(static_cast<unsigned char>(p[i]));
        }
    }
    void u8(unsigned char v) { bytes_.push_back(v); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            bytes_.push_back(static_cast<unsigned char>((v >> (i * 8)) & 0xFFu));
        }
    }
    void u64(uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            bytes_.push_back(
                static_cast<unsigned char>((v >> (i * 8)) & 0xFFull));
        }
    }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v) {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        u32(bits);
    }
    void f64(double v) {
        uint64_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        u64(bits);
    }
    void vec3(const glm::vec3& v) { f32(v.x); f32(v.y); f32(v.z); }
    void quat(const glm::quat& q) { f32(q.w); f32(q.x); f32(q.y); f32(q.z); }

    const std::vector<unsigned char>& bytes() const { return bytes_; }

private:
    std::vector<unsigned char> bytes_;
};

// Bounds-checked on every read. Once ok_ goes false it stays false and every
// subsequent read returns zero, so the caller can parse the whole record and
// check once at the end instead of threading a status through forty calls.
class Reader {
public:
    Reader(const unsigned char* data, std::size_t size)
        : data_(data), size_(size) {}

    bool ok() const { return ok_; }
    std::size_t remaining() const { return ok_ ? size_ - pos_ : 0u; }

    bool raw(char* out, std::size_t n) {
        if (!take(n)) return false;
        for (std::size_t i = 0; i < n; ++i) {
            out[i] = static_cast<char>(data_[pos_ - n + i]);
        }
        return true;
    }
    unsigned char u8() { return take(1) ? data_[pos_ - 1] : 0u; }
    uint32_t u32() {
        if (!take(4)) return 0u;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<uint32_t>(data_[pos_ - 4 + static_cast<std::size_t>(i)])
                 << (i * 8);
        }
        return v;
    }
    uint64_t u64() {
        if (!take(8)) return 0u;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<uint64_t>(data_[pos_ - 8 + static_cast<std::size_t>(i)])
                 << (i * 8);
        }
        return v;
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    float f32() {
        const uint32_t bits = u32();
        float v = 0.0f;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
    double f64() {
        const uint64_t bits = u64();
        double v = 0.0;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
    glm::vec3 vec3() {
        glm::vec3 v;
        v.x = f32(); v.y = f32(); v.z = f32();
        return v;
    }
    glm::quat quat() {
        glm::quat q;
        q.w = f32(); q.x = f32(); q.y = f32(); q.z = f32();
        return q;
    }

private:
    bool take(std::size_t n) {
        if (!ok_ || size_ - pos_ < n) {
            ok_ = false;
            return false;
        }
        pos_ += n;
        return true;
    }

    const unsigned char* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

void write_vehicle(Writer& w, const VehicleState& v) {
    w.vec3(v.position);
    w.quat(v.orientation);
    w.vec3(v.velocity);
    w.vec3(v.angular_velocity);
    w.f32(v.steer_angle);
    w.f32(v.engine_rpm);
    w.i32(v.gear);
    for (int i = 0; i < kWheelCount; ++i) {
        const WheelState& wh = v.wheels[static_cast<std::size_t>(i)];
        w.vec3(wh.contact_point);
        w.vec3(wh.contact_normal);
        w.f32(wh.suspension_length);
        w.f32(wh.spin);
        w.u8(wh.grounded ? 1u : 0u);
    }
}

void read_vehicle(Reader& r, VehicleState& v) {
    v.position = r.vec3();
    v.orientation = r.quat();
    v.velocity = r.vec3();
    v.angular_velocity = r.vec3();
    v.steer_angle = r.f32();
    v.engine_rpm = r.f32();
    v.gear = r.i32();
    for (int i = 0; i < kWheelCount; ++i) {
        WheelState& wh = v.wheels[static_cast<std::size_t>(i)];
        wh.contact_point = r.vec3();
        wh.contact_normal = r.vec3();
        wh.suspension_length = r.f32();
        wh.spin = r.f32();
        wh.grounded = r.u8() != 0u;
    }
}

bool read_whole_file(const std::string& path, std::vector<unsigned char>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    out.clear();
    unsigned char buf[4096];
    for (;;) {
        const std::size_t n = std::fread(buf, 1, sizeof(buf), f);
        if (n > 0) out.insert(out.end(), buf, buf + n);
        if (n < sizeof(buf)) break;
    }
    const bool bad = std::ferror(f) != 0;
    std::fclose(f);
    return !bad;
}

}  // namespace

std::string best_lap_filename(uint64_t seed) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "apricot_best_%016llX.lap",
                  static_cast<unsigned long long>(seed));
    return std::string(buf);
}

bool save_best_lap(const std::string& path, const BestLap& record) {
    if (!record.valid) return false;

    Writer w;
    w.raw(kMagic, sizeof(kMagic));
    w.u32(kBestLapFileVersion);
    w.u64(record.seed);
    w.f64(record.lap_time);

    w.u32(static_cast<uint32_t>(record.splits.size()));
    for (const double s : record.splits) w.f64(s);

    w.u32(record.tape.version);
    w.u64(record.tape.seed);
    w.u64(record.tape.start.step);
    w.i32(record.tape.start.checkpoint);
    w.f64(record.tape.start.lap_time);
    write_vehicle(w, record.tape.start.car);

    w.u32(static_cast<uint32_t>(record.tape.frames.size()));
    for (const InputFrame& f : record.tape.frames) {
        w.f32(f.steer);
        w.f32(f.throttle);
        w.f32(f.brake);
        w.f32(f.handbrake);
        w.f32(f.look_dx);
        w.f32(f.look_dy);
        w.u32(f.held);
        w.u32(f.pressed);
    }

    std::FILE* out = std::fopen(path.c_str(), "wb");
    if (!out) return false;

    const std::vector<unsigned char>& bytes = w.bytes();
    const std::size_t written =
        bytes.empty() ? 0u : std::fwrite(bytes.data(), 1, bytes.size(), out);
    // fclose can fail on a full disk after fwrite reported success, so its
    // result is part of the answer, not a formality.
    const bool closed = std::fclose(out) == 0;
    return closed && written == bytes.size();
}

bool load_best_lap(const std::string& path, uint64_t seed, BestLap& out) {
    out = BestLap{};

    std::vector<unsigned char> bytes;
    if (!read_whole_file(path, bytes)) return false;
    if (bytes.empty()) return false;

    Reader r(bytes.data(), bytes.size());

    char magic[8] = {};
    if (!r.raw(magic, sizeof(magic))) return false;
    if (std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) return false;

    if (r.u32() != kBestLapFileVersion) return false;

    BestLap rec;
    rec.seed = r.u64();
    rec.lap_time = r.f64();
    if (!r.ok()) return false;

    // A best lap belongs to exactly one world. Refuse before touching the tape.
    if (rec.seed != seed) return false;

    const uint32_t split_count = r.u32();
    if (!r.ok()) return false;
    if (static_cast<std::size_t>(split_count) > r.remaining() / kSplitBytes) {
        return false;
    }
    rec.splits.reserve(split_count);
    for (uint32_t i = 0; i < split_count; ++i) rec.splits.push_back(r.f64());

    rec.tape.version = r.u32();
    rec.tape.seed = r.u64();
    rec.tape.start.step = r.u64();
    rec.tape.start.checkpoint = r.i32();
    rec.tape.start.lap_time = r.f64();
    read_vehicle(r, rec.tape.start.car);
    if (!r.ok()) return false;

    // A tape recorded against different step semantics replays as a
    // convincing wrong lap. Refuse it.
    if (rec.tape.version != kReplayTapeVersion) return false;
    if (rec.tape.seed != seed) return false;

    const uint32_t frame_count = r.u32();
    if (!r.ok()) return false;
    if (static_cast<std::size_t>(frame_count) > r.remaining() / kFrameBytes) {
        return false;
    }
    rec.tape.frames.resize(frame_count);
    for (uint32_t i = 0; i < frame_count; ++i) {
        InputFrame& f = rec.tape.frames[static_cast<std::size_t>(i)];
        f.steer = r.f32();
        f.throttle = r.f32();
        f.brake = r.f32();
        f.handbrake = r.f32();
        f.look_dx = r.f32();
        f.look_dy = r.f32();
        f.held = r.u32();
        f.pressed = r.u32();
    }
    if (!r.ok()) return false;

    rec.valid = true;
    out = rec;
    return true;
}

}  // namespace apricot
