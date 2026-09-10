// Apricot Character Lab: direct rig + texture + animation inspection through
// the exact runtime loader, interpolator, dual-quaternion palette and shader.

#include <SDL.h>
#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "app/character_animation.h"
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "core/skeletal_animation.h"
#include "core/transform.h"
#include "gfx/camera.h"
#include "gfx/lighting.h"
#include "gfx/primitives.h"
#include "gfx/renderer.h"
#include "gfx/shader.h"
#include "gfx/skinned_mesh.h"
#include "gfx/sky.h"
#include "gfx/sky_env.h"
#include "gfx/texture.h"
#include "platform/window.h"
#include "scene/scene.h"

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr float kPi = 3.14159265358979323846f;

struct Options {
    std::string model;
    std::string skeleton;
    std::string animation;
    std::string texture;
    std::string screenshot;
    std::string clip_name;
    int frame_limit = 0;
    float height = 1.76f;
    float yaw_degrees = 180.0f;
    float playback_speed = 1.0f;
    bool bind_pose = false;
    bool states = false;
};

// --states drives the REAL CharacterAnimator through a scripted timeline, so
// what the window shows is the state machine the game runs, crossfades and all
// — not a hand-rolled clip player that agrees with it until somebody edits one.
// Times are timeline seconds; at 60 Hz, --frames N lands on N/60 s, which makes
// --screenshot repeatable at a chosen moment.
struct ScriptStep {
    float at_seconds;
    const char* label;
    apricot::CharacterAnimInput input;
};

apricot::CharacterAnimInput make_input(float speed, bool sprinting) {
    apricot::CharacterAnimInput input;
    input.identity = 0x1EAFBEEFull;
    input.speed_mps = speed;
    input.sprinting = sprinting;
    return input;
}

apricot::CharacterAnimInput punch_input() {
    apricot::CharacterAnimInput input = make_input(0.0f, false);
    input.punch = true;
    return input;
}

apricot::CharacterAnimInput flinch_input() {
    apricot::CharacterAnimInput input = make_input(0.0f, false);
    input.flinch = true;
    return input;
}

apricot::CharacterAnimInput downed_input(bool down) {
    apricot::CharacterAnimInput input = make_input(0.0f, false);
    input.downed = down;
    return input;
}

apricot::CharacterAnimInput dead_input() {
    apricot::CharacterAnimInput input = make_input(0.0f, false);
    input.dead = true;
    return input;
}

const std::vector<ScriptStep>& script() {
    static const std::vector<ScriptStep> steps = {
        { 0.0f, "idle",            make_input(0.0f, false)},
        { 4.0f, "walk",            make_input(1.6f, false)},
        { 8.0f, "sprint",          make_input(6.25f, true)},
        {12.0f, "idle",            make_input(0.0f, false)},
        {13.0f, "punch (right)",   punch_input()},
        {13.1f, "idle",            make_input(0.0f, false)},
        {14.0f, "punch (left)",    punch_input()},
        {14.1f, "idle",            make_input(0.0f, false)},
        {15.0f, "flinch",          flinch_input()},
        {15.1f, "idle",            make_input(0.0f, false)},
        {17.0f, "knocked down",    downed_input(true)},
        {23.0f, "get up",          downed_input(false)},
        {28.0f, "walk",            make_input(1.6f, false)},
        {31.0f, "die",             dead_input()},
        {40.0f, "idle (restart)",  make_input(0.0f, false)},
    };
    return steps;
}

std::size_t script_index(float seconds) {
    const std::vector<ScriptStep>& steps = script();
    std::size_t index = 0;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        if (seconds + 1e-4f >= steps[i].at_seconds) index = i;
    }
    return index;
}

float script_length() { return script().back().at_seconds; }

void usage() {
    std::printf(
        "Apricot Character Lab\n\n"
        "  apricot_character_lab --asset-dir DIR --clip walk [options]\n"
        "  apricot_character_lab --model FILE --skeleton FILE "
        "--animation FILE --texture FILE [options]\n\n"
        "Input:\n"
        "  --asset-dir DIR  directory containing skin.emesh, skin.eskel, body.png\n"
        "  --clip NAME      use ../animations/NAME.eanim with --asset-dir\n"
        "  --model FILE     rigged .emesh\n"
        "  --skeleton FILE  matching .eskel\n"
        "  --animation FILE .eanim clip\n"
        "  --texture FILE   diffuse PNG\n\n"
        "View:\n"
        "  --states         drive the real CharacterAnimator through a scripted\n"
        "                   idle/walk/sprint/punch/flinch/knockdown/get-up/die\n"
        "                   timeline instead of looping one clip\n"
        "  --height METRES  displayed character height (default 1.76)\n"
        "  --yaw DEGREES    source-facing correction (default 180)\n"
        "  --speed SCALE    clip playback speed (default 1)\n"
        "  --bind           hold the skeleton's bind pose\n"
        "  --frames N       deterministic bounded run\n"
        "  --screenshot PNG save final bounded frame\n\n"
        "Controls: A/D rotate, Q/E zoom, Space pause, arrows scrub, R reset, "
        "Esc quits.\n");
}

bool parse_int(const char* text, int& output) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || value <= 0 || value > 1000000) return false;
    output = static_cast<int>(value);
    return true;
}

bool parse_float(const char* text, float& output) {
    char* end = nullptr;
    output = std::strtof(text, &end);
    return end && *end == '\0' && std::isfinite(output);
}

bool take_value(int argc, char** argv, int& index, const char*& output) {
    if (index + 1 >= argc) return false;
    output = argv[++index];
    return true;
}

bool parse(int argc, char** argv, Options& output) {
    std::string asset_dir;
    std::string clip;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            usage();
            std::exit(0);
        }
        if (argument == "--bind") {
            output.bind_pose = true;
            continue;
        }
        if (argument == "--states") {
            output.states = true;
            continue;
        }
        const char* value = nullptr;
        if (!take_value(argc, argv, i, value)) {
            std::fprintf(stderr, "%s needs a value\n", argument.c_str());
            return false;
        }
        if (argument == "--asset-dir") asset_dir = value;
        else if (argument == "--clip") clip = value;
        else if (argument == "--model") output.model = value;
        else if (argument == "--skeleton") output.skeleton = value;
        else if (argument == "--animation") output.animation = value;
        else if (argument == "--texture") output.texture = value;
        else if (argument == "--screenshot") output.screenshot = value;
        else if (argument == "--frames") {
            if (!parse_int(value, output.frame_limit)) return false;
        } else if (argument == "--height") {
            if (!parse_float(value, output.height) || output.height <= 0.0f) {
                return false;
            }
        } else if (argument == "--yaw") {
            if (!parse_float(value, output.yaw_degrees)) return false;
        } else if (argument == "--speed") {
            if (!parse_float(value, output.playback_speed) ||
                output.playback_speed <= 0.0f) {
                return false;
            }
        } else {
            std::fprintf(stderr, "unknown option: %s\n", argument.c_str());
            return false;
        }
    }
    if (!asset_dir.empty()) {
        const fs::path directory(asset_dir);
        output.model = (directory / "skin.emesh").string();
        output.skeleton = (directory / "skin.eskel").string();
        output.texture = (directory / "body.png").string();
        if (clip.empty()) clip = "walk";
        output.clip_name = clip;
        output.animation = (directory.parent_path() / "animations" /
                            (clip + ".eanim")).string();
    }
    if (output.model.empty() || output.skeleton.empty() ||
        output.animation.empty() || output.texture.empty()) {
        std::fprintf(stderr, "give --asset-dir or all four explicit inputs\n");
        return false;
    }
    if (!output.screenshot.empty() && output.frame_limit <= 0) {
        std::fprintf(stderr, "--screenshot needs --frames\n");
        return false;
    }
    return true;
}

fs::path resolve_path(const std::string& input) {
    fs::path path(input);
    std::error_code error;
    if (fs::is_regular_file(path, error)) return path.lexically_normal();
    if (!path.is_absolute()) {
        path = fs::path(apricot::asset_root()) / path;
        if (fs::is_regular_file(path, error)) return path.lexically_normal();
    }
    return {};
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

bool save_screenshot(const apricot::Window& window, const std::string& path) {
    const int width = window.width();
    const int height = window.height();
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4u;
    for (int y = 0; y < height / 2; ++y) {
        unsigned char* top = pixels.data() +
            static_cast<std::size_t>(y) * row_bytes;
        unsigned char* bottom = pixels.data() +
            static_cast<std::size_t>(height - 1 - y) * row_bytes;
        std::swap_ranges(top, top + row_bytes, bottom);
    }
    const fs::path destination(path);
    std::error_code error;
    if (!destination.parent_path().empty()) {
        fs::create_directories(destination.parent_path(), error);
    }
    return stbi_write_png(path.c_str(), width, height, 4, pixels.data(),
                          width * 4) != 0;
}

int drain_gl_errors() {
    int count = 0;
    for (int i = 0; i < 32; ++i) {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR) break;
        std::fprintf(stderr, "GL error 0x%04X\n", static_cast<unsigned>(error));
        ++count;
    }
    return count;
}

int run(const Options& options) {
    const fs::path model_path = resolve_path(options.model);
    const fs::path skeleton_path = resolve_path(options.skeleton);
    const fs::path animation_path = resolve_path(options.animation);
    const fs::path texture_path = resolve_path(options.texture);
    if (model_path.empty() || skeleton_path.empty() || animation_path.empty() ||
        texture_path.empty()) {
        std::fprintf(stderr, "one or more input files do not exist\n");
        return 2;
    }

    apricot::Window window;
    apricot::WindowConfig config;
    config.title = "Apricot Character Lab";
    config.width = 960;
    config.height = 720;
    config.vsync = options.frame_limit <= 0;
    if (!window.init(config)) return 1;

    apricot::SkinnedEmesh source;
    apricot::Skeleton skeleton;
    apricot::Animation animation;
    apricot::SkinnedMesh mesh;
    apricot::Texture texture;
    apricot::Shader shader;
    if (!apricot::read_skinned_emesh(model_path.string(), source) ||
        !skeleton.load(skeleton_path.string()) || !skeleton.accepts(source) ||
        !animation.load(animation_path.string(), skeleton) ||
        animation.unresolved_channels() != 0 ||
        !mesh.upload(source) || !texture.load_file(texture_path.string()) ||
        !shader.build_from_files("shaders/skinned_character.vert",
                                 "shaders/skinned_character.frag")) {
        return 1;
    }

    // Look the clip up in the registry so a single-clip preview obeys the SAME
    // root policy and plant rule the game does. Without it a death clip plays
    // with its horizontal travel stripped and collapses on the spot, and the
    // preview quietly disagrees with what the player will see.
    const apricot::CharacterClipInfo* registered = nullptr;
    for (const apricot::CharacterClipInfo& info : apricot::kCharacterClips) {
        if (options.clip_name == info.name) registered = &info;
    }
    const bool planted = registered ? registered->planted : true;
    const float model_scale = options.height / source.bounds.size().y;
    const float plant = (options.bind_pose || !planted) ? 0.0f
        : apricot::locomotion_plant_offset(
              source, skeleton, animation, model_scale);

    // --states needs the whole set bound to THIS skeleton, not the single clip.
    apricot::CharacterClipSet clips;
    apricot::CharacterAnimator animator;
    std::array<float, apricot::kCharacterClipCount> plants{};
    if (options.states) {
        if (!clips.load(skeleton)) {
            std::fprintf(stderr, "--states needs the full clip set; run "
                                 "tools/lift_character_animations.py\n");
            return 1;
        }
        for (const apricot::CharacterClipInfo& info : apricot::kCharacterClips) {
            plants[static_cast<std::size_t>(info.clip)] = info.planted
                ? apricot::locomotion_plant_offset(
                      source, skeleton, clips.clip(info.clip), model_scale)
                : 0.0f;
        }
    }
    std::printf("character: %zu vertices, %zu triangles, %d bones, clip %.3f s, "
                "plant %.4f m%s\n",
                source.vertices.size(), source.indices.size() / 3u,
                skeleton.bone_count(), static_cast<double>(animation.duration()),
                static_cast<double>(plant),
                options.states ? ", state machine" : "");

    apricot::Renderer renderer;
    if (!renderer.init()) return 1;
    apricot::Scene scene;
    const apricot::MeshData box = apricot::make_box(glm::vec3{0.5f});
    const apricot::MeshId axis_mesh = renderer.add_mesh(box);
    const apricot::MaterialId white = renderer.white_material();
    add_axis(scene, axis_mesh, white, box.bounds, {0.75f, 0.01f, 0.0f},
             {1.5f, 0.02f, 0.02f}, {0.95f, 0.12f, 0.12f, 1.0f});
    add_axis(scene, axis_mesh, white, box.bounds, {0.0f, 0.75f, 0.0f},
             {0.02f, 1.5f, 0.02f}, {0.12f, 0.95f, 0.20f, 1.0f});
    add_axis(scene, axis_mesh, white, box.bounds, {0.0f, 0.01f, -0.75f},
             {0.02f, 0.02f, 1.5f}, {0.12f, 0.38f, 1.0f, 1.0f});
    scene.update();

    apricot::Camera camera;
    camera.aspect = window.aspect();
    camera.near_plane = 0.05f;
    camera.far_plane = 100.0f;
    const glm::vec3 target{0.0f, options.height * 0.5f, 0.0f};
    const glm::vec3 base_camera{1.15f, options.height * 0.62f, -3.25f};
    const auto point_camera = [&] {
        const glm::vec3 direction = glm::normalize(target - camera.position);
        camera.yaw = std::atan2(direction.x, -direction.z);
        camera.pitch = std::asin(glm::clamp(direction.y, -1.0f, 1.0f));
    };

    apricot::SkyEnv environment = apricot::compute_sky_env(0.48f);
    environment.ambient = glm::vec3{0.42f};
    environment.light_color = glm::vec3{0.92f};
    environment.fog_density = 0.0f;
    environment.fog_start = 0.0f;
    environment.fog_end = 0.0f;
    apricot::HeadlightRig headlights;
    apricot::CanopyLightRig canopy;
    canopy.intensity = 0.0f;
    apricot::Renderer::Options renderer_options;

    std::vector<glm::mat4> local;
    std::vector<glm::mat4> skin;
    std::vector<glm::vec4> dual_real;
    std::vector<glm::vec4> dual_part;
    std::vector<apricot::BonePose> parts_a;
    std::vector<apricot::BonePose> parts_b;
    std::size_t script_step = script().size();
    apricot::CharacterAnimState script_state = apricot::CharacterAnimState::Count;
    // A single-clip preview of a one-shot needs the same anchor frame the game
    // samples at load, or the preview and the game disagree about where the
    // body ends up.
    glm::vec2 clip_anchor{0.0f};
    if (registered && registered->root != apricot::ClipRoot::Strip) {
        std::vector<glm::mat4> reference;
        const float reference_time =
            registered->root == apricot::ClipRoot::AnchorEnd
                ? std::max(0.0f, animation.duration() -
                                     apricot::character_getup::SAMPLE_EPS)
                : 0.0f;
        animation.sample(reference_time, skeleton, reference);
        clip_anchor = apricot::root_translation_xz(skeleton, reference);
    }
    float animation_time = 0.0f;
    float interactive_yaw = 0.0f;
    float zoom = 1.0f;
    bool playing = true;
    bool running = true;
    int rendered = 0;
    int gl_errors = 0;
    Clock::time_point previous = Clock::now();

    while (running) {
        const Clock::time_point now = Clock::now();
        float delta = std::clamp(
            std::chrono::duration<float>(now - previous).count(), 0.0f, 0.1f);
        previous = now;
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
                case SDLK_LEFT: playing = false; animation_time -= 1.0f / 30.0f; break;
                case SDLK_RIGHT: playing = false; animation_time += 1.0f / 30.0f; break;
                case SDLK_r:
                    animation_time = 0.0f;
                    interactive_yaw = 0.0f;
                    zoom = 1.0f;
                    playing = true;
                    break;
                default: break;
            }
        }
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_A]) interactive_yaw += 75.0f * delta;
        if (keys[SDL_SCANCODE_D]) interactive_yaw -= 75.0f * delta;
        if (keys[SDL_SCANCODE_Q]) zoom = std::min(zoom + delta, 2.5f);
        if (keys[SDL_SCANCODE_E]) zoom = std::max(zoom - delta, 0.35f);
        if (playing) animation_time += delta * options.playback_speed;

        float frame_plant = plant;
        if (options.states) {
            const float timeline = std::fmod(std::max(animation_time, 0.0f),
                                             script_length());
            const std::size_t step = script_index(timeline);
            animator.advance(clips, script()[step].input,
                             playing ? delta * options.playback_speed : 0.0f);
            if (step != script_step || animator.state() != script_state) {
                script_step = step;
                script_state = animator.state();
                std::printf("%7.2f s  %-16s -> %-10s %s\n",
                            static_cast<double>(timeline),
                            script()[step].label,
                            apricot::character_anim_state_name(script_state),
                            apricot::character_clip_name(
                                animator.sample().clip));
                std::fflush(stdout);
            }
            if (animator.consume_punch_contact()) {
                std::printf("%7.2f s  CONTACT (%s fist)\n",
                            static_cast<double>(timeline),
                            animator.punching_right() ? "right" : "left");
                std::fflush(stdout);
            }
            apricot::evaluate_character_pose(clips, skeleton, animator.sample(),
                                             parts_a, parts_b, local);
            frame_plant = animator.plant(plants);
        } else if (options.bind_pose) {
            local.resize(static_cast<std::size_t>(skeleton.bone_count()));
            for (int bone = 0; bone < skeleton.bone_count(); ++bone) {
                local[static_cast<std::size_t>(bone)] =
                    skeleton.bone(bone).bind_local;
            }
        } else {
            animation.sample(animation_time, skeleton, local);
            if (registered && registered->root != apricot::ClipRoot::Strip) {
                apricot::anchor_root_motion_xz(
                    skeleton, local, clip_anchor);
            } else {
                apricot::strip_root_motion_xz(skeleton, local);
            }
        }
        skeleton.compute_skin_matrices(local, skin);
        apricot::skin_matrices_to_dual_quaternions(skin, dual_real, dual_part);

        apricot::Transform model;
        model.scale = glm::vec3{model_scale};
        model.rotation = glm::angleAxis(
            (options.yaw_degrees + interactive_yaw) * kPi / 180.0f,
            glm::vec3{0.0f, 1.0f, 0.0f});
        const glm::vec3 pivot{source.bounds.center().x, source.bounds.min.y,
                              source.bounds.center().z};
        model.position = -(model.rotation * (model.scale * pivot));
        model.position.y += frame_plant;

        camera.position = target + (base_camera - target) * zoom;
        point_camera();
        if (!window.minimised()) {
            window.apply_viewport();
            glClearColor(0.035f, 0.042f, 0.055f, 1.0f);
            glClear(static_cast<GLbitfield>(GL_COLOR_BUFFER_BIT |
                                           GL_DEPTH_BUFFER_BIT));
            const apricot::Scene::CullResult& visible = scene.cull(
                camera.frustum(), camera.position, 100.0f);
            renderer.render(scene, visible.visible, camera, environment,
                            headlights, canopy, renderer_options);

            shader.bind();
            shader.set_mat4("u_view_proj", camera.view_projection());
            shader.set_mat4("u_model", model.matrix());
            shader.set_int("u_diffuse", 0);
            shader.set_vec4("u_tint", glm::vec4{1.0f});
            shader.set_vec4_array("u_dq_real", dual_real.data(),
                                  static_cast<int>(dual_real.size()));
            shader.set_vec4_array("u_dq_dual", dual_part.data(),
                                  static_cast<int>(dual_part.size()));
            apricot::apply_lighting(shader, environment, camera.position,
                                    headlights, canopy);
            shader.set_float("u_specular_strength", 0.0f);
            texture.bind(0);
            mesh.draw();

            if (rendered < 8) gl_errors += drain_gl_errors();
            const bool final_frame = options.frame_limit > 0 &&
                                     rendered + 1 >= options.frame_limit;
            if (final_frame && !options.screenshot.empty() &&
                !save_screenshot(window, options.screenshot)) {
                std::fprintf(stderr, "screenshot failed: %s\n",
                             options.screenshot.c_str());
                return 1;
            }
            window.swap();
            ++rendered;
        }
        char title[512];
        if (options.states) {
            std::snprintf(title, sizeof(title),
                          "Apricot Character Lab | %s | %.2f s | %s | %s | %s",
                          model_path.filename().string().c_str(),
                          static_cast<double>(std::fmod(
                              std::max(animation_time, 0.0f), script_length())),
                          apricot::character_anim_state_name(animator.state()),
                          apricot::character_clip_name(animator.sample().clip),
                          playing ? "playing" : "paused");
        } else {
            std::snprintf(title, sizeof(title),
                          "Apricot Character Lab | %s | %.3f / %.3f s | %s",
                          model_path.filename().string().c_str(),
                          static_cast<double>(std::fmod(
                              std::max(animation_time, 0.0f), animation.duration())),
                          static_cast<double>(animation.duration()),
                          playing ? "playing" : "paused");
        }
        SDL_SetWindowTitle(window.sdl(), title);
        if (options.frame_limit > 0 && rendered >= options.frame_limit) {
            running = false;
        }
    }

    gl_errors += drain_gl_errors();
    std::printf("character lab: %d frames, %d GL errors\n", rendered, gl_errors);
    shader.destroy();
    mesh.destroy();
    texture.destroy();
    renderer.destroy();
    window.shutdown();
    return gl_errors == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse(argc, argv, options)) {
        usage();
        return 2;
    }
    return run(options);
}
