#include "gfx/loading_screen.h"

#include "core/asset_root.h"
#include "core/log.h"
#include "gfx/gl_state.h"
#include "game/intro_layout.h"
#include "game/ui_canvas.h"

namespace apricot {
namespace {

constexpr GLuint kTextureUnit = 1;

}  // namespace

LoadingScreen::~LoadingScreen() { destroy(); }

bool LoadingScreen::init(const std::string& texture_rel) {
    if (!shader_.build_from_files("shaders/loading_screen.vert",
                                  "shaders/loading_screen.frag")) {
        AP_ERROR("loading screen: shader failed to build");
        return false;
    }
    if (!texture_.load_file(asset_path(texture_rel))) {
        AP_ERROR("loading screen: texture failed to load '%s'",
                 texture_rel.c_str());
        shader_.destroy();
        return false;
    }
    if (!logo_.load_file(asset_path("textures/ui/probable-cause-intro-logo-v2.png"))) {
        destroy();
        return false;
    }
    started_ = std::chrono::steady_clock::now();
    ready_at_ = -1.0f;
    glGenVertexArrays(1, &vao_);
    if (vao_ == 0) {
        AP_ERROR("loading screen: GL refused to create a VAO");
        texture_.destroy();
        shader_.destroy();
        return false;
    }
    return true;
}

void LoadingScreen::destroy() {
    logo_.destroy();
    texture_.destroy();
    shader_.destroy();
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        gl_state::on_vertex_array_deleted(vao_);
        vao_ = 0;
    }
}

float LoadingScreen::elapsed() const {
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - started_).count();
}

void LoadingScreen::mark_ready() { ready_at_ = elapsed(); }

float LoadingScreen::menu_opacity() const {
    if (ready_at_ < 0.0f) return 0.0f;
    return std::clamp((elapsed() - ready_at_) / 0.65f, 0.0f, 1.0f);
}

void LoadingScreen::render(int width, int height) {
    if (!valid() || width <= 0 || height <= 0) return;

    glViewport(0, 0, width, height);
    shader_.bind();
    texture_.bind(kTextureUnit);
    shader_.set_int("u_screen", static_cast<int>(kTextureUnit));
    logo_.bind(2);
    shader_.set_int("u_logo", 2);
    const glm::vec2 drawable{width, height};
    const glm::vec2 canvas = UiCanvas::from_drawable(drawable).size;
    const IntroLayout layout = IntroLayout::from_canvas(canvas);
    shader_.set_vec2("u_cover", intro_cover_scale(drawable,
        {texture_.width(), texture_.height()}));
    shader_.set_vec2("u_resolution", drawable);
    shader_.set_vec4("u_logo_rect", {layout.logo_position / canvas,
                                   layout.logo_size / canvas});
    shader_.set_float("u_time", elapsed());

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    gl_state::bind_vertex_array(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

}  // namespace apricot
