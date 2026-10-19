#pragma once
#include <string_view>
namespace ingame
{
    inline unsigned movement_style(unsigned assigned, unsigned campaign, bool vanilla)
    {
        return !vanilla && assigned >= 1 && assigned <= 3 ? assigned : campaign;
    }
    inline bool movement_needs_crouch(unsigned assigned, unsigned campaign)
    {
        return assigned == 2 || assigned == 3 || campaign == 2 || campaign == 3;
    }
    inline bool movement_double_jump_allowed(unsigned owned, unsigned style, unsigned campaign)
    {
        return owned && (style ? style : campaign) == 3;
    }
    inline unsigned movement_attack_selection(unsigned selected, unsigned style, std::string_view action)
    {
        return style == 1 && (action == "TertiaryFadeIn" || action == "BellyFlopFadeIn") ? 4 : selected;
    }
    inline bool movement_skill_filter(bool original, unsigned style, unsigned mode, std::string_view skill)
    {
        if (skill != "OButton" || style < 1 || style > 3 || mode > 1)
            return original;
        const bool enabled = style == 2 || style == 3;
        return mode == 1 ? !enabled : enabled;
    }
}
