#include "game_sound.h"
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

namespace
{
    // FMOD's C++ and C entry points for the calls used here. Windows x64 passes "this" first, so both shapes are plain
    // functions whose first argument is the System or Sound. Whole registers are passed through untouched.
    enum class Api { Ex, Modern }; // FMOD Ex (playSound takes a channel index) or FMOD 1.x+ (a channel group)
    using PlayEx = int (*)(void *system, std::intptr_t channel_index, void *sound, std::uintptr_t paused, void **channel);
    using PlayModern = int (*)(void *system, void *sound, void *group, std::uintptr_t paused, void **channel);
    using GetName = int (*)(void *sound, char *name, std::intptr_t length);
    using GetSubSound = int (*)(void *sound, std::intptr_t index, void **subsound);
    using CreateSound = int (*)(void *system, const char *name, std::uintptr_t mode, void *info, void **sound);
    using Release = int (*)(void *sound);
    using ChannelSet = int (*)(void *channel, std::uintptr_t value);
    using GetLength = int (*)(void *sound, unsigned *length, std::uintptr_t unit);

    std::mutex sound_mutex;
    Api api = Api::Ex;
    void *play_original = nullptr;
    GetName get_name = nullptr;
    GetSubSound subsound_original = nullptr;
    CreateSound create_original = nullptr, stream_original = nullptr;
    Release release_original = nullptr;
    ChannelSet channel_mode = nullptr, channel_paused = nullptr;
    GetLength get_length = nullptr;
    void *fmod_system = nullptr;
    // the sounds the game played in this level, by name. Only these are ever played again: a sound from a level's
    // bank is freed with the bank (its sub-sounds without a release call of their own), so the list is emptied
    // whenever the level changes, and a sound the game just played in this level is surely still loaded.
    std::map<std::string, void *> by_name;
    std::map<void *, std::string> by_sound;
    std::vector<std::string> order;
    std::set<std::string> logged;

    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }
    void note(void *sound, const char *how)
    {
        if (!sound || !get_name)
            return;
        char name[256]{};
        if (get_name(sound, name, sizeof(name) - 1) != 0 || !name[0])
            return;
        std::lock_guard<std::mutex> guard(sound_mutex);
        const std::string text = name;
        if (!std::strcmp(how, "played"))
        {
            if (!by_name.count(text))
                order.push_back(text);
            by_name[text] = sound;
            by_sound[sound] = text;
        }
        // each name once, so the log shows what a TNT (or anything else) plays the first time it does
        if (logged.size() < 600 && logged.insert(text).second)
            std::cout << "Game sound " << how << ": " << text << "\n" << std::flush;
    }
    int play_ex(void *system, std::intptr_t channel_index, void *sound, std::uintptr_t paused, void **channel)
    {
        fmod_system = system;
        note(sound, "played");
        return reinterpret_cast<PlayEx>(play_original)(system, channel_index, sound, paused, channel);
    }
    int play_modern(void *system, void *sound, void *group, std::uintptr_t paused, void **channel)
    {
        fmod_system = system;
        note(sound, "played");
        return reinterpret_cast<PlayModern>(play_original)(system, sound, group, paused, channel);
    }
    int sub_sound(void *sound, std::intptr_t index, void **subsound)
    {
        const auto result = subsound_original(sound, index, subsound);
        if (result == 0 && subsound)
            note(*subsound, "loaded");
        return result;
    }
    int create_sound(void *system, const char *name, std::uintptr_t mode, void *info, void **sound)
    {
        fmod_system = system;
        const auto result = create_original(system, name, mode, info, sound);
        if (result == 0 && sound)
            note(*sound, "loaded");
        return result;
    }
    int create_stream(void *system, const char *name, std::uintptr_t mode, void *info, void **sound)
    {
        fmod_system = system;
        const auto result = stream_original(system, name, mode, info, sound);
        if (result == 0 && sound)
            note(*sound, "loaded");
        return result;
    }
    int release_sound(void *sound)
    {
        {
            std::lock_guard<std::mutex> guard(sound_mutex);
            const auto found = by_sound.find(sound);
            if (found != by_sound.end())
            {
                if (by_name.count(found->second) && by_name[found->second] == sound)
                    by_name.erase(found->second);
                by_sound.erase(found);
            }
        }
        return release_original(sound);
    }
    bool replace_slot(void **slot, void *replacement)
    {
        DWORD old = 0;
        if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old))
            return false;
        *slot = replacement;
        VirtualProtect(slot, sizeof(void *), old, &old);
        return true;
    }
    // a named export of a loaded DLL whose name starts with `prefix`
    void *export_starting(HMODULE module, const char *prefix)
    {
        if (!module)
            return nullptr;
        const auto base = reinterpret_cast<const unsigned char *>(module);
        const auto pe = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
        const auto dir = pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!dir.VirtualAddress || !dir.Size)
            return nullptr;
        const auto exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY *>(base + dir.VirtualAddress);
        const auto names = reinterpret_cast<const DWORD *>(base + exports->AddressOfNames);
        const auto ordinals = reinterpret_cast<const WORD *>(base + exports->AddressOfNameOrdinals);
        const auto functions = reinterpret_cast<const DWORD *>(base + exports->AddressOfFunctions);
        const auto length = std::strlen(prefix);
        for (DWORD i = 0; i < exports->NumberOfNames && i < 20000; ++i)
        {
            const auto name = reinterpret_cast<const char *>(base + names[i]);
            if (!std::strncmp(name, prefix, length))
                return const_cast<unsigned char *>(base + functions[ordinals[i]]);
        }
        return nullptr;
    }
} // namespace

namespace ingame
{
    void install_sound_hooks()
    {
        static bool done = false;
        if (done)
            return;
        done = true;
        const auto base = reinterpret_cast<unsigned char *>(GetModuleHandleW(nullptr));
        const auto pe = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + reinterpret_cast<IMAGE_DOS_HEADER *>(base)->e_lfanew);
        const auto directory = pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        std::ostringstream listed;
        unsigned count = 0;
        std::string library_used;
        if (directory.VirtualAddress && directory.VirtualAddress + directory.Size <= pe->OptionalHeader.SizeOfImage)
        {
            const auto imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + directory.VirtualAddress);
            for (unsigned i = 0; i < directory.Size / sizeof(*imports) && imports[i].Name; ++i)
            {
                if (!imports[i].OriginalFirstThunk || !imports[i].FirstThunk)
                    continue;
                const std::string library = lower(reinterpret_cast<const char *>(base + imports[i].Name));
                if (library.find("fmod") == std::string::npos)
                    continue;
                library_used = library;
                const auto module = GetModuleHandleA(library.c_str());
                auto names = reinterpret_cast<IMAGE_THUNK_DATA64 *>(base + imports[i].OriginalFirstThunk);
                auto addresses = reinterpret_cast<IMAGE_THUNK_DATA64 *>(base + imports[i].FirstThunk);
                for (unsigned j = 0; j < 4096 && names[j].u1.AddressOfData; ++j)
                {
                    if (IMAGE_SNAP_BY_ORDINAL64(names[j].u1.Ordinal))
                        continue;
                    const std::string name = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names[j].u1.AddressOfData)->Name;
                    const auto slot = reinterpret_cast<void **>(&addresses[j].u1.Function);
                    ++count;
                    if (count <= 60)
                        listed << " " << name;
                    const bool cpp_play = name.rfind("?playSound@System@FMOD@@", 0) == 0, c_play = name == "FMOD_System_PlaySound";
                    if ((cpp_play || c_play) && !play_original)
                    {
                        // FMOD Ex's playSound takes a channel index; FMOD 1.x and later a channel group
                        api = cpp_play ? (name.find("ChannelGroup") == std::string::npos ? Api::Ex : Api::Modern)
                                       : (library.find("fmodex") != std::string::npos ? Api::Ex : Api::Modern);
                        play_original = *slot;
                        if (!replace_slot(slot, api == Api::Ex ? reinterpret_cast<void *>(play_ex) : reinterpret_cast<void *>(play_modern)))
                            play_original = nullptr;
                    }
                    else if ((name.rfind("?getSubSound@Sound@FMOD@@", 0) == 0 || name == "FMOD_Sound_GetSubSound") && !subsound_original)
                    {
                        subsound_original = reinterpret_cast<GetSubSound>(*slot);
                        if (!replace_slot(slot, reinterpret_cast<void *>(sub_sound)))
                            subsound_original = nullptr;
                    }
                    else if ((name.rfind("?createSound@System@FMOD@@", 0) == 0 || name == "FMOD_System_CreateSound") && !create_original)
                    {
                        create_original = reinterpret_cast<CreateSound>(*slot);
                        if (!replace_slot(slot, reinterpret_cast<void *>(create_sound)))
                            create_original = nullptr;
                    }
                    else if ((name.rfind("?createStream@System@FMOD@@", 0) == 0 || name == "FMOD_System_CreateStream") && !stream_original)
                    {
                        stream_original = reinterpret_cast<CreateSound>(*slot);
                        if (!replace_slot(slot, reinterpret_cast<void *>(create_stream)))
                            stream_original = nullptr;
                    }
                    else if ((name.rfind("?release@Sound@FMOD@@", 0) == 0 || name == "FMOD_Sound_Release") && !release_original)
                    {
                        release_original = reinterpret_cast<Release>(*slot);
                        if (!replace_slot(slot, reinterpret_cast<void *>(release_sound)))
                            release_original = nullptr;
                    }
                    else if (name.rfind("?getName@Sound@FMOD@@", 0) == 0 || name == "FMOD_Sound_GetName")
                        get_name = reinterpret_cast<GetName>(*slot);
                }
                if (!get_name && module)
                {
                    get_name = reinterpret_cast<GetName>(export_starting(module, "?getName@Sound@FMOD@@"));
                    if (!get_name)
                        get_name = reinterpret_cast<GetName>(export_starting(module, "FMOD_Sound_GetName"));
                }
                for (const char *name : {"?getLength@Sound@FMOD@@", "FMOD_Sound_GetLength"})
                    if (!get_length)
                        get_length = reinterpret_cast<GetLength>(export_starting(module, name));
                // to play a 3D sound (a TNT ticks where it stands) flat, at full volume wherever Crash is
                for (const char *name : {"?setMode@Channel@FMOD@@", "?setMode@ChannelControl@FMOD@@", "FMOD_Channel_SetMode"})
                    if (!channel_mode)
                        channel_mode = reinterpret_cast<ChannelSet>(export_starting(module, name));
                for (const char *name : {"?setPaused@Channel@FMOD@@", "?setPaused@ChannelControl@FMOD@@", "FMOD_Channel_SetPaused"})
                    if (!channel_paused)
                        channel_paused = reinterpret_cast<ChannelSet>(export_starting(module, name));
            }
        }
        if (!count)
        {
            // no imports: FMOD may be built into NST or loaded later; list any FMOD modules for the next step
            std::string modules;
            const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
            if (snapshot != INVALID_HANDLE_VALUE)
            {
                MODULEENTRY32 entry{};
                entry.dwSize = sizeof(entry);
                for (auto more = Module32First(snapshot, &entry); more; more = Module32Next(snapshot, &entry))
                {
                    const auto name = lower(entry.szModule);
                    if (name.find("fmod") != std::string::npos || name.find("audio") != std::string::npos || name.find("wwise") != std::string::npos)
                        modules += " " + name;
                }
                CloseHandle(snapshot);
            }
            std::cout << "Game sound: NST imports nothing from FMOD (FMOD modules loaded:" << (modules.empty() ? " none" : modules)
                      << "); the Nitro Trap keeps its own tick\n" << std::flush;
            return;
        }
        std::cout << "Game sound: NST imports " << count << " FMOD functions from " << library_used << ":" << listed.str() << "\n"
                  << "Game sound: watching " << (play_original ? "playSound " : "") << (subsound_original ? "getSubSound " : "")
                  << (create_original ? "createSound " : "") << (stream_original ? "createStream " : "") << (release_original ? "release " : "")
                  << (get_name ? "(names readable)" : "(names NOT readable)") << (api == Api::Ex ? " [FMOD Ex]" : " [FMOD 1.x+]") << "\n" << std::flush;
    }
    bool play_game_sound(const std::string &part)
    {
        void *sound = nullptr, *system = nullptr;
        {
            std::lock_guard<std::mutex> guard(sound_mutex);
            const auto wanted = lower(part);
            for (const auto &name : order)
                if (lower(name).find(wanted) != std::string::npos && by_name.count(name))
                {
                    sound = by_name[name];
                    break;
                }
            system = fmod_system;
        }
        if (!sound || !system || !play_original)
            return false;
        void *channel = nullptr;
        // a free channel (FMOD Ex: FMOD_CHANNEL_FREE, -1) / the master channel group. Started paused when it can be
        // made 2D first (FMOD_2D = 8), so a sound placed in the world is heard where Crash is
        const bool flat = channel_mode && channel_paused;
        const auto result = api == Api::Ex ? reinterpret_cast<PlayEx>(play_original)(system, -1, sound, flat, &channel)
                                           : reinterpret_cast<PlayModern>(play_original)(system, sound, nullptr, flat, &channel);
        if (result != 0)
            return false;
        if (flat && channel)
        {
            channel_mode(channel, 8);
            channel_paused(channel, 0);
        }
        return true;
    }
    unsigned game_sound_length_ms(const std::string &name)
    {
        void *sound = nullptr;
        {
            std::lock_guard<std::mutex> guard(sound_mutex);
            const auto found = by_name.find(name);
            if (found == by_name.end())
                return 0;
            sound = found->second;
        }
        unsigned length = 0;
        return get_length && get_length(sound, &length, 1 /* FMOD_TIMEUNIT_MS */) == 0 ? length : 0;
    }
    std::string tnt_tick_sound()
    {
        std::lock_guard<std::mutex> guard(sound_mutex);
        std::string fallback;
        for (const auto &name : order)
        {
            const auto l = lower(name);
            if (!by_name.count(name) || l.find("tnt") == std::string::npos || l.find("explo") != std::string::npos ||
                l.find("boom") != std::string::npos || l.find("blast") != std::string::npos || l.find("break") != std::string::npos)
                continue;
            for (const char *tick : {"count", "tick", "beep", "timer", "fuse"})
                if (l.find(tick) != std::string::npos)
                    return name;
            if (fallback.empty())
                fallback = name;
        }
        return fallback;
    }
    void forget_game_sounds()
    {
        std::lock_guard<std::mutex> guard(sound_mutex);
        by_name.clear();
        by_sound.clear();
        order.clear();
    }
    std::vector<std::string> game_sound_names()
    {
        std::lock_guard<std::mutex> guard(sound_mutex);
        return order;
    }
}
