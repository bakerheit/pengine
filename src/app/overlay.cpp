#include "app/overlay.h"

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>

#include <SDL.h>

#include "core/fixed_step.h"
#include "core/log.h"
#include "platform/window.h"

namespace apricot::overlay {
namespace {
bool g_initialized = false;
}

bool init(Window& window) {
    if (g_initialized) return true;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // don't drop an ini file next to the binary
    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL2_InitForOpenGL(window.sdl(), window.gl_context())) {
        AP_ERROR("overlay: SDL2 backend init failed");
        ImGui::DestroyContext();
        return false;
    }
    // Must match the context created in platform/window.cpp.
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        AP_ERROR("overlay: GL3 backend init failed");
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    g_initialized = true;
    AP_INFO("overlay ready (Dear ImGui %s)", IMGUI_VERSION);
    return true;
}

void shutdown() {
    if (!g_initialized) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    g_initialized = false;
}

bool process_event(const void* sdl_event) {
    if (!g_initialized || !sdl_event) return false;
    return ImGui_ImplSDL2_ProcessEvent(static_cast<const SDL_Event*>(sdl_event));
}

void draw(Window& window, const Stats& stats) {
    if (!g_initialized) return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2{12.0f, 12.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.65f);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("apricot", nullptr, flags)) {
        ImGui::Text("apricot %s", APRICOT_VERSION);
        ImGui::Separator();
        ImGui::Text("%6.1f fps   %5.2f ms", stats.fps, stats.frame_ms);
        ImGui::Text("sim %d step%s @ %.0f Hz", stats.sim_steps,
                    stats.sim_steps == 1 ? "" : "s", kSimHz);
        ImGui::Text("alpha %.3f   step #%llu", stats.alpha,
                    stats.sim_step_index);
        if (stats.step_clamped) {
            // Loud on purpose. A clamped frame means sim time was thrown away.
            ImGui::TextColored(ImVec4{1.0f, 0.4f, 0.3f, 1.0f},
                               "STEP CLAMP: sim time dropped");
        }
        ImGui::Separator();
        ImGui::Text("%dx%d", window.width(), window.height());
        ImGui::TextDisabled("Esc or Ctrl+Q to quit");
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

}  // namespace apricot::overlay
