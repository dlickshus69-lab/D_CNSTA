#pragma once
#include "crash_colors_core.h"
#include <array>
#include <string>
#include <vector>

// Crash colors in the client: the chosen colors (from the YAML's crash_*_color options, once per seed, or /colors) are
// kept in state\colors.txt. The client makes a copy of the game's own Crash.pak with those colors, as a skin named
// colors_<code> in CrashNSTAP\skins (one per combination), and chooses it; like any skin, it shows from the next game
// start. All colors "default" goes back to the normal Crash.
namespace crash_colors
{
    using Choice = std::array<std::string, PartCount>; // "default", a color name or "#rrggbb", per part
    Choice chosen();
    // "fur black, chest white, ..." (only the parts that aren't default), or "the normal colors"
    std::string describe(const Choice &choice);
    // /colors: shows the colors; /colors <part> <color> changes one; /colors reset goes back to normal. Console lines
    std::vector<std::string> command(const std::string &argument);
    // the YAML's colors, once per seed (a later /colors stays until another seed). When the YAML also picks a skin mod,
    // the skin stays and the colors wait. Console lines
    std::vector<std::string> apply_seed_choice(const std::string &seed, const Choice &yaml, const std::string &yaml_skin);
}
