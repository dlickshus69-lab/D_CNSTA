#pragma once
#include "crash_colors_core.h"
#include <array>
#include <string>
#include <vector>

// Crash colors in the client: the chosen colors (from the YAML's crash_*_color options, once per seed; V1.3.3: only from
// the YAML, they stay for the whole seed) are kept in state\colors.txt. The client makes a copy of the game's own Crash.pak with those colors, as a skin named
// colors_<code> in CrashNSTAP\skins (one per combination), and chooses it; like any skin, it shows from the next game
// start. All colors "default" goes back to the normal Crash.
namespace crash_colors
{
    using Choice = std::array<std::string, PartCount>; // "default", a color name or "#rrggbb", per part
    Choice chosen();
    // "fur black, chest white, ..." (only the parts that aren't default), or "the normal colors"
    std::string describe(const Choice &choice);
    // /colors: shows the colors (they can't be changed during a seed). Console lines
    std::vector<std::string> command(const std::string &argument);
    // whether this seed gave Crash any colors
    bool has_colors();
    // makes (when needed) and chooses the colored Crash for this seed's colors; /skin default goes back to it. Console lines
    std::vector<std::string> wear();
    // after an update that colors differently: the chosen colors are made again (at each connection). Console lines
    std::vector<std::string> keep_current();
    // the YAML's colors, once per seed. When the YAML also picks a skin mod, the skin stays and the colors wait. Console lines
    std::vector<std::string> apply_seed_choice(const std::string &seed, const Choice &yaml, const std::string &yaml_skin);
    // what dinput8.dll did this game start with the levels holding their own copy of Crash's textures; a console
    // line, or empty when there's nothing to say
    std::string patch_status();
}
