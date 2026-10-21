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
    // Crash's position (x, y horizontal, z up) when he is loaded
    bool crash_position(std::array<float, 3> &at);
    std::uintptr_t crate_identity_owner(std::uintptr_t owner, const char **name);
}
