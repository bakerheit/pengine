#pragma once

#include <SDL.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace apricot {

// Opt-in, local-only QA transport. The launcher owns a private directory.
// Commands use ordinary SDL events, never write player/vehicle state.
class PlaytestBridge {
public:
    using Clock = std::chrono::steady_clock;
    PlaytestBridge() {
        const char* path = std::getenv("APRICOT_PLAYTEST_DIR");
        if (path && *path && std::filesystem::is_directory(path)) directory = path;
    }
    bool enabled() const { return !directory.empty(); }
    void pump() {
        if (!enabled()) return;
        const auto now = Clock::now();
        if (now - started_ > std::chrono::minutes(3)) {
            release();
            SDL_Event event{}; event.type = SDL_QUIT; SDL_PushEvent(&event);
            return;
        }
        if (active_ && now >= expires_) {
            release(); completed = accepted; capture = true;
        }
        const auto command = directory / "command.txt";
        std::ifstream stream(command);
        if (!stream) return;
        unsigned long long id = 0;
        int milliseconds = 0;
        std::string action, extra;
        const bool parsed = static_cast<bool>(stream >> id >> action >> milliseconds);
        const bool trailing = static_cast<bool>(stream >> extra);
        stream.close();
        std::error_code ec;
        std::filesystem::remove(command, ec);
        if (!parsed || trailing || id <= accepted || milliseconds < 30 || milliseconds > 2000) return;
        std::vector<SDL_Keycode> keys;
        if (action == "forward") keys = {SDLK_w};
        else if (action == "backward") keys = {SDLK_s};
        else if (action == "left") keys = {SDLK_a};
        else if (action == "right") keys = {SDLK_d};
        else if (action == "forward_left") keys = {SDLK_w, SDLK_a};
        else if (action == "forward_right") keys = {SDLK_w, SDLK_d};
        else if (action == "sprint") keys = {SDLK_w, SDLK_LSHIFT};
        else if (action == "jump") keys = {SDLK_SPACE};
        else if (action == "interact") keys = {SDLK_e};
        // S becomes reverse once stopped. Space alone cannot accelerate away.
        else if (action == "brake") keys = {SDLK_SPACE};
        else if (action == "camera") keys = {SDLK_c};
        else if (action == "map") keys = {SDLK_m};
        else if (action == "pause") keys = {SDLK_p};
        else if (action != "wait" && action != "stop" && action != "quit") return;
        release();
        accepted = id; current_action = action; held_ = keys;
        for (const auto key : held_) send_key(key, true);
        active_ = true;
        expires_ = now + std::chrono::milliseconds(milliseconds);
        if (action == "quit") {
            SDL_Event event{}; event.type = SDL_QUIT; SDL_PushEvent(&event);
        }
    }
    bool due() const { return enabled() && (capture || Clock::now() >= next_state_); }
    void publish(const std::string& json) {
        const auto temporary = directory / "state.tmp";
        { std::ofstream out(temporary); out << json; if (!out) return; }
        std::error_code ec;
        std::filesystem::rename(temporary, directory / "state.json", ec);
        next_state_ = Clock::now() + std::chrono::milliseconds(100);
    }
    std::filesystem::path directory;
    unsigned long long accepted = 0, completed = 0;
    std::string current_action = "wait";
    bool capture = true;
private:
    static void send_key(SDL_Keycode code, bool down) {
        SDL_Event event{};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
        event.key.keysym.sym = code;
        event.key.keysym.scancode = SDL_GetScancodeFromKey(code);
        SDL_PushEvent(&event);
    }
    void release() {
        for (const auto key : held_) send_key(key, false);
        held_.clear(); active_ = false;
    }
    std::vector<SDL_Keycode> held_;
    bool active_ = false;
    Clock::time_point started_ = Clock::now(), expires_{}, next_state_{};
};
} // namespace apricot
