// Repeatable QA of the production title passes, including native fullscreen.
#include <SDL.h>
#include <glad/gl.h>
#include <cstdio>
#include <string>
#include <vector>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include "app/game_ui.h"
#include "game/intro_layout.h"
#include "game/ui_canvas.h"
#include "gfx/hud.h"
#include "gfx/loading_screen.h"
#include "platform/window.h"

int main(int argc, char** argv) {
    using namespace apricot;
    const std::string prefix = argc > 1 ? argv[1] : "/tmp/probable-cause-intro-qa";
    Window window;
    WindowConfig config;
    config.title = "Probable Cause - Intro QA";
    config.vsync = false;
    if (!window.init(config)) return 1;
    LoadingScreen intro;
    Hud hud;
    GameUi ui;
    UiFlow flow;
    if (!intro.init("textures/ui/probable-cause-intro-plate-v2.png") || !hud.init()) return 1;
    intro.mark_ready();
    const auto draw = [&]() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) if (event.type == SDL_QUIT) return false;
        int width, height;
        SDL_GL_GetDrawableSize(window.sdl(), &width, &height);
        window.on_resize(width, height);
        intro.render(width, height);
        auto canvas = UiCanvas::from_drawable({width, height});
        GameUiSnapshot snapshot;
        snapshot.title_opacity = intro.menu_opacity();
        hud.begin(canvas.size);
        ui.draw(hud, flow, snapshot, canvas.size);
        hud.end();
        return glGetError() == GL_NO_ERROR;
    };
    while (intro.elapsed() < 2.3f) {
        if (!draw()) return 1;
        window.swap();
        SDL_Delay(8);
    }
    struct Size { int width, height; const char* name; };
    for (const auto size : {Size{1280,720,"wide"}, Size{1000,1000,"tall"},
                            Size{1600,1000,"16x10"}, Size{0,0,"fullscreen"}}) {
        if (size.width) SDL_SetWindowSize(window.sdl(), size.width, size.height);
        else if (SDL_SetWindowFullscreen(window.sdl(), SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) return 1;
        for (int frame = 0; frame < 50; ++frame) {
            if (!draw()) return 1;
            window.swap();
            SDL_Delay(8);
        }
        if (!draw()) return 1;
        const int w = window.width(), h = window.height();
        std::vector<unsigned char> pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
        stbi_flip_vertically_on_write(1);
        const auto path = prefix + "-" + size.name + ".png";
        if (!stbi_write_png(path.c_str(),w,h,3,pixels.data(),w*3)) return 1;
        const auto canvas = UiCanvas::from_drawable({w,h});
        const auto layout = IntroLayout::from_canvas(canvas.size);
        int logical_w, logical_h;
        SDL_GetWindowSize(window.sdl(), &logical_w, &logical_h);
        for (int row = 0; row < 3; ++row) {
            const auto target = layout.menu_position + glm::vec2{80, row * layout.row_height + 30};
            const glm::vec2 logical{logical_w, logical_h};
            const auto pointer = canvas.from_window(target / canvas.size * logical, logical);
            if (ui.hit_test(flow, pointer, canvas.size) != row) return 1;
        }
        if (ui.hit_test(flow, {canvas.size.x - 20, canvas.size.y - 20}, canvas.size) != -1) return 1;
        std::printf("PASS %s %dx%d: title render, all pointer rows, GL clean; %s\n", size.name,w,h,path.c_str());
        window.swap();
    }
    flow.set_selection(1);
    flow.update(kBtnAccept);
    if (flow.screen() != UiScreen::Map) return 1;
    flow.update(kBtnBack);
    if (flow.screen() != UiScreen::Title) return 1;
    if (flow.update(kBtnAccept) != UiAction::BeginDrive) return 1;
    std::puts("PASS City Map returns to title and Start enters gameplay");
    return 0;
}
