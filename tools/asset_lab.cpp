// Apricot Asset Lab: small, real-renderer QA for cooked models and textures.
//
// This deliberately uses the same .emesh reader, Texture, Renderer and Scene
// as the game. A pretty preview made through a different importer can hide the
// exact UV, winding, scale and shader bugs this tool exists to expose.

#include <SDL.h>
#include <glad/gl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include "physics/vehicle_damage.h"
#include "app/vehicle_lamp_mesh.h"
#include "app/vehicle_headlight_profile.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "core/log.h"
#include "core/transform.h"
#include "gfx/camera.h"
#include "gfx/lighting.h"
#include "gfx/primitives.h"
#include "gfx/renderer.h"
#include "gfx/sky_env.h"
#include "gfx/texture.h"
#include "platform/window.h"
#include "scene/scene.h"

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr float kPi = 3.14159265358979323846f;

struct Options {
    std::vector<std::string> mesh_specs;
    std::string asset_dir;
    std::string animation;
    std::string texture;
    std::string screenshot;
    int frame_limit = 0;
    int pinned_frame = -1;
    float fps = 8.0f;
    float yaw_degrees = 0.0f;
    float turntable_degrees_per_second = 0.0f;
    float display_height = 0.0f;
    float initial_zoom = 1.0f;
    int damage_zone = -1;
    int lamp_preview = -1;
    float lamp_power = 1.0f;
    float damage_strength = 0.75f;
    bool damage_contact_set = false;
    bool instancing = true;
    glm::vec3 damage_contact{0.0f, 0.33f, 0.0f};
};

struct LoadedFrame {
    std::string path;
    apricot::StaticEmesh source;
    apricot::MeshId gpu = apricot::kInvalidId;
    uint64_t geometry_hash = 0;
};

void print_usage() {
    std::printf(
        "Apricot Asset Lab\n"
        "\n"
        "  apricot_asset_lab --asset-dir DIR [--animation NAME]\n"
        "  apricot_asset_lab --model FILE [--texture FILE]\n"
        "  apricot_asset_lab --clip GLOB --texture FILE\n"
        "  apricot_asset_lab --texture FILE\n"
        "\n"
        "Input:\n"
        "  --asset-dir DIR   directory with body.png and baked .emesh frames\n"
        "  --animation NAME  load NAME_*.emesh from --asset-dir\n"
        "  --model FILE      load one cooked .emesh; may be repeated\n"
        "  --clip GLOB       load a sorted baked clip, e.g. 'walk_*.emesh'\n"
        "  --texture FILE    diffuse PNG; alone opens a flat texture preview\n"
        "\n"
        "View:\n"
        "  --yaw DEG         runtime model correction before interactive turn\n"
        "  --height METRES   scale the union bounds to this display height\n"
        "  --zoom N          camera distance multiplier (.35..2.5, default 1)\n"
        "  --damage-zone N   preview one vehicle damage region (0..11)\n"
        "  --lamp-preview N  overlay a body-surface glow mask (0..3)\n"
        "  --lamp-power N    lamp level 0..1 (.22 for rear running lights)\n"
        "  --damage-strength N severity 0..1 (default .75)\n"
        "  --damage-contact X H Z  optional physics-frame hit (-1..1, 0..1, -1..1)\n"
        "  --fps N           animation playback rate (default 8)\n"
        "  --frame N         pin a zero-based animation frame\n"
        "  --turntable DEG   automatic turntable speed in degrees/second\n"
        "\n"
        "Automation:\n"
        "  --frames N        render exactly N frames, then exit\n"
        "  --screenshot FILE save the final frame as PNG or BMP\n"
        "  --no-instancing   check the single-instance renderer path\n"
        "\n"
        "Controls: A/D rotate, Q/E zoom, Space pause, arrows step, R reset,\n"
        "S saves a PNG, Esc quits. Red is +X, green is +Y, blue is forward -Z.\n");
}

bool parse_int(const char* text, int& out) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

bool parse_float(const char* text, float& out) {
    char* end = nullptr;
    const float value = std::strtof(text, &end);
    if (!end || *end != '\0' || !std::isfinite(value)) return false;
    out = value;
    return true;
}

bool take_value(int argc, char** argv, int& index, const char* option,
                const char*& value) {
    if (index + 1 >= argc) {
        std::fprintf(stderr, "%s needs a value\n", option);
        return false;
    }
    value = argv[++index];
    return true;
}

bool parse_options(int argc, char** argv, Options& out) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            std::exit(0);
        }

        const char* value = nullptr;
        if (arg == "--no-instancing") {
            out.instancing = false;
        } else if (arg == "--model" || arg == "--clip") {
            if (!take_value(argc, argv, i, arg.c_str(), value)) return false;
            out.mesh_specs.emplace_back(value);
        } else if (arg == "--asset-dir") {
            if (!take_value(argc, argv, i, arg.c_str(), value)) return false;
            out.asset_dir = value;
        } else if (arg == "--animation") {
            if (!take_value(argc, argv, i, arg.c_str(), value)) return false;
            out.animation = value;
        } else if (arg == "--texture") {
            if (!take_value(argc, argv, i, arg.c_str(), value)) return false;
            out.texture = value;
        } else if (arg == "--screenshot") {
            if (!take_value(argc, argv, i, arg.c_str(), value)) return false;
            out.screenshot = value;
        } else if (arg == "--frames") {
            if (!take_value(argc, argv, i, arg.c_str(), value) ||
                !parse_int(value, out.frame_limit) || out.frame_limit <= 0) {
                std::fprintf(stderr, "--frames needs a positive integer\n");
                return false;
            }
        } else if (arg == "--frame") {
            if (!take_value(argc, argv, i, arg.c_str(), value) ||
                !parse_int(value, out.pinned_frame) || out.pinned_frame < 0) {
                std::fprintf(stderr, "--frame needs a non-negative integer\n");
                return false;
            }
        } else if (arg == "--fps") {
            if (!take_value(argc, argv, i, arg.c_str(), value) ||
                !parse_float(value, out.fps) || out.fps <= 0.0f) {
                std::fprintf(stderr, "--fps needs a positive number\n");
                return false;
            }
        } else if (arg == "--zoom") {
            if (!take_value(argc, argv, i, arg.c_str(), value) ||
                !parse_float(value, out.initial_zoom) || out.initial_zoom < .35f ||
                out.initial_zoom > 2.5f) return false;
        } else if (arg == "--lamp-power") {
            if (!take_value(argc, argv, i, arg.c_str(), value) ||
                !parse_float(value, out.lamp_power) || out.lamp_power < 0.0f ||
                out.lamp_power > 1.0f) return false;
        } else if (arg == "--lamp-preview") {
            if (!take_value(argc, argv, i, arg.c_str(), value) ||
                !parse_int(value, out.lamp_preview) || out.lamp_preview < 0 ||
                out.lamp_preview > 3) return false;
        } else if (arg == "--damage-zone") {
            if (!take_value(argc, argv, i, arg.c_str(), value) ||
                !parse_int(value, out.damage_zone) || out.damage_zone < 0 ||
                out.damage_zone >= static_cast<int>(apricot::kVehicleDamageZoneCount)) {
                std::fprintf(stderr, "--damage-zone needs an index from 0 to 11\n");
                return false;
            }
        } else if (arg == "--damage-strength") {
            if (!take_value(argc, argv, i, arg.c_str(), value) ||
                !parse_float(value, out.damage_strength) ||
                out.damage_strength < 0.0f || out.damage_strength > 1.0f) return false;
        } else if (arg == "--damage-contact") {
            for (int component = 0; component < 3; ++component) {
                if (!take_value(argc, argv, i, arg.c_str(), value) ||
                    !parse_float(value, out.damage_contact[component])) return false;
                const float low = component == 1 ? 0.0f : -1.0f;
                if (out.damage_contact[component] < low ||
                    out.damage_contact[component] > 1.0f) return false;
            }
            out.damage_contact_set = true;
        } else if (arg == "--yaw") {
            if (!take_value(argc, argv, i, arg.c_str(), value) ||
                !parse_float(value, out.yaw_degrees)) {
                std::fprintf(stderr, "--yaw needs a finite number\n");
                return false;
            }
        } else if (arg == "--turntable") {
            if (!take_value(argc, argv, i, arg.c_str(), value) ||
                !parse_float(value, out.turntable_degrees_per_second)) {
                std::fprintf(stderr, "--turntable needs a finite number\n");
                return false;
            }
        } else if (arg == "--height") {
            if (!take_value(argc, argv, i, arg.c_str(), value) ||
                !parse_float(value, out.display_height) ||
                out.display_height <= 0.0f) {
                std::fprintf(stderr, "--height needs a positive number\n");
                return false;
            }
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            return false;
        }
    }

    if (!out.animation.empty() && out.asset_dir.empty()) {
        std::fprintf(stderr, "--animation needs --asset-dir\n");
        return false;
    }
    if (out.mesh_specs.empty() && out.asset_dir.empty() && out.texture.empty()) {
        std::fprintf(stderr, "give me --asset-dir, --model, --clip, or --texture\n");
        return false;
    }
    if (!out.screenshot.empty() && out.frame_limit <= 0) {
        std::fprintf(stderr,
                     "--screenshot needs --frames for a deterministic capture\n");
        return false;
    }
    return true;
}

fs::path existing_path(const std::string& input) {
    fs::path path(input);
    std::error_code ec;
    if (fs::exists(path, ec)) return path.lexically_normal();
    if (!path.is_absolute()) {
        path = fs::path(apricot::asset_root()) / path;
        if (fs::exists(path, ec)) return path.lexically_normal();
    }
    return {};
}

bool wildcard_match(const std::string& name, const std::string& pattern) {
    const std::size_t star = pattern.find('*');
    if (star == std::string::npos) return name == pattern;
    if (pattern.find('*', star + 1u) != std::string::npos) return false;
    const std::string prefix = pattern.substr(0, star);
    const std::string suffix = pattern.substr(star + 1u);
    return name.size() >= prefix.size() + suffix.size() &&
           name.compare(0, prefix.size(), prefix) == 0 &&
           name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<fs::path> expand_spec(const std::string& input) {
    fs::path pattern(input);
    if (input.find('*') == std::string::npos) {
        const fs::path found = existing_path(input);
        return found.empty() ? std::vector<fs::path>{}
                             : std::vector<fs::path>{found};
    }

    fs::path directory = pattern.parent_path();
    if (directory.empty()) directory = ".";
    std::error_code ec;
    if (!fs::is_directory(directory, ec) && !pattern.is_absolute()) {
        pattern = fs::path(apricot::asset_root()) / pattern;
        directory = pattern.parent_path();
    }
    if (!fs::is_directory(directory, ec)) return {};

    std::vector<fs::path> paths;
    const std::string filename_pattern = pattern.filename().string();
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, ec)) {
        if (ec) break;
        if (entry.is_regular_file() &&
            wildcard_match(entry.path().filename().string(), filename_pattern)) {
            paths.push_back(entry.path().lexically_normal());
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

uint64_t hash_bytes(uint64_t hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<uint64_t>(bytes[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

uint64_t geometry_hash(const apricot::StaticEmesh& mesh) {
    uint64_t hash = 1469598103934665603ull;
    hash = hash_bytes(hash, mesh.vertices.data(),
                      mesh.vertices.size() * sizeof(apricot::EmeshVertex));
    return hash_bytes(hash, mesh.indices.data(),
                      mesh.indices.size() * sizeof(uint32_t));
}

void print_frame_report(const LoadedFrame& frame, std::size_t index) {
    float uv_min_u = std::numeric_limits<float>::max();
    float uv_min_v = std::numeric_limits<float>::max();
    float uv_max_u = -std::numeric_limits<float>::max();
    float uv_max_v = -std::numeric_limits<float>::max();
    float normal_min = std::numeric_limits<float>::max();
    float normal_max = 0.0f;
    for (const apricot::EmeshVertex& vertex : frame.source.vertices) {
        uv_min_u = std::min(uv_min_u, vertex.u);
        uv_min_v = std::min(uv_min_v, vertex.v);
        uv_max_u = std::max(uv_max_u, vertex.u);
        uv_max_v = std::max(uv_max_v, vertex.v);
        const float normal_length = std::sqrt(vertex.nx * vertex.nx +
                                              vertex.ny * vertex.ny +
                                              vertex.nz * vertex.nz);
        normal_min = std::min(normal_min, normal_length);
        normal_max = std::max(normal_max, normal_length);
    }
    const glm::vec3 size = frame.source.bounds.size();
    std::printf(
        "frame %zu: %s\n"
        "  %zu vertices, %zu triangles, bounds %.3f x %.3f x %.3f, "
        "UV [%.3f %.3f]-[%.3f %.3f], normals %.3f-%.3f, hash %016llx\n",
        index, frame.path.c_str(), frame.source.vertices.size(),
        frame.source.indices.size() / 3u, static_cast<double>(size.x),
        static_cast<double>(size.y), static_cast<double>(size.z),
        static_cast<double>(uv_min_u), static_cast<double>(uv_min_v),
        static_cast<double>(uv_max_u), static_cast<double>(uv_max_v),
        static_cast<double>(normal_min), static_cast<double>(normal_max),
        static_cast<unsigned long long>(frame.geometry_hash));
}

bool save_screenshot(const apricot::Window& window, const std::string& path) {
    const int width = window.width();
    const int height = window.height();
    if (width <= 0 || height <= 0) return false;

    std::vector<uint8_t> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4u;
    for (int y = 0; y < height / 2; ++y) {
        uint8_t* top = pixels.data() + static_cast<std::size_t>(y) * row_bytes;
        uint8_t* bottom = pixels.data() +
            static_cast<std::size_t>(height - 1 - y) * row_bytes;
        std::swap_ranges(top, top + row_bytes, bottom);
    }

    const fs::path destination(path);
    std::error_code ec;
    if (!destination.parent_path().empty()) {
        fs::create_directories(destination.parent_path(), ec);
    }
    bool saved = false;
    if (destination.extension() == ".png") {
        saved = stbi_write_png(path.c_str(), width, height, 4, pixels.data(),
                               width * 4) != 0;
    } else {
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
            pixels.data(), width, height, 32, width * 4,
            SDL_PIXELFORMAT_RGBA32);
        if (!surface) return false;
        saved = SDL_SaveBMP(surface, path.c_str()) == 0;
        SDL_FreeSurface(surface);
    }
    if (saved) std::printf("screenshot: %s\n", path.c_str());
    else std::fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());
    return saved;
}

const char* gl_error_name(GLenum error) {
    switch (error) {
        case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            return "GL_INVALID_FRAMEBUFFER_OPERATION";
        case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
        default: return "GL_<unknown>";
    }
}

int drain_gl_errors() {
    int count = 0;
    for (int i = 0; i < 32; ++i) {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR) break;
        std::fprintf(stderr, "GL error %s (0x%04X)\n", gl_error_name(error),
                     static_cast<unsigned>(error));
        ++count;
    }
    return count;
}

apricot::Transform display_transform(const apricot::AABB& bounds, float scale,
                                     float yaw_radians) {
    apricot::Transform transform;
    transform.scale = glm::vec3{scale};
    transform.rotation = glm::angleAxis(yaw_radians,
                                        glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::vec3 floor_pivot{bounds.center().x, bounds.min.y,
                                bounds.center().z};
    transform.position = -(transform.rotation * (transform.scale * floor_pivot));
    return transform;
}

void add_axis(apricot::Scene& scene, apricot::MeshId mesh,
              apricot::MaterialId material, const apricot::AABB& bounds,
              glm::vec3 position, glm::vec3 scale, glm::vec4 tint) {
    apricot::Renderable renderable;
    renderable.mesh = mesh;
    renderable.material = material;
    renderable.tint = tint;
    apricot::Transform transform;
    transform.position = position;
    transform.scale = scale;
    scene.create(renderable, transform, bounds);
}

void update_title(apricot::Window& window, const std::vector<LoadedFrame>& frames,
                  std::size_t frame, bool playing, float yaw_degrees) {
    const std::string name = frames.empty()
        ? std::string("texture")
        : fs::path(frames[frame].path).filename().string();
    char title[512];
    std::snprintf(title, sizeof(title),
                  "Apricot Asset Lab | %s | frame %zu/%zu | %s | yaw %.1f",
                  name.c_str(), frames.empty() ? 1u : frame + 1u,
                  frames.empty() ? 1u : frames.size(),
                  playing ? "playing" : "paused",
                  static_cast<double>(yaw_degrees));
    SDL_SetWindowTitle(window.sdl(), title);
}

int run(const Options& original) {
    Options options = original;
    if (!options.asset_dir.empty()) {
        const fs::path directory = existing_path(options.asset_dir);
        if (directory.empty() || !fs::is_directory(directory)) {
            std::fprintf(stderr, "asset directory not found: %s\n",
                         options.asset_dir.c_str());
            return 2;
        }
        const std::string pattern = options.animation.empty()
            ? (directory / "body.emesh").string()
            : (directory / (options.animation + "_*.emesh")).string();
        options.mesh_specs.push_back(pattern);
        if (options.texture.empty()) {
            options.texture = (directory / "body.png").string();
        }
    }

    std::vector<fs::path> mesh_paths;
    for (const std::string& spec : options.mesh_specs) {
        std::vector<fs::path> matches = expand_spec(spec);
        if (matches.empty()) {
            std::fprintf(stderr, "no cooked meshes matched: %s\n", spec.c_str());
            return 2;
        }
        mesh_paths.insert(mesh_paths.end(), matches.begin(), matches.end());
    }
    std::sort(mesh_paths.begin(), mesh_paths.end());
    mesh_paths.erase(std::unique(mesh_paths.begin(), mesh_paths.end()),
                     mesh_paths.end());

    fs::path texture_path;
    if (!options.texture.empty()) {
        texture_path = existing_path(options.texture);
        if (texture_path.empty() || !fs::is_regular_file(texture_path)) {
            std::fprintf(stderr, "texture not found: %s\n", options.texture.c_str());
            return 2;
        }
    }
    if (mesh_paths.empty() && texture_path.empty()) return 2;

    apricot::Window window;
    apricot::WindowConfig window_config;
    window_config.title = "Apricot Asset Lab";
    window_config.width = 960;
    window_config.height = 720;
    window_config.vsync = options.frame_limit <= 0;
    if (!window.init(window_config)) return 1;

    apricot::Renderer renderer;
    if (!renderer.init()) return 1;

    std::vector<LoadedFrame> frames;
    apricot::AABB union_bounds;
    bool clip_consistent = true;
    std::size_t expected_vertices = 0;
    std::size_t expected_indices = 0;
    for (const fs::path& path : mesh_paths) {
        LoadedFrame frame;
        frame.path = path.string();
        if (!apricot::read_static_emesh(frame.path, frame.source)) return 1;
        frame.geometry_hash = geometry_hash(frame.source);
        frame.gpu = renderer.add_mesh(frame.source);
        if (frame.gpu == apricot::kInvalidId) return 1;
        union_bounds.expand(frame.source.bounds);
        if (frames.empty()) {
            expected_vertices = frame.source.vertices.size();
            expected_indices = frame.source.indices.size();
        } else if (frame.source.vertices.size() != expected_vertices ||
                   frame.source.indices.size() != expected_indices) {
            clip_consistent = false;
        }
        frames.push_back(std::move(frame));
    }

    std::set<uint64_t> variants;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        variants.insert(frames[i].geometry_hash);
        print_frame_report(frames[i], i);
    }
    if (frames.size() > 1u) {
        std::printf("clip: %zu frames, %zu distinct poses, topology %s\n",
                    frames.size(), variants.size(),
                    clip_consistent ? "consistent" : "CHANGED");
        if (variants.size() < 2u) {
            std::fprintf(stderr, "clip check failed: every frame is identical\n");
            clip_consistent = false;
        }
    }

    apricot::Texture texture;
    int texture_width = 1;
    int texture_height = 1;
    if (!texture_path.empty()) {
        if (!texture.load_file(texture_path.string())) return 1;
        texture_width = texture.width();
        texture_height = texture.height();
        std::printf("texture: %s (%d x %d)\n", texture_path.string().c_str(),
                    texture_width, texture_height);
    } else if (!texture.make_checker(64, 8, {0.9f, 0.1f, 0.9f},
                                              {0.08f, 0.08f, 0.08f})) {
        return 1;
    }
    const apricot::MaterialId material = renderer.add_material(std::move(texture));

    apricot::Scene scene;
    apricot::Renderable subject_renderable;
    apricot::NodeId surface_glow = apricot::kInvalidId;
    subject_renderable.material = material;
    apricot::NodeId subject = apricot::kInvalidId;
    float model_scale = 1.0f;
    float display_span = 2.0f;
    const bool texture_only = frames.empty();

    if (!texture_only) {
        const float source_height = union_bounds.size().y;
        if (!(source_height > 1e-5f)) {
            std::fprintf(stderr, "model has no usable height\n");
            return 1;
        }
        if (options.display_height > 0.0f) {
            model_scale = options.display_height / source_height;
        }
        display_span = std::max({union_bounds.size().x, union_bounds.size().y,
                                 union_bounds.size().z}) * model_scale;
        subject_renderable.mesh = frames.front().gpu;
        const apricot::Transform transform = display_transform(
            union_bounds, model_scale, options.yaw_degrees * kPi / 180.0f);
        subject = scene.create(subject_renderable, transform, union_bounds);
        if (options.damage_zone >= 0 || options.damage_contact_set || options.lamp_preview >= 0) {
            apricot::VehicleDamageState damage;
            if (options.damage_zone >= 0)
                damage.zones[static_cast<std::size_t>(options.damage_zone)] = options.damage_strength;
            if (options.damage_contact_set) {
                auto& stamp = damage.stamps[0];
                stamp.contact_xz = {options.damage_contact.x, options.damage_contact.z};
                stamp.height = options.damage_contact.y;
                stamp.severity = options.damage_strength;
                stamp.radius = 0.35f;
            }
            const glm::vec3 centre = union_bounds.center();
            const glm::vec3 half = union_bounds.extents();
            if (auto* node = scene.get(subject)) {
                node->renderable.body_damage0 = apricot::pack_vehicle_damage0(damage);
                node->renderable.body_damage1 = glm::vec4{
                    apricot::pack_vehicle_damage1(damage), centre.x, centre.z};
                node->renderable.deform_frame = {
                    1.0f / std::max(half.x, 0.001f),
                    1.0f / std::max(half.z, 0.001f), centre.y,
                    1.0f / std::max(half.y, 0.001f)};
                if (options.lamp_preview >= 0) {
                    auto glow = node->renderable;
                    glow.uv_scale = apricot::vehicle_lamp_surface_uv(
                        static_cast<std::size_t>(options.lamp_preview),
                        apricot::vehicle_headlight_profile(frames.front().path).id);
                    glow.material = material;
                    const bool front = options.lamp_preview < 2;
                    const glm::vec3 colour = (front ? glm::vec3{1.0f,.88f,.65f}
                        : glm::vec3{1.0f,.025f,.012f}) * glm::mix(.72f,1.0f,options.lamp_power);
                    glow.tint = glm::vec4{colour,1.0f+options.lamp_power*(front ? 1.8f : 2.2f)};
                    surface_glow = scene.create(glow, transform, union_bounds);
                    if (auto* lamp = scene.get(surface_glow)) lamp->visible = options.lamp_power > .01f;
                }
            }
        }

        const apricot::MeshData unit_box = apricot::make_box(glm::vec3{0.5f});
        const apricot::MeshId axis_mesh = renderer.add_mesh(unit_box);
        const float axis_length = std::max(display_span * 0.72f, 0.5f);
        const float thickness = std::max(display_span * 0.012f, 0.01f);
        const apricot::MaterialId white = renderer.white_material();
        add_axis(scene, axis_mesh, white, unit_box.bounds,
                 {axis_length * 0.5f, thickness, 0.0f},
                 {axis_length, thickness * 2.0f, thickness * 2.0f},
                 {0.95f, 0.12f, 0.12f, 1.0f});
        add_axis(scene, axis_mesh, white, unit_box.bounds,
                 {0.0f, axis_length * 0.5f, 0.0f},
                 {thickness * 2.0f, axis_length, thickness * 2.0f},
                 {0.12f, 0.95f, 0.20f, 1.0f});
        add_axis(scene, axis_mesh, white, unit_box.bounds,
                 {0.0f, thickness, -axis_length * 0.5f},
                 {thickness * 2.0f, thickness * 2.0f, axis_length},
                 {0.12f, 0.38f, 1.0f, 1.0f});
        add_axis(scene, axis_mesh, white, unit_box.bounds,
                 {0.0f, thickness, -axis_length},
                 {thickness * 6.0f, thickness * 2.0f, thickness * 6.0f},
                 {0.12f, 0.38f, 1.0f, 1.0f});
    } else {
        const apricot::MeshData quad = apricot::make_billboard_quad();
        subject_renderable.mesh = renderer.add_mesh(quad);
        apricot::Transform transform;
        const float aspect = static_cast<float>(texture_width) /
                             static_cast<float>(texture_height);
        transform.position.z = -0.5f;
        transform.scale = {2.0f * aspect, 2.0f, 1.0f};
        subject = scene.create(subject_renderable, transform, quad.bounds);
        display_span = std::max(2.0f, 2.0f * aspect);
    }

    apricot::Camera camera;
    camera.aspect = window.aspect();
    camera.near_plane = 0.05f;
    camera.far_plane = 100.0f;
    const float camera_distance = std::max(display_span * 1.9f, 2.5f);
    const float subject_height = texture_only
        ? 2.0f : union_bounds.size().y * model_scale;
    const glm::vec3 target{0.0f, subject_height * 0.5f, 0.0f};
    if (texture_only) {
        camera.position = {0.0f, target.y, camera_distance};
    } else {
        camera.position = {camera_distance * 0.42f,
                           target.y + display_span * 0.12f, -camera_distance};
    }

    const auto point_camera = [&] {
        const glm::vec3 direction = glm::normalize(target - camera.position);
        camera.yaw = std::atan2(direction.x, -direction.z);
        camera.pitch = std::asin(glm::clamp(direction.y, -1.0f, 1.0f));
    };
    point_camera();

    apricot::SkyEnv environment = apricot::compute_sky_env(0.48f);
    environment.fog_density = 0.0f;
    environment.fog_start = 0.0f;
    environment.fog_end = 0.0f;
    if (texture_only) {
        environment.ambient = glm::vec3{1.0f};
        environment.light_color = glm::vec3{0.0f};
        environment.specular_strength = 0.0f;
    } else {
        environment.ambient = glm::vec3{0.42f};
        environment.light_color = glm::vec3{0.92f};
    }
    apricot::HeadlightRig headlights;
    apricot::CanopyLightRig canopy;
    canopy.intensity = 0.0f;
    apricot::Renderer::Options render_options;
    render_options.instancing = options.instancing;

    bool running = true;
    bool playing = options.pinned_frame < 0;
    bool save_requested = false;
    int rendered = 0;
    int gl_errors = 0;
    float animation_seconds = 0.0f;
    float interactive_yaw = 0.0f;
    float zoom = options.initial_zoom;
    std::size_t manual_frame = options.pinned_frame >= 0
        ? static_cast<std::size_t>(options.pinned_frame) : 0u;
    Clock::time_point previous_time = Clock::now();

    while (running) {
        const Clock::time_point now = Clock::now();
        float delta = std::chrono::duration<float>(now - previous_time).count();
        previous_time = now;
        delta = std::clamp(delta, 0.0f, 0.1f);
        if (options.frame_limit > 0) delta = 1.0f / 60.0f;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                int width = 0;
                int height = 0;
                SDL_GL_GetDrawableSize(window.sdl(), &width, &height);
                window.on_resize(width, height);
                camera.aspect = window.aspect();
            }
            if (event.type != SDL_KEYDOWN || event.key.repeat != 0) continue;
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE: running = false; break;
                case SDLK_SPACE: playing = !playing; break;
                case SDLK_LEFT:
                    playing = false;
                    if (!frames.empty()) {
                        manual_frame = (manual_frame + frames.size() - 1u) %
                                       frames.size();
                    }
                    break;
                case SDLK_RIGHT:
                    playing = false;
                    if (!frames.empty()) {
                        manual_frame = (manual_frame + 1u) % frames.size();
                    }
                    break;
                case SDLK_r:
                    interactive_yaw = 0.0f;
                    zoom = 1.0f;
                    playing = options.pinned_frame < 0;
                    manual_frame = options.pinned_frame >= 0
                        ? static_cast<std::size_t>(options.pinned_frame) : 0u;
                    animation_seconds = 0.0f;
                    break;
                case SDLK_s: save_requested = true; break;
                default: break;
            }
        }

        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_A]) interactive_yaw += 75.0f * delta;
        if (keys[SDL_SCANCODE_D]) interactive_yaw -= 75.0f * delta;
        if (keys[SDL_SCANCODE_Q]) zoom = std::min(zoom + delta, 2.5f);
        if (keys[SDL_SCANCODE_E]) zoom = std::max(zoom - delta, 0.35f);

        if (playing) animation_seconds += delta;
        interactive_yaw += options.turntable_degrees_per_second * delta;
        std::size_t frame_index = manual_frame;
        if (!frames.empty()) {
            if (playing) {
                const auto played = static_cast<uint64_t>(
                    std::floor(animation_seconds * options.fps));
                frame_index = static_cast<std::size_t>(played % frames.size());
            } else {
                frame_index %= frames.size();
            }
            if (apricot::SceneNode* node = scene.get(subject)) {
                node->renderable.mesh = frames[frame_index].gpu;
            }
            const float yaw = (options.yaw_degrees + interactive_yaw) *
                              kPi / 180.0f;
            scene.set_transform(subject,
                                display_transform(union_bounds, model_scale, yaw));
            if (auto* glow = scene.get(surface_glow)) {
                glow->renderable.mesh = frames[frame_index].gpu;
                scene.set_transform(surface_glow,
                                    display_transform(union_bounds, model_scale, yaw));
            }
        }

        glm::vec3 base_position;
        if (texture_only) {
            base_position = {0.0f, target.y, camera_distance};
        } else {
            base_position = {camera_distance * 0.42f,
                             target.y + display_span * 0.12f, -camera_distance};
        }
        camera.position = target + (base_position - target) * zoom;
        point_camera();

        scene.update();
        if (!window.minimised()) {
            window.apply_viewport();
            glClearColor(0.035f, 0.042f, 0.055f, 1.0f);
            glClear(static_cast<GLbitfield>(GL_COLOR_BUFFER_BIT |
                                           GL_DEPTH_BUFFER_BIT));
            const apricot::Scene::CullResult& culled = scene.cull(
                camera.frustum(), camera.position, 100.0f);
            renderer.render(scene, culled.visible, camera, environment,
                            headlights, canopy, render_options);
            if (rendered < 8) gl_errors += drain_gl_errors();

            const bool final_bounded_frame = options.frame_limit > 0 &&
                rendered + 1 >= options.frame_limit;
            if (save_requested ||
                (final_bounded_frame && !options.screenshot.empty())) {
                const std::string destination = !options.screenshot.empty()
                    ? options.screenshot : "build/asset-lab.png";
                if (!save_screenshot(window, destination)) return 1;
                save_requested = false;
            }
            window.swap();
            ++rendered;
        }

        update_title(window, frames, frame_index, playing,
                     options.yaw_degrees + interactive_yaw);
        if (options.frame_limit > 0 && rendered >= options.frame_limit) {
            running = false;
        }
    }

    gl_errors += drain_gl_errors();
    std::printf("asset lab: %d frames, %zu assets, %d GL errors\n",
                rendered, frames.size() + (texture_path.empty() ? 0u : 1u),
                gl_errors);
    renderer.destroy();
    window.shutdown();
    return clip_consistent && gl_errors == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        print_usage();
        return 2;
    }
    return run(options);
}
