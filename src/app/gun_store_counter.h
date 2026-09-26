#pragma once
// The App's state for the Brassline Arms counter. The rules — prices, what is
// refused and why, the customer zone, the confirm step — are all headless in
// game/gun_store_shop.h; the App glue that opens, routes, draws and scripts
// the counter is src/app/gun_store_counter.cpp. This struct is here so App
// carries one member for the whole shop rather than a scatter of flags.
#include <string>

#include "game/gun_store_shop.h"
#include "game/weapon_ownership.h"

namespace apricot {

struct GunStoreCounter {
    GunStoreMenu menu;
    // While the menu is open it keeps every event of the frame, like the bank
    // keypad: the key that closed it must not also reach the player.
    bool input_consumed = false;
    bool restore_mouse = false;

    // --gun-store-check. See gun_store_counter.cpp.
    bool check = false, check_done = false, check_failed = false;
    unsigned check_captures = 0;
    int check_stage = 0;
    int check_stage_frame = 0;
    std::string check_capture;  // screenshot suffix owed on the next rendered frame
};

}  // namespace apricot
