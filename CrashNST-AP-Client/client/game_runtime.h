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
    std::string level_key(std::string zone);
    unsigned hub_campaign(std::string zone);
    void update_gameplay(Shared &state);
    unsigned take_observed_deaths();
    void restore_gameplay(bool game_thread = false);
    void inherit_license();
    unsigned movement_campaign(std::uintptr_t manager);
    unsigned movement_double_jump(unsigned owned);
    std::uintptr_t crate_identity_owner(std::uintptr_t owner, const char **name);
}
