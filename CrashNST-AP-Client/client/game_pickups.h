#pragma once
#include "ingame_protocol.h"
namespace ingame
{
    void update_collectibles(Shared &state, const std::string &zone);
    void restore_collectible_hooks();
} // namespace ingame
