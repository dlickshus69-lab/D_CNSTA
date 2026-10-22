#pragma once
#include <string>
#include <vector>

namespace ingame
{
    // The game's own sounds through FMOD (NST plays its sounds with FMOD; its sound files are FMOD sound banks).
    // If NST imports FMOD's functions, its imports of playSound, getSubSound and createSound are routed through the
    // client so it learns the sounds the game plays (by name), and it can play one of them again, for example a TNT
    // crate's countdown tick for the Nitro Trap. Every sound the game makes still plays as before.
    // Installs once and logs which FMOD functions NST imports ("Game sound: ...").
    void install_sound_hooks();
    // plays the first sound the game has used in this level whose name contains `part` (any case); false if none
    bool play_game_sound(const std::string &part);
    // the best guess at a TNT crate's countdown tick among the sounds used in this level, or empty
    std::string tnt_tick_sound();
    // how long a sound the game played in this level lasts, in milliseconds (0 if unknown)
    unsigned game_sound_length_ms(const std::string &name);
    // sounds may be freed with the level: forget them when it changes
    void forget_game_sounds();
    // the names of the sounds used in this level, in the order first used
    std::vector<std::string> game_sound_names();
}
