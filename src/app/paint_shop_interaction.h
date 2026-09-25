#pragma once
// The respray booth at Rook's: the host half of game/paint_picker.h. Raw
// events in, the picker panel drawn out. Every decision — focus, drag, what
// Accept does, whether a confirm is refused — is the headless picker's; this
// class only maps keys, buttons and the pointer onto its entry points and
// draws the layout it shares with hit-testing.
//
// It never touches paint. The App reads preview() once per rendered frame
// (preview_changed()) and composites then, because a fast mouse sends hundreds
// of motion events a second and a recolour per event would stall the frame.
//
// Built on the bank modal's shape (app/bank_interaction.h): while modal() the
// App routes raw events here and pauses the sim.
#include <cstdint>
#include <optional>

#include <SDL.h>
#include <glm/glm.hpp>

#include "game/paint_picker.h"

namespace apricot {

class GameUi;
class Hud;

struct PaintShopView {
    int wanted_level = 0;
    bool flash = false;
    int64_t step = 0;
};

class PaintShopInteraction {
public:
    bool modal() const { return picker_.is_open(); }
    void open(const PaintPickerOpen& init);

    // `window_size` is the logical window, `canvas_size` the UiCanvas the
    // panel is laid out in. Moves picker state only.
    void event(const SDL_Event& e, glm::vec2 window_size, glm::vec2 canvas_size);
    // Once per rendered frame: host dt, the merged UI axes, and the pad's
    // right-stick look delta (radians) for the camera orbit.
    void update(float dt, float ui_axis_x, float ui_axis_y, float look_dx);
    void draw(Hud& hud, const GameUi& ui, glm::vec2 canvas, const PaintShopView& view) const;

    std::optional<PaintOrder> take_order() { return picker_.take_order(); }
    bool take_cancel() { return picker_.take_cancel(); }
    PaintOrder preview() const { return picker_.preview(); }
    bool preview_changed() { return picker_.preview_changed(); }
    const PaintPicker& picker() const { return picker_; }

    // Radians the bay camera is swung round the car, from right-mouse drags
    // outside the panel or the right stick. Eases home when left alone.
    float orbit_yaw() const { return orbit_yaw_; }

private:
    void handle(const SDL_Event& e, glm::vec2 window_size, glm::vec2 canvas_size);
    PaintPicker picker_;
    float trigger_left_ = 0.0f, trigger_right_ = 0.0f;
    bool orbiting_ = false;
    float orbit_yaw_ = 0.0f;
    float orbit_idle_s_ = 0.0f;
};

}  // namespace apricot
