#include <SDL.h>
#include <glad/gl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "app/game_ui.h"
#include "game/road_name.h"
#include "game/ui_canvas.h"
#include "gfx/hud.h"
#include "platform/window.h"

int main(int argc, char** argv) {
    float zoom = 1.0f;
    bool zoom_was_set = false;
    float wheel_steps = 0.0f;
    glm::vec2 cursor{-1.0f, -1.0f};
    int layer = 0;
    bool minimap = false;
    bool waypoint = false;
    apricot::GameUiSnapshot snapshot;
    apricot::WindowConfig config;
    config.title = "Probable Cause - Map Lab";
    config.vsync = false;
    std::string screenshot;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--minimap") { minimap = true; continue; }
        if (arg == "--waypoint") { waypoint = true; continue; }
        if (arg == "--help") {
            std::puts("apricot_map_lab --zoom 1..16 --x METRES --z METRES "
                      "--width POINTS --height POINTS --layer 0..2 --screenshot FILE.png "
                      "--wheel-steps STEPS --cursor-x CANVAS_PX --cursor-y CANVAS_PX "
                      "--minimap --waypoint --heading DEGREES --speed MPH --mission-x X --mission-z Z");
            return 0;
        }
        if (i + 1 >= argc) return 2;
        const char* value = argv[++i];
        if (arg == "--screenshot") { screenshot = value; continue; }
        char* end = nullptr;
        const float number = std::strtof(value, &end);
        if (end == value || *end != '\0' || !std::isfinite(number)) return 2;
        if (arg == "--zoom") {
            zoom = std::clamp(number, 1.0f, 16.0f);
            zoom_was_set = true;
        }
        else if (arg == "--heading") {
            const float radians = number * 3.14159265359f / 180.0f;
            snapshot.player_forward = {std::sin(radians),0,-std::cos(radians)};
        }
        else if (arg == "--speed") snapshot.speed_mph = number;
        else if (arg == "--mission-x" || arg == "--mission-z") {
            if (!snapshot.mission_target) snapshot.mission_target = glm::vec2{};
            (*snapshot.mission_target)[arg == "--mission-x" ? 0 : 1] = number;
        }
        else if (arg == "--wheel-steps") wheel_steps = number;
        else if (arg == "--cursor-x") cursor.x = number;
        else if (arg == "--cursor-y") cursor.y = number;
        else if (arg == "--layer" && number >= 0 && number <= 2)
            layer = static_cast<int>(number);
        else if (arg == "--x") snapshot.player_position.x = number;
        else if (arg == "--z") snapshot.player_position.z = number;
        else if (arg == "--width" && number >= 320 && number <= 3840)
            config.width = static_cast<int>(number);
        else if (arg == "--height" && number >= 240 && number <= 2160)
            config.height = static_cast<int>(number);
        else return 2;
    }
    apricot::Window window;
    if (!window.init(config)) return 1;
    apricot::Hud hud;
    if (!hud.init()) return 1;
    apricot::GameUi map;
    apricot::CurrentRoadName road_name;
    snapshot.road_name = road_name.update(
        {snapshot.player_position.x, snapshot.player_position.z});
    const auto build_start = std::chrono::steady_clock::now();
    map.build_map();
    const double build_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - build_start).count();
    apricot::UiFlow flow;
    flow.update(apricot::kBtnMap);
    for (int i = 0; i < layer; ++i) flow.update(apricot::kBtnCamCycle);
    const auto canvas = apricot::UiCanvas::from_drawable(
        {static_cast<float>(window.width()), static_cast<float>(window.height())});
    if (!zoom_was_set)
        map.open_map_view(snapshot.player_position, canvas.size);
    const float steps = std::log(zoom) / std::log(1.0f / 0.72f);
    for (int i = 0; i < 180; ++i) {
        map.update_map({}, i == 0 ? steps : 0.0f, {}, i == 0,
                       snapshot.player_position, 1.0f / 60.0f, canvas.size);
    }
    double draw_ms = 0.0;
    for (int i = 0; i < 120; ++i) {
        map.update_map({}, 0.0f, {}, false, snapshot.player_position,
                       1.0f / 60.0f, canvas.size, cursor,
                       i == 0 ? wheel_steps : 0.0f);
    }
    for (int i = 0; i < 36; ++i) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) if (event.type == SDL_QUIT) return 0;
        const auto start = std::chrono::steady_clock::now();
        window.apply_viewport();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        hud.begin(canvas.size);
        if (i == 0 && waypoint) map.toggle_waypoint(
            cursor.x >= 0 ? cursor : glm::vec2{canvas.size.x*.6f,canvas.size.y*.5f}, canvas.size);
        if (minimap) map.draw_minimap(hud, flow, snapshot, canvas.size);
        else map.draw(hud, flow, snapshot, canvas.size);
        hud.end();
        glFinish();
        if (i >= 6) draw_ms += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        if (i == 35 && !screenshot.empty()) {
            const int w = window.width(), h = window.height();
            std::vector<unsigned char> pixels(static_cast<std::size_t>(w) *
                                              static_cast<std::size_t>(h) * 4u);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            stbi_flip_vertically_on_write(1);
            if (!stbi_write_png(screenshot.c_str(), w, h, 4, pixels.data(), w * 4))
                return 1;
        }
        window.swap();
    }
    const bool clean = glGetError() == GL_NO_ERROR;
    std::printf("map %.1fx: build %.2f ms, render %.2f ms, %d HUD quads, "
                "%d draw(s), GL %s\n", static_cast<double>(zoom), build_ms,
                draw_ms / 30.0, hud.last_quad_count(), hud.last_draw_calls(),
                clean ? "clean" : "ERROR");
    return clean ? 0 : 1;
}
