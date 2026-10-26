#pragma once
#include "ingame_protocol.h"
#include "game_memory.h"
#include <array>
#include <map>
#include <vector>

namespace ingame
{
    struct LiveBox
    {
        std::array<std::array<float, 3>, 8> corners;
        unsigned color = 0;
    };
    std::vector<LiveBox> live_boxes(const Shared &state, const std::string &zone);
    SceneView scene();
    bool campaign_menu_visible();
    // the pause menu is on screen (its Left_Side and Right_Side panels); visible panel sets are logged when they
    // change so other menus' names show up in the log
    bool pause_menu_visible();
    std::string level_key(std::string zone);
    unsigned hub_campaign(std::string zone);
    void preserve_shared_ui_audio();
    void configure_hub_access(const std::array<unsigned, 3> &areas);
    void configure_movement(const std::map<std::string, unsigned> &styles, bool move_shuffle = false);
    void update_gameplay(Shared &state);
    unsigned take_observed_deaths();
    // the Slippery Trap is using the game's own ice (the floor under Crash is ice); otherwise it moves the stick
    bool real_ice_active();
    void restore_gameplay(bool game_thread = false);
    void inherit_license();
    unsigned movement_campaign(std::uintptr_t manager);
    unsigned movement_double_jump(unsigned owned);
    // gimmick stages (vanilla controls) and chase stages, where walking would be certain death
    bool slow_trap_exempt();
    // gimmick levels except the scuba levels Under Pressure and Deep Trouble
    bool darkness_trap_exempt();
    // gimmick stages (hog, polar bear, jetpack, Crash 3 vehicles): no Darkness there
    // gimmick levels for traps: hog, polar bear, jetpack, Crash 3 vehicle/scuba levels and the Crash 2 jet-board levels
    bool gimmick_stage();
    // levels the Nitro Trap doesn't strike in: the gimmick levels except the hog, Crash 2 jet-board and polar bear
    // levels and the scuba levels (where it only appears ahead of Crash)
    bool nitro_trap_exempt();
    // Crash's position (x, y horizontal, z up) when he is loaded
    bool crash_position(std::array<float, 3> &at);
    // a pad's left stick (x right, y up, -32767 to 32767) as the game is about to get it: the push into Papu Papu's wall in
    // front of his throne taken out as Crash comes up to it (V1.3.74; nothing changes elsewhere); whether it changed
    bool wall_stick(unsigned pad, short &x, short &y);
    // V1.3.79: Papu Papu's wall as it's drawn in the game, while it's up: lines along the arena's floor in the game's
    // units, each either where his physics is set back past it (hard: the straight wall) or where only the stick is
    // steered (its rounded ends), and how high it's drawn
    struct WallLine
    {
        std::vector<std::array<float, 3>> points;
        bool hard = false;
        float height = 0;
    };
    std::vector<WallLine> wall_outline();
    std::uintptr_t crate_identity_owner(std::uintptr_t owner, const char **name);
}
