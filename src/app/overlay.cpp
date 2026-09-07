#include "app/overlay.h"
#include "app/bug_report.h"

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>

#include <SDL.h>

#include <algorithm>

#include "core/fixed_step.h"
#include "core/log.h"
#include "gfx/gl_state.h"
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

void draw(Window& window, const Stats& stats, Controls& controls) {
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
        ImGui::Text("nodes  %d visible / %d total", stats.visible_nodes,
                    stats.scene_nodes);
        ImGui::Text("draws  %d      instances %d", stats.draw_calls,
                    stats.instances);
        ImGui::Text("batch  %d (%d instanced, longest run %d)", stats.batches,
                    stats.instanced_batches, stats.largest_run);
        ImGui::Text("binds  %u skipped by the cache", stats.skipped_binds);

        // The number the toggle exists to make visible. One draw carrying many
        // instances is the win; one instance per draw is what it replaced.
        const float per_draw =
            stats.draw_calls > 0
                ? static_cast<float>(stats.instances) / static_cast<float>(stats.draw_calls)
                : 0.0f;
        ImGui::Text("       %.1f instances per draw", static_cast<double>(per_draw));

        if (ImGui::Checkbox("instancing (F7)", &controls.instancing)) {
            // Nothing to do — the app reads this back the same frame.
        }
        if (!controls.instancing) {
            ImGui::TextColored(ImVec4{1.0f, 0.75f, 0.3f, 1.0f},
                               "NAIVE PATH: one draw per node");
        }

        ImGui::Separator();
        ImGui::Text("terrain %zu chunks  %.1f MB  %zu meshes",
                    stats.resident_chunks, stats.terrain_mb, stats.live_meshes);
        // The line that says whether LOD is on. All of it at level 0 means the
        // rings are not being applied, and the MB figure above is about to say
        // so much more loudly.
        ImGui::Text("   lod  %zu / %zu / %zu / %zu", stats.resident_by_lod[0],
                    stats.resident_by_lod[1], stats.resident_by_lod[2],
                    stats.resident_by_lod[3]);
        ImGui::Text("   this frame  +%d built  ~%d refit  -%d evict  "
                    "%d freed",
                    stats.chunks_built, stats.chunks_refitted,
                    stats.chunks_evicted, stats.meshes_freed);
        ImGui::Text("   mesh %.2f ms   cull %.3f ms", stats.mesh_ms,
                    stats.cull_ms);
        if (stats.stream_budget_hit) {
            ImGui::TextColored(ImVec4{1.0f, 0.82f, 0.35f, 1.0f},
                               "   instance budget carried a chunk over");
        }
        if (stats.fill_steps > 0) {
            ImGui::TextDisabled("   last fill %.1f ms in %d steps (F8 warps)",
                                stats.fill_ms, stats.fill_steps);
        }

        ImGui::Separator();
        ImGui::Text("hud    %d quads in %d draw%s", stats.hud_quads,
                    stats.hud_draw_calls, stats.hud_draw_calls == 1 ? "" : "s");
        ImGui::Text("precip %d particles -> %d quads", stats.rain_drops,
                    stats.rain_quads);

        ImGui::Separator();
        ImGui::Text("sky    t=%.3f", static_cast<double>(stats.time_of_day));
        ImGui::Text("snowpack %.2f m", static_cast<double>(stats.snow_depth_m));
        ImGui::Text("haze   %.0f -> %.0f m",
                    static_cast<double>(stats.fog_start_m),
                    static_cast<double>(stats.fog_end_m));
        ImGui::SliderFloat("sky speed", &controls.sky_speed, 0.0f, 20.0f, "%.1f");
        ImGui::SliderFloat("rain", &controls.rain, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("overcast", &controls.overcast, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("weather fog", &controls.fog, 0.0f, 1.0f, "%.2f");
        bool automatic_snow = controls.snow_depth_override_m < 0.0f;
        if (ImGui::Checkbox("automatic snowpack", &automatic_snow)) {
            controls.snow_depth_override_m =
                automatic_snow ? -1.0f : stats.snow_depth_m;
        }
        if (!automatic_snow) {
            ImGui::SliderFloat("snow depth (m)",
                               &controls.snow_depth_override_m,
                               0.0f, 1.5f, "%.2f m");
        }

        ImGui::Separator();
        if (stats.gl_errors > 0) {
            // A GL error means a call was rejected and the frame on screen is
            // not the frame that was asked for. Never let that be quiet.
            ImGui::TextColored(ImVec4{1.0f, 0.3f, 0.3f, 1.0f},
                               "GL ERRORS: %d (see the log)", stats.gl_errors);
        } else {
            ImGui::TextDisabled("GL clean");
        }
        ImGui::Text("%dx%d", window.width(), window.height());
        ImGui::TextDisabled(
            "F1 dev  F2 report bug  F3 hide stats  P pause  M map");
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // The UI backend binds its own program, VAO and texture behind the bind
    // cache's back, so everything the cache believes about GL state is now a
    // lie. Forget all of it — see gl_state.h.
    gl_state::invalidate_all();
}

void draw_bug_report(Window& window, BugReportUi& report) {
    if (!g_initialized || !report.open) return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    const float width = std::min(660.0f, static_cast<float>(window.width()) - 48.0f);
    ImGui::SetNextWindowSize(ImVec2{width, 0.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowPos(
        ImVec2{static_cast<float>(window.width()) * 0.5f,
               static_cast<float>(window.height()) * 0.38f},
        ImGuiCond_Always, ImVec2{0.5f, 0.5f});
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("REPORT BUG", nullptr, flags)) {
        ImGui::TextWrapped(
            "Tell Codex what is wrong. Apricot will attach a clean screenshot "
            "of this game window and the exact location below.");
        ImGui::Spacing();
        ImGui::Text("Position  %.3f, %.3f, %.3f",
                    static_cast<double>(report.position.x),
                    static_cast<double>(report.position.y),
                    static_cast<double>(report.position.z));
        ImGui::Text("Forward   %.4f, %.4f, %.4f",
                    static_cast<double>(report.forward.x),
                    static_cast<double>(report.forward.y),
                    static_cast<double>(report.forward.z));
        ImGui::Spacing();
        ImGui::TextUnformatted("What happened?");
        if (report.focus_input) {
            ImGui::SetKeyboardFocusHere();
            report.focus_input = false;
        }
        const bool enter = ImGui::InputText(
            "##bug", report.message.data(), report.message.size(),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        const bool has_message = bug_report_has_message(report);
        if (!has_message) ImGui::BeginDisabled();
        const bool send_clicked =
            ImGui::Button("SEND TO CODEX", ImVec2{170.0f, 34.0f});
        if ((send_clicked || enter) && has_message) {
            report.submit_requested = true;
        }
        if (!has_message) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("CANCEL", ImVec2{110.0f, 34.0f}) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            report.cancel_requested = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Enter sends   Esc cancels");
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    gl_state::invalidate_all();
}

}  // namespace apricot::overlay
