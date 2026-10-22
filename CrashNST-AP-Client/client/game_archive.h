#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ingame
{
    // NST's .pak archives (IGA v11, like the Zoinks font in permanent.pak) and the sound samples inside them
    // (FMOD sound banks: FSB4/FSB5, alone or inside FMOD Studio banks, or WAV files). Read-only: the game's own files
    // on this computer, nothing is copied into the mod. Used to find the TNT crate's countdown sound and play it with
    // the Nitro Trap's countdown.
    struct GameSound
    {
        std::string pak, member, name; // archive file, file inside it, sample name inside the bank ("" when unnamed)
        std::string codec;              // "PCM16", "PCM8", "IMA ADPCM", "MPEG", "Vorbis", ... or "unknown (mode)"
        unsigned index = 0, channels = 0, rate = 0, frames = 0;
        std::uint64_t offset = 0, bytes = 0; // the sample's data inside the archive file
        bool signed8 = true;                 // 8-bit PCM is signed in FMOD banks, unsigned in WAV files
        std::uint32_t vorbis_setup = 0;      // FSB5 Vorbis: the CRC32 naming FMOD's setup header
    };
    // what a sound search read: sound files, by format ("FSB5", "WAV", "other ..."), and samples, by codec
    struct SoundScan
    {
        unsigned files = 0, named = 0, unnamed = 0;
        std::map<std::string, unsigned> formats, codecs;
        std::vector<std::string> notes; // what the first few files that held no sound bank look like
    };
    // the game folder's archives directory (next to the game's .exe)
    std::filesystem::path game_archives();
    // every archive member whose name contains `text` (any case): "pak: member (size)"
    std::vector<std::string> find_archive_members(const std::string &text, unsigned limit = 200);
    // sounds whose sample or file name contains `text` (any case). NST keeps each sound in its own file
    // (sound_samples/wavs/.../name,bank,NoLoc,48000,40,0,0.igz), so only files named for `text` are read, unless
    // `everywhere` reads every sound file. A file that is in several archives is read once.
    std::vector<GameSound> find_game_sounds(const std::string &text, bool everywhere, SoundScan &scan, unsigned limit = 200);
    // the sound as a WAV file in memory, when its codec is one the client can play (PCM); empty with a reason otherwise
    std::vector<unsigned char> game_sound_wav(const GameSound &sound, std::string &why);
    // copies the first archive member whose name contains `text` to `folder`; the file's path, or empty
    std::filesystem::path dump_archive_member(const std::string &text, const std::filesystem::path &folder);
    // console lines describing the archives: how many, and where their sound files are and what they're called
    std::vector<std::string> archive_census();
    // console lines about the sounds matching `text`. With `choose`, it also picks the Nitro Trap countdown's sound
    // (the sample whose name contains `prefer`, or else the likeliest TNT countdown) and loads it when it can play it;
    // a loaded choice is written to `remember`.
    std::vector<std::string> sound_search_report(const std::string &text, bool everywhere, bool choose, const std::string &prefer = {},
                                                 const std::filesystem::path &remember = {});
    // the session's first search (in the background): the remembered sound, or else the archives' census and the
    // TNT sound files
    std::vector<std::string> startup_sound_search(const std::filesystem::path &remember);
}
