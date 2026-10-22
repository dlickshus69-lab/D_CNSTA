#include "game_archive.h"
#include "game_sound.h"
#include <windows.h>
#include <lzma.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <initializer_list>
#include <stdexcept>

namespace ingame
{
    namespace
    {
        constexpr std::size_t limit = 64 * 1024 * 1024;
        using Bytes = std::vector<unsigned char>;
        void require(bool ok)
        {
            if (!ok)
                throw std::runtime_error("unreadable archive data");
        }
        template <class T>
        T get(const Bytes &bytes, std::size_t offset)
        {
            require(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset);
            T result;
            std::memcpy(&result, bytes.data() + offset, sizeof(result));
            return result;
        }
        std::uint32_t word(const Bytes &b, std::size_t p) { return get<std::uint32_t>(b, p); }
        std::string string_at(const Bytes &b, std::size_t p)
        {
            require(p < b.size());
            const auto end = std::find(b.begin() + p, b.end(), 0);
            require(end != b.end());
            return std::string(b.begin() + p, end);
        }
        std::string lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }
        // IGA v11 (see game_font.cpp): 56-byte header, per-file hashes and 16-byte records, block index tables,
        // then the name table; members are stored whole or in 32 KiB LZMA blocks on 2 KiB sectors
        struct Archive
        {
            std::ifstream stream;
            std::uint64_t size = 0;
            Bytes header, names;
            unsigned count = 0, toc = 0, large = 0, medium = 0, small = 0;
            explicit Archive(const std::filesystem::path &path) : stream(path, std::ios::binary)
            {
                require(static_cast<bool>(stream));
                stream.seekg(0, std::ios::end);
                size = static_cast<std::uint64_t>(stream.tellg());
                require(size >= 64);
                header = read(0, 56);
                require(word(header, 0) == 0x1a414749 && word(header, 4) == 11 && word(header, 16) == 0x800);
                toc = word(header, 8), count = word(header, 12), large = word(header, 28), medium = word(header, 32), small = word(header, 36);
                require(count > 0 && count <= 200000 && toc >= count * 20 && toc <= limit && 56ull + toc <= size);
                const auto names_start = get<std::uint64_t>(header, 40);
                require(names_start >= 56ull + toc);
                names = read(names_start, word(header, 48));
                require(names.size() >= count * 4ull);
            }
            Bytes read(std::uint64_t p, std::size_t n)
            {
                require(n <= limit && p <= size && n <= size - p);
                Bytes b(n);
                stream.clear();
                stream.seekg(static_cast<std::streamoff>(p));
                require(static_cast<bool>(stream.read(reinterpret_cast<char *>(b.data()), static_cast<std::streamsize>(n))));
                return b;
            }
            std::string name(unsigned i)
            {
                auto p = word(names, i * 4);
                require(p >= count * 4);
                const auto build = string_at(names, p);
                return string_at(names, p + build.size() + 1);
            }
            std::uint32_t length(unsigned i) { return word(read(56ull + count * 4ull + i * 16ull, 16), 8); }
            Bytes member(unsigned i)
            {
                const auto info = read(56ull + count * 4ull + i * 16ull, 16);
                const auto source = word(info, 0) + ((static_cast<std::uint64_t>(word(info, 4)) & 1) << 32);
                const auto length = word(info, 8), command = word(info, 12);
                require(length > 0 && length <= limit);
                if (command == 0xffffffff)
                    return read(source, length);
                require(command >> 28 == 2); // LZMA blocks; other kinds are not used for these files
                std::uint64_t table = 56ull + count * 20ull;
                unsigned elements = large, width = 4, flag = 0x80000000;
                if (length <= 0x7f * 0x800)
                {
                    table += large * 4ull + medium * 2ull;
                    elements = small, width = 1, flag = 0x80;
                }
                else if (length <= 0x7fff * 0x800)
                {
                    table += large * 4ull;
                    elements = medium, width = 2, flag = 0x8000;
                }
                const auto blocks = (length + 0x7fff) / 0x8000, first = command & 0xfffffff;
                require(first <= elements && blocks <= elements - first && table + (first + blocks) * static_cast<std::uint64_t>(width) <= 56ull + toc);
                const auto indices = read(table + first * static_cast<std::uint64_t>(width), blocks * width);
                Bytes result;
                result.reserve(length);
                for (unsigned b = 0; b < blocks; ++b)
                {
                    unsigned entry = 0;
                    std::memcpy(&entry, indices.data() + b * width, width);
                    const auto p = source + (entry & (flag - 1)) * 0x800ull;
                    const auto expected = std::min<unsigned>(0x8000, length - static_cast<unsigned>(result.size()));
                    Bytes block;
                    if (entry & flag)
                    {
                        const auto h = read(p, 7);
                        const auto packed = get<std::uint16_t>(h, 0);
                        const auto props = h[2];
                        lzma_options_lzma options{};
                        options.lc = props % 9;
                        options.lp = props / 9 % 5;
                        options.pb = props / 45;
                        options.dict_size = std::max(4096u, word(h, 3));
                        require(packed && options.pb <= 4 && options.lc + options.lp <= 4 && options.dict_size <= limit);
                        options.ext_flags = LZMA_LZMA1EXT_ALLOW_EOPM;
                        lzma_set_ext_size(options, expected);
                        lzma_filter filters[] = {{LZMA_FILTER_LZMA1EXT, &options}, {LZMA_VLI_UNKNOWN, nullptr}};
                        const auto payload = read(p + 7, packed);
                        block.resize(expected);
                        std::size_t in = 0, out = 0;
                        require(lzma_raw_buffer_decode(filters, nullptr, payload.data(), &in, payload.size(), block.data(), &out, block.size()) == LZMA_OK &&
                                out == expected && in == payload.size());
                    }
                    else
                        block = read(p, expected);
                    result.insert(result.end(), block.begin(), block.end());
                }
                return result;
            }
        };
        std::vector<std::filesystem::path> archives()
        {
            std::vector<std::filesystem::path> result;
            std::error_code error;
            const auto root = game_archives();
            for (std::filesystem::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error))
                if (it->is_regular_file(error) && lower(it->path().extension().string()) == ".pak")
                    result.push_back(it->path());
            std::sort(result.begin(), result.end());
            return result;
        }
        // FSB5 sample rates by index
        constexpr std::array<unsigned, 11> fsb5_rates{{4000, 8000, 11000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 96000}};
        std::string fsb5_codec(unsigned mode)
        {
            switch (mode)
            {
            case 1: return "PCM8";
            case 2: return "PCM16";
            case 3: return "PCM24";
            case 4: return "PCM32";
            case 5: return "PCM float";
            case 6: return "GC ADPCM";
            case 7: return "IMA ADPCM";
            case 8: return "VAG";
            case 10: return "XMA";
            case 11: return "MPEG";
            case 13: return "AT9";
            case 15: return "Vorbis";
            case 16: return "FADPCM";
            case 17: return "Opus";
            default: return "unknown (" + std::to_string(mode) + ")";
            }
        }
        std::string fsb4_codec(std::uint32_t mode)
        {
            if (mode & 0x200)
                return "MPEG";
            if (mode & 0x400000)
                return "IMA ADPCM";
            if (mode & 0x2000000)
                return "GC ADPCM";
            if (mode & 0x1000000)
                return "XMA";
            if (mode & 0x800000)
                return "VAG";
            if (mode & 0x8000000)
                return "Vorbis";
            if (mode & 0x10)
                return "PCM16";
            if (mode & 0x08)
                return "PCM8";
            return "unknown (mode " + std::to_string(mode) + ")";
        }
        // the samples of an FSB4 or FSB5 bank (vgmstream's layouts)
        std::vector<GameSound> bank_samples(const Bytes &b)
        {
            std::vector<GameSound> result;
            if (b.size() < 64)
                return result;
            if (!std::memcmp(b.data(), "FSB5", 4))
            {
                const auto version = word(b, 4), count = word(b, 8), headers = word(b, 12), names = word(b, 16), data = word(b, 20), mode = word(b, 24);
                const std::size_t base = version == 0 ? 0x40 : 0x3c;
                require(count <= 100000 && base + headers + names <= b.size());
                const auto data_start = base + headers + names;
                std::size_t p = base;
                for (unsigned i = 0; i < count; ++i)
                {
                    const auto low = word(b, p), high = word(b, p + 4);
                    p += 8;
                    GameSound s;
                    s.index = i;
                    s.codec = fsb5_codec(mode);
                    s.channels = ((low >> 5) & 0x03) + 1;
                    const auto rate = (low >> 1) & 0x0f;
                    s.rate = rate < fsb5_rates.size() ? fsb5_rates[rate] : 0;
                    s.offset = data_start + ((((static_cast<std::uint64_t>(high) & 0x03) << 25) | ((low >> 7) & 0x1ffffff)) << 5);
                    s.frames = (high >> 2) & 0x3fffffff;
                    // extra chunks: channels, rate, loop, codec setup ...
                    for (bool more = low & 1; more;)
                    {
                        const auto chunk = word(b, p);
                        more = chunk & 1;
                        const auto size = (chunk >> 1) & 0xffffff, type = (chunk >> 25) & 0x7f;
                        if (type == 1 && size >= 1)
                            s.channels = b.at(p + 4);
                        if (type == 2 && size >= 4)
                            s.rate = word(b, p + 4);
                        p += 4 + size;
                    }
                    result.push_back(s);
                }
                for (unsigned i = 0; i < result.size(); ++i)
                {
                    const auto end = i + 1 < result.size() ? result[i + 1].offset : data_start + data;
                    result[i].bytes = end > result[i].offset ? end - result[i].offset : 0;
                    if (names)
                        result[i].name = string_at(b, base + headers + word(b, base + headers + i * 4));
                }
                return result;
            }
            if (!std::memcmp(b.data(), "FSB4", 4))
            {
                const auto count = word(b, 4), headers = word(b, 8), flags = word(b, 20);
                require(count <= 100000 && 0x30ull + headers <= b.size());
                std::size_t p = 0x30;
                std::uint64_t offset = 0x30ull + headers;
                GameSound first;
                std::uint32_t first_mode = 0;
                for (unsigned i = 0; i < count; ++i)
                {
                    GameSound s;
                    s.index = i;
                    std::uint32_t mode = first_mode;
                    if (i == 0 || !(flags & 0x02))
                    {
                        const auto size = get<std::uint16_t>(b, p);
                        require(size >= 0x40 && p + size <= b.size());
                        const auto raw = std::string(reinterpret_cast<const char *>(b.data() + p + 2), 30);
                        s.name = raw.substr(0, raw.find('\0'));
                        s.frames = word(b, p + 0x20);
                        s.bytes = word(b, p + 0x24);
                        mode = word(b, p + 0x30);
                        s.rate = word(b, p + 0x34);
                        s.channels = get<std::uint16_t>(b, p + 0x3e);
                        p += size;
                        if (i == 0)
                            first = s, first_mode = mode;
                    }
                    else
                    {
                        // basic headers: only the length, the rest as the first sample
                        s.frames = word(b, p), s.bytes = word(b, p + 4);
                        s.rate = first.rate, s.channels = first.channels;
                        p += 8;
                    }
                    s.codec = fsb4_codec(mode);
                    s.offset = offset;
                    offset += s.bytes;
                    result.push_back(s);
                }
            }
            return result;
        }
        bool bank_name(const std::string &member)
        {
            const auto l = lower(member);
            return (l.size() > 4 && (l.compare(l.size() - 4, 4, ".snd") == 0 || l.compare(l.size() - 4, 4, ".fsb") == 0)) &&
                   l.find("sound") != std::string::npos;
        }
    } // namespace

    std::filesystem::path game_archives()
    {
        std::vector<wchar_t> exe(32768);
        const auto length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (!length || length >= exe.size())
            return {};
        return std::filesystem::path(exe.data()).parent_path() / L"archives";
    }
    std::vector<std::string> find_archive_members(const std::string &text, unsigned limit_lines)
    {
        std::vector<std::string> result;
        const auto wanted = lower(text);
        for (const auto &path : archives())
            try
            {
                Archive a(path);
                for (unsigned i = 0; i < a.count && result.size() < limit_lines; ++i)
                {
                    const auto name = a.name(i);
                    if (lower(name).find(wanted) != std::string::npos)
                        result.push_back(path.filename().string() + ": " + name + " (" + std::to_string(a.length(i)) + " bytes)");
                }
            }
            catch (const std::exception &)
            {
            }
        return result;
    }
    std::vector<GameSound> find_game_sounds(const std::string &text, bool everywhere, unsigned &scanned, unsigned limit_sounds)
    {
        std::vector<GameSound> result;
        scanned = 0;
        const auto wanted = lower(text);
        for (const auto &path : archives())
        {
            // the sound-sample banks shared by every level live in the always-loaded archives
            const auto pak = lower(path.filename().string());
            if (!everywhere && pak.find("permanent") == std::string::npos && pak.find("common") == std::string::npos &&
                pak.find("crash") == std::string::npos && pak.find("shared") == std::string::npos)
                continue;
            try
            {
                Archive a(path);
                for (unsigned i = 0; i < a.count && result.size() < limit_sounds; ++i)
                {
                    const auto name = a.name(i);
                    const auto l = lower(name);
                    // music streams are long; the TNT is a short effect
                    if (!bank_name(name) || l.find("music") != std::string::npos || l.find("stream") != std::string::npos || a.length(i) > 16u * 1024 * 1024)
                        continue;
                    ++scanned;
                    try
                    {
                        const auto bank = a.member(i);
                        for (auto &s : bank_samples(bank))
                            if ((lower(s.name).find(wanted) != std::string::npos || lower(name).find(wanted) != std::string::npos) && result.size() < limit_sounds)
                            {
                                s.pak = path.string(), s.member = name;
                                result.push_back(s);
                            }
                    }
                    catch (const std::exception &)
                    {
                    }
                }
            }
            catch (const std::exception &)
            {
            }
        }
        return result;
    }
    std::vector<unsigned char> game_sound_wav(const GameSound &sound, std::string &why)
    {
        const bool pcm16 = sound.codec == "PCM16", pcm8 = sound.codec == "PCM8";
        if (!pcm16 && !pcm8)
            return why = "its codec is " + sound.codec + ", which the client can't play yet", Bytes{};
        if (!sound.channels || sound.channels > 2 || !sound.rate || !sound.bytes || sound.bytes > 8u * 1024 * 1024)
            return why = "its format is unexpected", Bytes{};
        try
        {
            Archive a(sound.pak);
            Bytes bank;
            for (unsigned i = 0; i < a.count && bank.empty(); ++i)
                if (a.name(i) == sound.member)
                    bank = a.member(i);
            require(!bank.empty() && sound.offset <= bank.size() && sound.bytes <= bank.size() - sound.offset);
            const auto bits = pcm16 ? 16u : 8u, align = sound.channels * bits / 8;
            const auto size = static_cast<std::uint32_t>(sound.bytes - sound.bytes % align);
            Bytes wav(44 + size);
            const auto put32 = [&](std::size_t at, std::uint32_t v) { std::memcpy(&wav[at], &v, 4); };
            const auto put16 = [&](std::size_t at, std::uint16_t v) { std::memcpy(&wav[at], &v, 2); };
            std::memcpy(&wav[0], "RIFF", 4), put32(4, 36 + size), std::memcpy(&wav[8], "WAVE", 4);
            std::memcpy(&wav[12], "fmt ", 4), put32(16, 16), put16(20, 1), put16(22, static_cast<std::uint16_t>(sound.channels));
            put32(24, sound.rate), put32(28, sound.rate * align), put16(32, static_cast<std::uint16_t>(align)), put16(34, static_cast<std::uint16_t>(bits));
            std::memcpy(&wav[36], "data", 4), put32(40, size);
            std::memcpy(&wav[44], bank.data() + sound.offset, size);
            if (pcm8) // FMOD's 8-bit PCM is signed, WAV's unsigned
                for (std::size_t i = 44; i < wav.size(); ++i)
                    wav[i] = static_cast<unsigned char>(wav[i] ^ 0x80);
            return wav;
        }
        catch (const std::exception &e)
        {
            return why = std::string("its bank could not be read (") + e.what() + ")", Bytes{};
        }
    }
    std::filesystem::path dump_archive_member(const std::string &text, const std::filesystem::path &folder)
    {
        const auto wanted = lower(text);
        for (const auto &path : archives())
            try
            {
                Archive a(path);
                for (unsigned i = 0; i < a.count; ++i)
                {
                    const auto name = a.name(i);
                    if (lower(name).find(wanted) == std::string::npos)
                        continue;
                    const auto data = a.member(i);
                    std::filesystem::create_directories(folder);
                    auto file = name;
                    std::replace(file.begin(), file.end(), '\\', '/');
                    const auto target = folder / std::filesystem::path(file).filename();
                    std::ofstream out(target, std::ios::binary);
                    out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
                    return out ? target : std::filesystem::path{};
                }
            }
            catch (const std::exception &)
            {
            }
        return {};
    }
    std::vector<std::string> sound_search_report(const std::string &text, bool everywhere, bool choose, const std::string &prefer)
    {
        std::vector<std::string> lines;
        const auto root = game_archives();
        std::error_code error;
        if (root.empty() || !std::filesystem::is_directory(root, error))
            return {"Sound search: the game's archives folder was not found next to its .exe"};
        for (const auto &line : find_archive_members(text, 30))
            lines.push_back("Archive file: " + line);
        unsigned scanned = 0;
        auto sounds = find_game_sounds(text, everywhere, scanned, 80);
        lines.push_back("Sound search \"" + text + "\": " + std::to_string(sounds.size()) + " sounds in " + std::to_string(scanned) + " sound banks" +
                        (everywhere ? " (every archive)" : " (shared archives; add \"all\" for every archive)"));
        for (const auto &s : sounds)
            lines.push_back("Game sound: " + s.name + " (" + s.codec + ", " + std::to_string(s.channels) + " ch, " + std::to_string(s.rate) + " Hz, " +
                            std::to_string(s.rate ? 1000ull * s.frames / s.rate : 0) + " ms) in " + s.member + " [" +
                            std::filesystem::path(s.pak).filename().string() + "]");
        if (!choose)
            return lines;
        // the likeliest TNT countdown: a TNT sound that ticks, beeps or counts, not its explosion
        const GameSound *pick = nullptr;
        const auto has = [](const std::string &name, std::initializer_list<const char *> words) {
            const auto l = lower(name);
            return std::any_of(words.begin(), words.end(), [&](const char *w) { return l.find(w) != std::string::npos; });
        };
        for (const auto &s : sounds)
        {
            if (!prefer.empty() ? lower(s.name).find(lower(prefer)) != std::string::npos
                                : has(s.name, {"tnt"}) && has(s.name, {"count", "tick", "beep", "timer", "fuse"}) && !has(s.name, {"explo", "boom", "blast"}))
            {
                pick = &s;
                break;
            }
        }
        if (!pick && prefer.empty())
            for (const auto &s : sounds)
                if (has(s.name, {"tnt"}) && !has(s.name, {"explo", "boom", "blast", "break"}))
                {
                    pick = &s;
                    break;
                }
        if (!pick)
        {
            lines.push_back("Nitro Trap countdown: no TNT countdown sound found" + std::string(everywhere ? "" : " in the shared archives (try /sound_find tnt all)") +
                            "; it keeps the client's tick");
            return lines;
        }
        std::string why;
        auto wav = game_sound_wav(*pick, why);
        if (wav.empty())
        {
            lines.push_back("Nitro Trap countdown: found \"" + pick->name + "\" in " + pick->member + ", but " + why +
                            ". /sound_dump " + std::filesystem::path(pick->member).filename().string() + " saves its bank to send");
            return lines;
        }
        set_countdown_sound(std::move(wav), pick->name);
        lines.push_back("Nitro Trap countdown: now plays the game's own \"" + pick->name + "\" (" + pick->codec + ") on each number");
        return lines;
    }
}
