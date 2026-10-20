#pragma once
#include <cstdint>
#include <string_view>
namespace ingame
{
    // Ability Shuffle: one AP item per basic move, item ID 0xCB0000 + 2400 + index of the bit below
    enum : unsigned
    {
        MoveSpin = 1,
        MoveBodySlam = 2,
        MoveCrouch = 4,
        MoveSlide = 8,
        MoveAll = 15
    };
    inline constexpr std::int64_t move_item(unsigned index) { return 0xCB0000 + 2400 + index; }
    // the attack event a selected attack fades into. slide and belly flop names are verified (Crashsanity
    // already filters them); the spin name is a best guess and unknown names are logged for testing.
    inline unsigned attack_move(std::string_view action)
    {
        if (action == "TertiaryFadeIn")
            return MoveSlide;
        if (action == "BellyFlopFadeIn")
            return MoveBodySlam;
        if (action.find("Spin") != std::string_view::npos || action.rfind("Primary", 0) == 0)
            return MoveSpin;
        return 0;
    }
    inline bool move_blocked(unsigned move, bool shuffle, bool vanilla, unsigned owned)
    {
        // F10 vanilla controls lift every Ability Shuffle block, like Crashsanity styles
        return move && shuffle && !vanilla && !(owned & move);
    }
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
