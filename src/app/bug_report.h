#pragma once

#include <array>
#include <iomanip>
#include <sstream>
#include <string>

#include <glm/glm.hpp>

namespace apricot {

// Host-only state for the F2 reporter. It never enters InputFrame or a save:
// opening a developer tool must not change a replay or the simulated world.
struct BugReportUi {
    bool open = false;
    bool focus_input = false;
    bool submit_requested = false;
    bool cancel_requested = false;
    std::array<char, 1024> message{};
    glm::vec3 position{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};

    void begin(glm::vec3 at, glm::vec3 facing) {
        open = true;
        focus_input = true;
        submit_requested = false;
        cancel_requested = false;
        message.fill('\0');
        position = at;
        forward = facing;
    }

    void close() {
        open = false;
        focus_input = false;
    }
};

inline bool bug_report_has_message(const BugReportUi& report) {
    for (char c : report.message) {
        if (c == '\0') break;
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return true;
    }
    return false;
}

inline std::string bug_report_markdown(const BugReportUi& report,
                                       unsigned long long frame,
                                       unsigned long long sim_step,
                                       const char* player_mode,
                                       const char* version) {
    std::ostringstream out;
    out << "# Apricot bug report\n\n"
        << "## Player report\n\n" << report.message.data() << "\n\n"
        << "## Captured game state\n\n"
        << std::fixed << std::setprecision(3)
        << "- Position: `{" << report.position.x << "f, " << report.position.y
        << "f, " << report.position.z << "f}`\n"
        << std::setprecision(6)
        << "- Forward: `{" << report.forward.x << "f, " << report.forward.y
        << "f, " << report.forward.z << "f}`\n"
        << "- Mode: `" << (player_mode ? player_mode : "unknown") << "`\n"
        << "- Rendered frame: `" << frame << "`\n"
        << "- Simulation step: `" << sim_step << "`\n"
        << "- Apricot version: `" << (version ? version : "unknown") << "`\n"
        << "- Screenshot: `screenshot.png`\n";
    return out.str();
}

}  // namespace apricot
