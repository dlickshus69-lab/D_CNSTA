#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ingame
{
    // NST's .pak archives (IGA v11, like the Zoinks font in permanent.pak) and the FMOD sound banks inside them
    // (.snd files are FSB4/FSB5 banks). Read-only: the game's own files on this computer, nothing is copied into the
    // mod. Used to find the TNT crate's countdown sound and play it with the Nitro Trap's countdown.
    struct GameSound
    {
        std::string pak, member, name; // archive file, file inside it, sample name inside the bank
        std::string codec;              // "PCM16", "PCM8", "IMA ADPCM", "MPEG", "Vorbis", ... or "unknown (mode)"
        unsigned index = 0, channels = 0, rate = 0, frames = 0;
        std::uint64_t offset = 0, bytes = 0; // the sample's data inside the bank
    };
    // the game folder's archives directory (next to the game's .exe)
    std::filesystem::path game_archives();
    // every archive member whose name contains `text` (any case): "pak: member (size)"
    std::vector<std::string> find_archive_members(const std::string &text, unsigned limit = 200);
    // sounds whose sample name contains `text` (any case), from the sound banks of every archive (`everywhere`) or
    // only the sound-sample banks; `scanned` counts the banks read
    std::vector<GameSound> find_game_sounds(const std::string &text, bool everywhere, unsigned &scanned, unsigned limit = 200);
    // the sound as a WAV file in memory, when its codec is one the client can play (PCM); empty with a reason otherwise
    std::vector<unsigned char> game_sound_wav(const GameSound &sound, std::string &why);
    // copies the first archive member whose name contains `text` to `folder`; the file's path, or empty
    std::filesystem::path dump_archive_member(const std::string &text, const std::filesystem::path &folder);
    // console lines about the sounds matching `text`. With `choose`, it also picks the Nitro Trap countdown's sound
    // (the sample whose name contains `prefer`, or else the likeliest TNT countdown) and loads it when it can play it.
    std::vector<std::string> sound_search_report(const std::string &text, bool everywhere, bool choose, const std::string &prefer = {});
}
