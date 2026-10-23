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
    // the Nitro Trap countdown's sound read from the game's own sound banks (a WAV in memory; empty clears it)
    void set_countdown_sound(std::vector<unsigned char> wav, const std::string &name);
    // plays it for countdown number `index` (0 for 3, 1 for 2, 2 for 1), every `step_ms`: a sound as long as two
    // numbers or more plays once, at 3; a short one at every number. False when there is none.
    bool play_countdown_sound(int index, unsigned step_ms);
    // the countdown stood still (a death, the pause menu): its sound stops where it is (V1.3.2; it used to play on)
    void stop_countdown_sound();
    // the countdown goes on `offset_ms` after its start: a sound as long as two numbers or more plays on from there, so
    // it stays in step with the numbers (a short one simply plays at the next number)
    void resume_countdown_sound(unsigned offset_ms, unsigned step_ms);
    std::string countdown_sound_name();
    // its volume, 0 (silent) to 100 (as loud as the sound file); 50 unless /nitro_volume changed it
    void set_countdown_volume(unsigned percent);
    unsigned countdown_volume();
}
