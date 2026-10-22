#pragma once
#include <filesystem>
#include <string>
#include <vector>

// Crash skins: skin mods' Crash.pak files kept in CrashNSTAP\skins. The client only records which one to wear
// (state\skin.txt); the dinput8.dll proxy puts it in the game's archives folder when the game starts, before the game
// opens it, and leaves what happened in CRASHNSTAP_SKIN. So a new choice shows after restarting the game.
namespace skins
{
    // CrashNSTAP\skins next to the client's DLL
    std::filesystem::path folder();
    // the skins in that folder: folder names and .pak file names (without "default", the game's own copy)
    std::vector<std::string> available();
    // "default", "playstation_1", "biker" or another skin's name, from the YAML's spelling ("default_crash", ...)
    std::string normalize(const std::string &name);
    // "Default Crash", "PlayStation 1 Crash", "Biker Crash", or the name itself
    std::string display(const std::string &name);
    // the skin to wear from the next game start (state\skin.txt), "default" when none was chosen
    std::string chosen();
    void choose(const std::string &name);
    // the skin this game start put on; empty when the proxy didn't report one (an older dinput8.dll)
    std::string worn();
    // a console line about the skin this game is wearing and the one chosen for next time
    std::string status();
    // the YAML's crash_skin, chosen once per seed (a later /skin stays until another seed); a console line, or empty
    // when nothing changed
    std::string apply_seed_choice(const std::string &seed, const std::string &skin);
}
