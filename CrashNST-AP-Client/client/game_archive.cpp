#include "game_archive.h"
#include "game_sound.h"
#include "sound_decode.h"
#include <set>
#include <cstdio>
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
#include <sstream>

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
            unsigned count = 0, toc = 0, large_blocks = 0, medium_blocks = 0, small_blocks = 0;
            explicit Archive(const std::filesystem::path &path) : stream(path, std::ios::binary)
            {
                require(static_cast<bool>(stream));
                stream.seekg(0, std::ios::end);
                size = static_cast<std::uint64_t>(stream.tellg());
                require(size >= 64);
                header = read(0, 56);
                require(word(header, 0) == 0x1a414749 && word(header, 4) == 11 && word(header, 16) == 0x800);
                toc = word(header, 8), count = word(header, 12), large_blocks = word(header, 28), medium_blocks = word(header, 32), small_blocks = word(header, 36);
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
            // (V1.4.7) the first of a member's two names in the name table (the game's build folder), and its record
            std::string prefix(unsigned i)
            {
                const auto p = word(names, i * 4);
                require(p >= count * 4);
                return string_at(names, p);
            }
            Bytes record(unsigned i) { return read(56ull + count * 4ull + i * 16ull, 16); }
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
                unsigned elements = large_blocks, width = 4, flag = 0x80000000;
                if (length <= 0x7f * 0x800)
                {
                    table += large_blocks * 4ull + medium_blocks * 2ull;
                    elements = small_blocks, width = 1, flag = 0x80;
                }
                else if (length <= 0x7fff * 0x800)
                {
                    table += large_blocks * 4ull;
                    elements = medium_blocks, width = 2, flag = 0x8000;
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
        std::string extension(const std::string &name)
        {
            const auto slash = name.find_last_of("/\\:");
            const auto dot = name.find_last_of('.');
            return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? std::string("(none)") : lower(name.substr(dot));
        }
        bool has_word(const std::string &name, std::initializer_list<const char *> words)
        {
            const auto l = lower(name);
            return std::any_of(words.begin(), words.end(), [&](const char *w) { return l.find(w) != std::string::npos; });
        }
        // archive files that hold sound samples: NST's sound_samples folder (one .igz per sound), or the usual
        // sound-bank kinds. sounds/banks/*.igz are only the banks' descriptions (events, volumes) and sound_streams
        // the music.
        bool sound_file(const std::string &name)
        {
            const auto l = lower(name);
            if (l.find("sound_streams") != std::string::npos || l.find("music") != std::string::npos)
                return false;
            if (l.find("sound_samples") != std::string::npos)
                return true;
            const auto kind = extension(name);
            return kind == ".snd" || kind == ".fsb" || kind == ".bank" || kind == ".wav";
        }
        // the sound's name in its file name: "sound_samples/wavs/Crates/fx_tnt_beep,Crates_bank,NoLoc,48000,40,0,0.igz"
        // is "fx_tnt_beep"
        std::string sound_name(std::string member)
        {
            std::replace(member.begin(), member.end(), '\\', '/');
            auto file = member.substr(member.find_last_of('/') + 1);
            const auto comma = file.find(',');
            return comma != std::string::npos ? file.substr(0, comma) : std::filesystem::path(file).stem().string();
        }
        std::string hex_bytes(const Bytes &b, std::size_t from, std::size_t count)
        {
            std::string text;
            char digits[4];
            for (std::size_t i = from; i < b.size() && i < from + count; ++i)
                std::snprintf(digits, sizeof(digits), "%02x", b[i]), text += (i > from ? " " : "") + std::string(digits);
            return text;
        }
        // where the usual audio signatures sit in a file, for files that held no sound bank
        std::string signatures(const Bytes &b)
        {
            std::string text;
            for (const char *magic : {"FSB5", "FSB4", "OggS", "RIFF", "vorbis", "OPUS", "XMA", "fLaC"})
            {
                const auto length = std::strlen(magic);
                const auto found = std::search(b.begin(), b.end(), magic, magic + length);
                if (found != b.end())
                {
                    char at[24];
                    std::snprintf(at, sizeof(at), " at 0x%zx", static_cast<std::size_t>(found - b.begin()));
                    text += (text.empty() ? "" : ", ") + std::string(magic) + at;
                }
            }
            return text.empty() ? "no audio signatures" : text;
        }
        // the always-loaded archives, where sounds shared by every level are likeliest
        bool shared_archive(const std::filesystem::path &path)
        {
            return has_word(path.filename().string(), {"permanent", "common", "crash", "shared", "sound", "audio", "global", "boot"});
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
            case 9: return "HEVAG";
            case 10: return "XMA";
            case 11: return "MPEG";
            case 12: return "CELT";
            case 13: return "AT9";
            case 14: return "XWMA";
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
        // the samples of an FSB5 or FSB4 bank starting at `at` (vgmstream's layouts); offsets are from the file's start
        std::vector<GameSound> fsb_samples(const Bytes &b, std::size_t at)
        {
            std::vector<GameSound> result;
            if (b.size() < at + 64)
                return result;
            if (!std::memcmp(b.data() + at, "FSB5", 4))
            {
                const auto version = word(b, at + 4), count = word(b, at + 8), headers = word(b, at + 12), names = word(b, at + 16), data = word(b, at + 20), mode = word(b, at + 24);
                const std::size_t base = at + (version == 0 ? 0x40 : 0x3c);
                require(count <= 100000 && headers <= limit && names <= limit && base + headers + names <= b.size());
                const auto data_start = base + headers + names;
                std::size_t p = base;
                for (unsigned i = 0; i < count; ++i)
                {
                    const auto low = word(b, p), high = word(b, p + 4);
                    p += 8;
                    GameSound s;
                    s.index = i;
                    s.codec = fsb5_codec(mode);
                    static constexpr unsigned channel_codes[] = {1, 2, 6, 8};
                    s.channels = channel_codes[(low >> 5) & 0x03];
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
                        if (type == 11 && size >= 4)
                            s.vorbis_setup = word(b, p + 4);
                        p += 4 + size;
                    }
                    result.push_back(s);
                }
                for (unsigned i = 0; i < result.size(); ++i)
                {
                    const auto end = i + 1 < result.size() ? result[i + 1].offset : static_cast<std::uint64_t>(data_start) + data;
                    result[i].bytes = end > result[i].offset ? end - result[i].offset : 0;
                    if (names)
                        result[i].name = string_at(b, base + headers + word(b, base + headers + i * 4));
                }
                return result;
            }
            if (!std::memcmp(b.data() + at, "FSB4", 4))
            {
                const auto count = word(b, at + 4), headers = word(b, at + 8), flags = word(b, at + 20);
                require(count <= 100000 && at + 0x30ull + headers <= b.size());
                std::size_t p = at + 0x30;
                std::uint64_t offset = at + 0x30ull + headers;
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
        // a RIFF WAVE file: one sample
        std::vector<GameSound> wav_samples(const Bytes &b)
        {
            std::vector<GameSound> result;
            if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4))
                return result;
            GameSound s;
            unsigned tag = 0, bits = 0, align = 0;
            for (std::size_t p = 12; p + 8 <= b.size();)
            {
                const auto size = word(b, p + 4);
                if (!std::memcmp(b.data() + p, "fmt ", 4) && size >= 16)
                {
                    tag = get<std::uint16_t>(b, p + 8), s.channels = get<std::uint16_t>(b, p + 10), s.rate = word(b, p + 12);
                    align = get<std::uint16_t>(b, p + 20), bits = get<std::uint16_t>(b, p + 22);
                }
                if (!std::memcmp(b.data() + p, "data", 4))
                {
                    s.offset = p + 8;
                    s.bytes = std::min<std::uint64_t>(size, b.size() - (p + 8));
                    break;
                }
                p += 8 + size + (size & 1);
            }
            if (!s.bytes || !tag)
                return result;
            s.signed8 = false;
            s.codec = tag == 1 ? (bits == 16 ? "PCM16" : bits == 8 ? "PCM8" : "PCM" + std::to_string(bits))
                      : tag == 2      ? "MS ADPCM"
                      : tag == 0x11   ? "IMA ADPCM"
                      : tag == 0x55   ? "MPEG"
                                      : "WAV format " + std::to_string(tag);
            s.frames = tag == 1 && align ? static_cast<unsigned>(s.bytes / align) : 0;
            result.push_back(s);
            return result;
        }
        // the samples in an archive file: an FSB bank (alone, or inside an FMOD Studio .bank or another container) or a WAV.
        // `format` names what the file turned out to be.
        std::vector<GameSound> file_samples(const Bytes &b, std::string &format, const std::string &member);
        std::vector<GameSound> samples_of(const Bytes &b, std::string &format)
        {
            static const char fsb5[] = "FSB5", fsb4[] = "FSB4";
            for (const auto magic : {fsb5, fsb4})
            {
                const auto found = std::search(b.begin(), b.end(), magic, magic + 4);
                if (found != b.end())
                {
                    format = magic;
                    return fsb_samples(b, static_cast<std::size_t>(found - b.begin()));
                }
            }
            auto wav = wav_samples(b);
            if (!wav.empty())
                return format = "WAV", wav;
            format = "other";
            if (b.size() >= 4)
            {
                std::string magic;
                for (int i = 0; i < 4; ++i)
                    magic += std::isprint(b[i]) ? static_cast<char>(b[i]) : '.';
                format = "other \"" + magic + "\"";
            }
            return {};
        }
        // a file holding one unnamed sample (a WAV, a one-sound bank) is named for the file
        std::vector<GameSound> file_samples(const Bytes &b, std::string &format, const std::string &member)
        {
            auto samples = samples_of(b, format);
            if (samples.size() == 1 && samples[0].name.empty())
                samples[0].name = sound_name(member);
            return samples;
        }
        std::string label(const GameSound &s)
        {
            return s.name.empty() ? "#" + std::to_string(s.index) : s.name;
        }
        std::string describe(const GameSound &s)
        {
            return label(s) + " (" + s.codec + ", " + std::to_string(s.channels) + " ch, " + std::to_string(s.rate) + " Hz, " +
                   std::to_string(s.rate ? 1000ull * s.frames / s.rate : 0) + " ms) in " + s.member + " [" + std::filesystem::path(s.pak).filename().string() + "]";
        }
        // the archive file's samples, read again (for playing or a remembered choice)
        std::vector<GameSound> reread(const std::filesystem::path &pak, const std::string &member, Bytes &data)
        {
            Archive a(pak);
            for (unsigned i = 0; i < a.count; ++i)
                if (a.name(i) == member)
                {
                    data = a.member(i);
                    std::string format;
                    auto samples = file_samples(data, format, member);
                    for (auto &s : samples)
                        s.pak = pak.string(), s.member = member;
                    return samples;
                }
            throw std::runtime_error("the file is no longer in its archive");
        }
        std::vector<unsigned char> wav_from(const GameSound &sound, const Bytes &bank, std::string &why)
        {
            if (sound.offset > bank.size() || sound.bytes > bank.size() - sound.offset || sound.bytes > 16u * 1024 * 1024)
                return why = "its data is outside its file", Bytes{};
            Pcm pcm;
            if (!decode_fmod_sample(sound.codec, bank.data() + sound.offset, static_cast<std::size_t>(sound.bytes), sound.channels, sound.rate, sound.frames,
                                    sound.vorbis_setup, sound.signed8, pcm, why))
                return Bytes{};
            if (pcm.samples.size() < pcm.channels * 64ull)
                return why = "it decoded to almost nothing", Bytes{};
            return wav_file(pcm);
        }
        // the likeliest TNT countdown: a TNT sound that ticks, beeps or counts, not its explosion. With `prefer`, the
        // first sound whose name (or "file #index") contains it.
        const GameSound *countdown_choice(const std::vector<GameSound> &sounds, const std::string &prefer)
        {
            const auto full = [](const GameSound &s) { return label(s) + " " + s.member + " #" + std::to_string(s.index); };
            if (!prefer.empty())
            {
                for (const auto &s : sounds)
                    if (lower(label(s)).find(lower(prefer)) != std::string::npos)
                        return &s;
                for (const auto &s : sounds)
                    if (lower(full(s)).find(lower(prefer)) != std::string::npos)
                        return &s;
                return nullptr;
            }
            // a TNT sound that ticks, beeps or counts (not its explosion), the likeliest words first
            const GameSound *best = nullptr;
            int best_score = 0;
            for (const auto &s : sounds)
            {
                const auto name = full(s);
                if (!has_word(name, {"tnt"}) || has_word(label(s), {"explo", "boom", "blast", "break", "destroy", "smash"}))
                    continue;
                int score = 1;
                const std::pair<const char *, int> words[] = {{"countdown", 6}, {"count", 5}, {"tick", 5}, {"beep", 4}, {"timer", 3}, {"fuse", 2}, {"crate", 1}};
                for (const auto &[word, points] : words)
                    if (has_word(label(s), {word}))
                    {
                        score += points;
                        break;
                    }
                if (score > best_score)
                    best = &s, best_score = score;
            }
            return best;
        }
        // loads the sound for the Nitro countdown; a console line about it. `remember` keeps the choice for next time.
        std::string use_countdown(const GameSound &sound, const std::filesystem::path &remember, bool remembered = false)
        {
            std::string why;
            Bytes bank;
            Bytes wav;
            try
            {
                reread(sound.pak, sound.member, bank);
                wav = wav_from(sound, bank, why);
            }
            catch (const std::exception &e)
            {
                why = std::string("its file could not be read (") + e.what() + ")";
            }
            if (wav.empty())
            {
                // a copy of the file to send, so the next version can play it
                std::string saved;
                if (!remember.empty() && !bank.empty())
                {
                    std::error_code error;
                    const auto folder = remember.parent_path() / "dumps";
                    std::filesystem::create_directories(folder, error);
                    const auto target = folder / (sound_name(sound.member) + ".bin");
                    std::ofstream out(target, std::ios::binary);
                    out.write(reinterpret_cast<const char *>(bank.data()), static_cast<std::streamsize>(bank.size()));
                    if (out)
                        saved = ". Its file is saved as " + target.string() + " (please send it)";
                }
                return "Nitro Trap countdown: found \"" + label(sound) + "\" (" + sound.codec + ", " + std::to_string(sound.channels) + " ch, " +
                       std::to_string(sound.rate) + " Hz, " + std::to_string(sound.bytes) + " bytes at " + std::to_string(sound.offset) + " of a " +
                       std::to_string(bank.size()) + "-byte file), but " + why + saved;
            }
            set_countdown_sound(std::move(wav), label(sound));
            if (!remember.empty() && !remembered)
            {
                std::error_code error;
                std::filesystem::create_directories(remember.parent_path(), error);
                std::ofstream out(remember, std::ios::binary);
                out << sound.pak << '\n' << sound.member << '\n' << sound.index << '\n' << label(sound) << '\n';
            }
            return "Nitro Trap countdown: plays the game's own \"" + label(sound) + "\" (" + sound.codec + ", " +
                   std::to_string(sound.rate ? 1000ull * sound.frames / sound.rate : 0) + " ms)" + (remembered ? " (remembered from last time)" : "");
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
    std::vector<GameSound> find_game_sounds(const std::string &text, bool everywhere, SoundScan &scan, unsigned limit_sounds)
    {
        std::vector<GameSound> result;
        scan = {};
        const auto wanted = lower(text);
        std::set<std::string> read; // the same sound file sits in many level archives
        for (const auto &path : archives())
        {
            try
            {
                Archive a(path);
                for (unsigned i = 0; i < a.count && result.size() < limit_sounds; ++i)
                {
                    const auto name = a.name(i);
                    const auto l = lower(name);
                    const bool named = !wanted.empty() && l.find(wanted) != std::string::npos;
                    if ((!named && !everywhere) || !sound_file(name) || a.length(i) > 16u * 1024 * 1024 || !read.insert(l).second)
                        continue;
                    ++scan.files;
                    try
                    {
                        const auto data = a.member(i);
                        std::string format;
                        auto samples = file_samples(data, format, name);
                        ++scan.formats[format.rfind("other", 0) == 0 ? std::string("other") : format];
                        if (samples.empty() && scan.notes.size() < 4)
                            scan.notes.push_back(sound_name(name) + " (" + std::to_string(data.size()) + " bytes, " + format + "): " + signatures(data) +
                                                 "; starts " + hex_bytes(data, 0, 48));
                        for (auto &s : samples)
                        {
                            ++scan.codecs[s.codec];
                            ++(s.name.empty() ? scan.unnamed : scan.named);
                            if ((named || lower(s.name).find(wanted) != std::string::npos) && result.size() < limit_sounds)
                            {
                                s.pak = path.string(), s.member = name;
                                result.push_back(s);
                            }
                        }
                    }
                    catch (const std::exception &e)
                    {
                        ++scan.formats["unreadable"];
                        if (scan.notes.size() < 4)
                            scan.notes.push_back(sound_name(name) + ": " + e.what());
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
        try
        {
            Bytes bank;
            reread(sound.pak, sound.member, bank);
            return wav_from(sound, bank, why);
        }
        catch (const std::exception &e)
        {
            return why = std::string("its file could not be read (") + e.what() + ")", Bytes{};
        }
    }
    std::vector<std::string> archive_member_report(const std::string &text)
    {
        std::vector<std::string> lines;
        const auto wanted = lower(text);
        const auto root = game_archives();
        const auto paks = archives();
        struct Seen
        {
            std::uint64_t size = 0;
            std::vector<std::string> where;
        };
        std::map<std::string, Seen> members;
        std::string names;
        unsigned with = 0, unreadable = 0, listed = 0;
        for (const auto &path : paks)
        {
            std::error_code error;
            auto relative = std::filesystem::relative(path, root, error);
            const auto label = (error ? path.filename() : relative).generic_string();
            try
            {
                Archive a(path);
                bool any = false;
                for (unsigned i = 0; i < a.count; ++i)
                {
                    const auto name = a.name(i);
                    if (lower(name).find(wanted) == std::string::npos)
                        continue;
                    any = true;
                    auto &m = members[lower(name)];
                    m.size = a.length(i);
                    m.where.push_back(label);
                }
                with += any;
                names += (listed++ % 10 ? ", " : "\n  ") + label + " (" + std::to_string(a.count) + ")";
            }
            catch (const std::exception &)
            {
                ++unreadable;
            }
        }
        lines.push_back("Archive search \"" + text + "\": " + std::to_string(members.size()) + " different files in " + std::to_string(with) + " of " +
                        std::to_string(paks.size()) + " archives" + (unreadable ? " (" + std::to_string(unreadable) + " unreadable)" : ""));
        std::vector<std::pair<std::string, const Seen *>> sorted;
        for (const auto &[name, seen] : members)
            sorted.push_back({name, &seen});
        std::stable_sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.second->where.size() > b.second->where.size(); });
        for (std::size_t i = 0; i < sorted.size() && i < 120; ++i)
        {
            const auto &where = sorted[i].second->where;
            std::string list;
            for (std::size_t j = 0; j < where.size() && j < 4; ++j)
                list += (j ? ", " : "") + where[j];
            lines.push_back("  " + sorted[i].first + " (" + std::to_string(sorted[i].second->size) + " bytes) in " + std::to_string(where.size()) +
                            (where.size() == 1 ? " archive: " : " archives: ") + list + (where.size() > 4 ? ", ..." : ""));
        }
        if (sorted.size() > 120)
            lines.push_back("  ... and " + std::to_string(sorted.size() - 120) + " more");
        lines.push_back("Archives (files inside):" + names);
        return lines;
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
    // (V1.4.3 research, Testing build: the real Nitro in Ripper Roo's arena, Eric: "You should do the real nitro, just
    // like the nitro trap"; his arena's archive has the Nitro's textures but no Nitro template is loaded there, while
    // N. Sanity Beach (L101) has one): what Roo's arena, N. Sanity Beach and Crash_Crates.pak hold, to find what makes a
    // level load the Nitro. One zip: each archive's member list (name, size, how it's stored), its small level files
    // (not textures, sounds, models, animations, materials, shaders or effects; 3 MB each at most, 24 MB an archive),
    // and every member of theirs naming the Nitro crate (Crash_Crate_Nitro), up to 16 MB each
    namespace
    {
        std::uint32_t crc32_of(const Bytes &data)
        {
            static const auto table = []
            {
                std::array<std::uint32_t, 256> t{};
                for (std::uint32_t i = 0; i < 256; ++i)
                {
                    std::uint32_t c = i;
                    for (int k = 0; k < 8; ++k)
                        c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
                    t[i] = c;
                }
                return t;
            }();
            std::uint32_t c = 0xffffffffu;
            for (const auto b : data)
                c = table[(c ^ b) & 0xff] ^ (c >> 8);
            return c ^ 0xffffffffu;
        }
        // a zip of whole files, stored (no compression)
        struct ZipOut
        {
            std::ofstream out;
            Bytes central;
            unsigned files = 0;
            std::uint64_t at = 0;
            explicit ZipOut(const std::filesystem::path &path) : out(path, std::ios::binary) {}
            static void put16(Bytes &b, std::uint16_t v) { b.push_back(v & 0xff), b.push_back(v >> 8); }
            static void put32(Bytes &b, std::uint32_t v) { put16(b, v & 0xffff), put16(b, v >> 16); }
            void add(const std::string &name, const Bytes &data)
            {
                const auto crc = crc32_of(data);
                const auto size = static_cast<std::uint32_t>(data.size());
                Bytes local;
                put32(local, 0x04034b50), put16(local, 20), put16(local, 0), put16(local, 0), put16(local, 0), put16(local, 0x21);
                put32(local, crc), put32(local, size), put32(local, size), put16(local, static_cast<std::uint16_t>(name.size())), put16(local, 0);
                local.insert(local.end(), name.begin(), name.end());
                put32(central, 0x02014b50), put16(central, 20), put16(central, 20), put16(central, 0), put16(central, 0), put16(central, 0), put16(central, 0x21);
                put32(central, crc), put32(central, size), put32(central, size), put16(central, static_cast<std::uint16_t>(name.size())), put16(central, 0), put16(central, 0);
                put16(central, 0), put16(central, 0), put32(central, 0), put32(central, static_cast<std::uint32_t>(at));
                central.insert(central.end(), name.begin(), name.end());
                out.write(reinterpret_cast<const char *>(local.data()), static_cast<std::streamsize>(local.size()));
                out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
                at += local.size() + data.size(), ++files;
            }
            bool finish()
            {
                Bytes end;
                put32(end, 0x06054b50), put16(end, 0), put16(end, 0), put16(end, static_cast<std::uint16_t>(files)), put16(end, static_cast<std::uint16_t>(files));
                put32(end, static_cast<std::uint32_t>(central.size())), put32(end, static_cast<std::uint32_t>(at)), put16(end, 0);
                out.write(reinterpret_cast<const char *>(central.data()), static_cast<std::streamsize>(central.size()));
                out.write(reinterpret_cast<const char *>(end.data()), static_cast<std::streamsize>(end.size()));
                out.flush();
                return static_cast<bool>(out);
            }
        };
        bool names_nitro(const Bytes &data)
        {
            static const std::string a = "Crash_Crate_Nitro", b = "crash_crate_nitro";
            return std::search(data.begin(), data.end(), a.begin(), a.end()) != data.end() || std::search(data.begin(), data.end(), b.begin(), b.end()) != data.end();
        }
    }
    std::vector<std::string> nitro_research_export(const std::filesystem::path &folder)
    {
        std::vector<std::string> lines;
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        const auto zip_path = folder / L"roo_nitro_files.zip";
        ZipOut zip(zip_path);
        if (!zip.out)
            return {"Roo files: couldn't write " + zip_path.string()};
        const std::vector<std::string> wanted{"b102_ripperroo.pak", "l101_nsanitybeach.pak", "crash_crates.pak"};
        std::uint64_t total = 0;
        for (const auto &path : archives())
        {
            const auto file = lower(path.filename().string());
            if (std::find(wanted.begin(), wanted.end(), file) == wanted.end())
                continue;
            try
            {
                Archive a(path);
                std::ostringstream listing;
                std::map<std::string, std::pair<unsigned, std::uint64_t>> folders;
                std::uint64_t taken = 0;
                unsigned kept = 0, nitro = 0;
                std::string nitro_names;
                for (unsigned i = 0; i < a.count; ++i)
                {
                    const auto name = a.name(i);
                    const auto info = a.read(56ull + a.count * 4ull + i * 16ull, 16);
                    const auto length = word(info, 8), command = word(info, 12);
                    listing << i << "\t" << name << "\t" << length << "\t" << (command == 0xffffffff ? "whole" : "lzma") << "\n";
                    const auto l = lower(name);
                    const auto top = l.substr(0, l.find_first_of("/\\"));
                    folders[top].first++, folders[top].second += length;
                    const bool asset = has_word(l, {"textures/", "sound_samples/", "sound_streams/", "models/", "anims/", "materialinstances/", "shaders/", "vfx/", "fonts/", "movies/"});
                    if (asset && !has_word(l, {"nitro"}))
                        continue;
                    if (length > 16u * 1024 * 1024)
                        continue;
                    Bytes data;
                    try
                    {
                        data = a.member(i);
                    }
                    catch (const std::exception &)
                    {
                        continue;
                    }
                    const bool names = !asset && names_nitro(data);
                    const bool little = !asset && length <= 3u * 1024 * 1024 && taken + length <= 24u * 1024 * 1024;
                    const bool nitro_file = has_word(l, {"nitro"}) && length <= 3u * 1024 * 1024;
                    if (!names && !little && !nitro_file)
                        continue;
                    auto member = name;
                    std::replace(member.begin(), member.end(), '\\', '/');
                    zip.add(path.stem().string() + "/" + member, data);
                    ++kept, total += data.size();
                    if (!names)
                        taken += data.size();
                    else
                        ++nitro, nitro_names += (nitro_names.empty() ? "" : ", ") + name;
                }
                const auto text = listing.str();
                zip.add(path.stem().string() + ".txt", Bytes(text.begin(), text.end()));
                std::ostringstream line;
                line << "Roo files: " << path.filename().string() << ": " << a.count << " files; " << kept << " copied; " << nitro << " naming the Nitro crate"
                     << (nitro_names.empty() ? std::string() : " (" + nitro_names + ")") << "; by folder:";
                for (const auto &[top, counts] : folders)
                    line << " " << top << " " << counts.first << " (" << counts.second / 1024 << " KB)";
                lines.push_back(line.str());
            }
            catch (const std::exception &e)
            {
                lines.push_back("Roo files: " + path.filename().string() + " couldn't be read (" + e.what() + ")");
            }
        }
        if (!zip.finish())
            return {"Roo files: couldn't finish " + zip_path.string()};
        lines.push_back("Roo files: done: " + zip_path.string() + " (" + std::to_string(zip.files) + " files, " + std::to_string((zip.at + 1023) / 1024) +
                        " KB). Send it in the chat with your log.");
        return lines;
    }
    // V1.4.7 (Testing build): the real Nitro in Ripper Roo's arena (Eric: "You should do the real nitro, just like the
    // nitro trap"; /roo_files: a level loads the files its package list names (packages/generated/maps/.../<level>
    // _pkg.igz: pairs of a kind and a file, "igx_entities" for its map layers), an archive's table is sorted by the
    // FNV-1a hash of each member's name in lower case (all 1030 of Roo's in order), and the Nitro's template
    // (Crate_Nitro_Spawned_gen) is only in N. Sanity Beach's crate layer, l101_nsanitybeach_crates.igz, with its files
    // (the Nitro's model, material, script, explosion) in N. Sanity Beach's archive and not Roo's). A copy of Roo's
    // archive is made with that crate layer (as a plain file, "igx_file", not a map layer), everything it needs that
    // Roo's archive lacks (its dependency list, TDEP, followed through every file it names), and those added to his
    // package list; every member stored whole. The loader puts it in place at the game's start (see dinput_proxy.cpp)
    namespace
    {
        std::uint32_t fnv1a_lower(const std::string &name)
        {
            std::uint32_t h = 0x811c9dc5u;
            for (const unsigned char c : name)
                h = (h ^ static_cast<unsigned char>(std::tolower(c))) * 0x01000193u;
            return h;
        }
        void put32(Bytes &b, std::size_t at, std::uint32_t v) { std::memcpy(b.data() + at, &v, 4); }
        void put64(Bytes &b, std::size_t at, std::uint64_t v) { std::memcpy(b.data() + at, &v, 8); }
        // an IGZ (version 10) file: its 0x800-byte header with the section table, its fixup chunks, and its data
        struct IgzChunk
        {
            std::string magic;
            std::uint32_t count = 0, header = 16;
            Bytes data;
        };
        struct Igz
        {
            Bytes head;
            std::vector<IgzChunk> chunks;
            Bytes data;
        };
        Igz igz_read(const Bytes &b)
        {
            require(b.size() >= 0x800 && word(b, 0) == 0x49475a01 && word(b, 4) == 10);
            Igz f;
            f.head.assign(b.begin(), b.begin() + 0x800);
            // sections: {pool, 0, offset, size}; the first the fixups, the second the data (a package list has just these)
            require(word(b, 0x30 + 8) == 0 && word(b, 0x30 + 12) == 0);
            const auto fix_at = word(b, 0x18), fix_size = word(b, 0x1c), data_at = word(b, 0x28), data_size = word(b, 0x2c);
            require(fix_at == 0x800 && data_at == fix_at + fix_size && data_at + static_cast<std::uint64_t>(data_size) <= b.size());
            for (std::uint32_t p = fix_at; p < fix_at + fix_size;)
            {
                IgzChunk c;
                c.magic.assign(reinterpret_cast<const char *>(b.data() + p), 4);
                c.count = word(b, p + 4);
                const auto size = word(b, p + 8);
                c.header = word(b, p + 12);
                require(size >= c.header && c.header >= 16 && p + static_cast<std::uint64_t>(size) <= fix_at + fix_size);
                c.data.assign(b.begin() + p + c.header, b.begin() + p + size);
                f.chunks.push_back(std::move(c));
                p += size;
            }
            f.data.assign(b.begin() + data_at, b.begin() + data_at + data_size);
            return f;
        }
        Bytes igz_write(Igz f)
        {
            Bytes fix;
            for (auto &c : f.chunks)
            {
                while (c.data.size() % 4)
                    c.data.push_back(0);
                const auto at = fix.size();
                fix.resize(at + c.header + c.data.size());
                std::memcpy(fix.data() + at, c.magic.data(), 4);
                put32(fix, at + 4, c.count), put32(fix, at + 8, static_cast<std::uint32_t>(c.header + c.data.size())), put32(fix, at + 12, c.header);
                std::copy(c.data.begin(), c.data.end(), fix.begin() + static_cast<std::ptrdiff_t>(at + c.header));
            }
            Bytes out = f.head;
            put32(out, 0x18, 0x800), put32(out, 0x1c, static_cast<std::uint32_t>(fix.size()));
            put32(out, 0x28, static_cast<std::uint32_t>(0x800 + fix.size())), put32(out, 0x2c, static_cast<std::uint32_t>(f.data.size()));
            out.insert(out.end(), fix.begin(), fix.end());
            out.insert(out.end(), f.data.begin(), f.data.end());
            return out;
        }
        IgzChunk *igz_chunk(Igz &f, const char *magic)
        {
            for (auto &c : f.chunks)
                if (c.magic == magic)
                    return &c;
            return nullptr;
        }
        // the string table: each string on an even offset
        std::vector<std::string> igz_strings(const IgzChunk &c)
        {
            std::vector<std::string> out;
            std::size_t i = 0;
            for (std::uint32_t k = 0; k < c.count; ++k)
            {
                require(i < c.data.size());
                const auto end = std::find(c.data.begin() + static_cast<std::ptrdiff_t>(i), c.data.end(), 0);
                require(end != c.data.end());
                out.emplace_back(c.data.begin() + static_cast<std::ptrdiff_t>(i), end);
                i = static_cast<std::size_t>(end - c.data.begin()) + 1;
                i += i & 1;
            }
            return out;
        }
        Bytes igz_string_table(const std::vector<std::string> &strings)
        {
            Bytes out;
            for (const auto &text : strings)
            {
                out.insert(out.end(), text.begin(), text.end());
                out.push_back(0);
                if (out.size() & 1)
                    out.push_back(0);
            }
            return out;
        }
        // the fixup lists' offsets (in 4-byte steps): each the one before plus 3 bits a nibble, low nibble first, the
        // high bit of a nibble saying more follow
        std::vector<std::uint32_t> igz_unpack(const Bytes &d, std::uint32_t count)
        {
            std::vector<std::uint32_t> out;
            std::uint32_t at = 0;
            std::size_t nibble = 0;
            const auto next = [&]() -> unsigned {
                require(nibble / 2 < d.size());
                const unsigned v = nibble % 2 ? d[nibble / 2] >> 4 : d[nibble / 2] & 15;
                ++nibble;
                return v;
            };
            while (out.size() < count)
            {
                std::uint32_t value = 0, shift = 0;
                unsigned n = 0;
                do
                {
                    n = next();
                    value |= (n & 7u) << shift, shift += 3;
                } while ((n & 8) && shift < 32);
                at += value;
                out.push_back(at);
            }
            return out;
        }
        Bytes igz_pack(const std::vector<std::uint32_t> &values)
        {
            std::vector<unsigned> nibbles;
            std::uint32_t before = 0;
            for (const auto v : values)
            {
                require(v >= before);
                std::uint32_t d = v - before;
                before = v;
                do
                {
                    const unsigned n = d & 7;
                    d >>= 3;
                    nibbles.push_back(d ? n | 8 : n);
                } while (d);
            }
            if (nibbles.size() % 2)
                nibbles.push_back(0);
            Bytes out;
            for (std::size_t i = 0; i < nibbles.size(); i += 2)
                out.push_back(static_cast<unsigned char>(nibbles[i] | (nibbles[i + 1] << 4)));
            return out;
        }
        // a dependency's path as an archive member's name: "<build>models:/Levels/x.igz" -> "models/levels/x.igz"
        std::string member_of(std::string path)
        {
            if (path.rfind("<build>", 0) == 0)
                path.erase(0, 7);
            const auto colon = path.find(":/");
            if (colon == std::string::npos)
                return {};
            const auto ns = lower(path.substr(0, colon));
            auto rest = path.substr(colon + 2);
            static const std::map<std::string, std::string> folders{{"materials", "materialinstances/"}, {"data", ""}};
            if (ns == "project")
            {
                const auto out = rest.find("/output/");
                rest = out == std::string::npos ? rest : rest.substr(out + 8);
                return lower(rest);
            }
            const auto it = folders.find(ns);
            return lower((it == folders.end() ? ns + "/" : it->second) + rest);
        }
        // an IGZ file's dependencies (its TDEP list: name, path, name, path ...) as member names
        std::vector<std::string> igz_dependencies(const Bytes &b)
        {
            std::vector<std::string> out;
            if (b.size() < 0x800 || word(b, 0) != 0x49475a01 || word(b, 4) != 10)
                return out;
            const auto fix_at = word(b, 0x18), fix_size = word(b, 0x1c);
            for (std::uint32_t p = fix_at; p + 16 <= fix_at + static_cast<std::uint64_t>(fix_size) && p + 16 <= b.size();)
            {
                const auto size = word(b, p + 8), header = word(b, p + 12), count = word(b, p + 4);
                if (size < 16 || p + static_cast<std::uint64_t>(size) > b.size())
                    break;
                if (std::memcmp(b.data() + p, "TDEP", 4) == 0)
                {
                    std::vector<std::string> strings;
                    std::size_t i = p + header;
                    while (strings.size() < count && i < p + size)
                    {
                        const auto end = std::find(b.begin() + static_cast<std::ptrdiff_t>(i), b.begin() + p + size, 0);
                        strings.emplace_back(b.begin() + static_cast<std::ptrdiff_t>(i), end);
                        i = static_cast<std::size_t>(end - b.begin()) + 1;
                    }
                    for (std::size_t k = 1; k < strings.size(); k += 2)
                        if (const auto m = member_of(strings[k]); !m.empty())
                            out.push_back(m);
                }
                p += size;
            }
            return out;
        }
        // a package list's pairs (kind, file); its string table, chunk info and pair array
        struct Package
        {
            Igz igz;
            std::vector<std::string> strings;
            std::size_t info = 0; // the igStreamingChunkInfo's place in the data
            std::vector<std::pair<std::uint64_t, std::uint64_t>> pairs;
        };
        Package package_read(const Bytes &b)
        {
            Package p;
            p.igz = igz_read(b);
            const auto *tstr = igz_chunk(p.igz, "TSTR");
            const auto *tmet = igz_chunk(p.igz, "TMET");
            const auto *rvtb = igz_chunk(p.igz, "RVTB");
            require(tstr && tmet && rvtb && igz_chunk(p.igz, "RSTT"));
            p.strings = igz_strings(*tstr);
            // the chunk info: the object whose type (its first word, a TMET index) is igStreamingChunkInfo
            std::vector<std::string> types;
            for (std::size_t i = 0; i < tmet->data.size();)
            {
                const auto end = std::find(tmet->data.begin() + static_cast<std::ptrdiff_t>(i), tmet->data.end(), 0);
                types.emplace_back(tmet->data.begin() + static_cast<std::ptrdiff_t>(i), end);
                i = static_cast<std::size_t>(end - tmet->data.begin()) + 1;
                i += i & 1;
            }
            bool found = false;
            for (const auto at : igz_unpack(rvtb->data, rvtb->count))
            {
                const std::size_t object = at * 4ull;
                require(object + 8 <= p.igz.data.size());
                const auto type = get<std::uint64_t>(p.igz.data, object);
                if (type < types.size() && types[type] == "igStreamingChunkInfo")
                    require(object + 64 <= p.igz.data.size()), p.info = object, found = true;
            }
            require(found);
            const auto n = get<std::uint64_t>(p.igz.data, p.info + 0x28), at = get<std::uint64_t>(p.igz.data, p.info + 0x38);
            require(n < 100000 && at + n * 16 <= p.igz.data.size());
            for (std::uint64_t k = 0; k < n; ++k)
            {
                const auto kind = get<std::uint64_t>(p.igz.data, at + k * 16), file = get<std::uint64_t>(p.igz.data, at + k * 16 + 8);
                require(kind < p.strings.size() && file < p.strings.size());
                p.pairs.emplace_back(kind, file);
            }
            return p;
        }
        // the list with more pairs: their strings at the end of the string table, the whole pair array again at the end of
        // the data (the old one left where it was, unused), and its string references added to RSTT
        Bytes package_add(Package p, const std::vector<std::pair<std::string, std::string>> &more)
        {
            std::map<std::string, std::uint64_t> index;
            for (std::size_t i = 0; i < p.strings.size(); ++i)
                index.emplace(p.strings[i], i);
            const auto string_of = [&](const std::string &text) {
                if (const auto it = index.find(text); it != index.end())
                    return it->second;
                p.strings.push_back(text);
                index.emplace(text, p.strings.size() - 1);
                return static_cast<std::uint64_t>(p.strings.size() - 1);
            };
            for (const auto &[kind, file] : more)
                p.pairs.emplace_back(string_of(kind), string_of(file));
            auto &data = p.igz.data;
            while (data.size() % 16)
                data.push_back(0);
            const auto at = data.size();
            data.resize(at + p.pairs.size() * 16);
            for (std::size_t k = 0; k < p.pairs.size(); ++k)
                put64(data, at + k * 16, p.pairs[k].first), put64(data, at + k * 16 + 8, p.pairs[k].second);
            const auto memory = get<std::uint64_t>(data, p.info + 0x30);
            put64(data, p.info + 0x28, p.pairs.size());
            put64(data, p.info + 0x30, (memory & 0xffffffff00000000ULL) | (p.pairs.size() * 16)); // (its flags kept)
            put64(data, p.info + 0x38, at);
            auto *tstr = igz_chunk(p.igz, "TSTR");
            tstr->data = igz_string_table(p.strings), tstr->count = static_cast<std::uint32_t>(p.strings.size());
            auto *rstt = igz_chunk(p.igz, "RSTT");
            auto refs = igz_unpack(rstt->data, rstt->count);
            require(refs.empty() || refs.back() * 4ull < at);
            for (std::size_t k = 0; k < p.pairs.size() * 2; ++k)
                refs.push_back(static_cast<std::uint32_t>((at + k * 8) / 4));
            rstt->data = igz_pack(refs), rstt->count = static_cast<std::uint32_t>(refs.size());
            return igz_write(p.igz);
        }
        // the lookup numbers in an archive's header (words 20 and 24): a name's place in the table is about its hash /
        // divider, within slop of it (the game's own: divider 0xffffffff / count, slop the furthest any member is from
        // there; Crash.pak: 4219024 and 22 for its 1018 files)
        std::uint32_t hash_divider(std::size_t n) { return n ? static_cast<std::uint32_t>(0xffffffffu / n) : 0; }
        std::uint32_t hash_slop(const std::vector<std::uint32_t> &hashes, std::uint32_t divider)
        {
            std::int64_t most = 0;
            for (std::size_t i = 0; divider && i < hashes.size(); ++i)
            {
                const auto off = static_cast<std::int64_t>(hashes[i] / divider) - static_cast<std::int64_t>(i);
                most = std::max<std::int64_t>(most, off < 0 ? -off : off);
            }
            return static_cast<std::uint32_t>(most);
        }
        // a member to add, stored whole
        struct NewMember
        {
            std::string name, prefix;
            Bytes data;
        };
        // a copy of the archive `a` at `target`: its members as they are, byte for byte (compressed ones with their block
        // tables), moved along by whole sectors if the table grew; the `replaced` ones (by index) and the `added` ones
        // stored whole after them, each on its own sectors; the table sorted by name hash, the names last; the header's
        // count, table size, lookup numbers and names' place made again. Each member's number (its record's second word,
        // above its lowest byte; different for each, Crash.pak: 0 to 1017 and one 0xffffff) is kept, and an added one gets
        // the next free one. `notes` gets what was found
        void write_archive(const std::filesystem::path &target, Archive &a, const std::map<unsigned, Bytes> &replaced, const std::vector<NewMember> &added,
                           std::vector<std::string> &notes)
        {
            const std::uint32_t n0 = a.count;
            const auto table0 = a.read(56, a.toc);
            const std::uint64_t blocks = a.toc - n0 * 20ull;
            std::vector<std::uint32_t> hashes0(n0);
            for (std::uint32_t i = 0; i < n0; ++i)
                hashes0[i] = word(table0, i * 4ull);
            const auto divider0 = word(a.header, 20), slop0 = word(a.header, 24);
            {
                std::ostringstream line;
                line << "Roo Nitro: Roo's archive: " << n0 << " files, lookup numbers " << divider0 << " and " << slop0 << " (made again here: " << hash_divider(n0) << " and "
                     << hash_slop(hashes0, divider0) << "), flags " << word(a.header, 52) << ", blocks " << word(a.header, 28) << "/" << word(a.header, 32) << "/" << word(a.header, 36);
                notes.push_back(line.str());
            }
            // (the game's lookup numbers worked out as it does, or the copy isn't made)
            if (divider0 != hash_divider(n0) || slop0 != hash_slop(hashes0, divider0))
                throw std::runtime_error("Roo's archive's lookup numbers aren't what the client works them out to be, so no copy was made (send the log)");
            struct Entry
            {
                std::uint32_t hash = 0, second = 0, length = 0, command = 0;
                std::uint64_t at = 0;
                std::string prefix, name;
                const Bytes *whole = nullptr;
            };
            std::vector<Entry> entries;
            std::set<std::uint32_t> numbers;
            std::uint64_t first = ~std::uint64_t{0};
            const auto names_at0 = get<std::uint64_t>(a.header, 40);
            for (std::uint32_t i = 0; i < n0; ++i)
            {
                Entry e;
                const auto r = n0 * 4ull + i * 16ull;
                e.hash = hashes0[i], e.second = word(table0, r + 4), e.length = word(table0, r + 8), e.command = word(table0, r + 12);
                e.at = word(table0, r) + ((static_cast<std::uint64_t>(e.second) & 1) << 32);
                e.prefix = a.prefix(i), e.name = a.name(i);
                require(e.hash == fnv1a_lower(e.name) && e.at < names_at0 && (i == 0 || hashes0[i - 1] < e.hash));
                numbers.insert(e.second >> 8);
                if (const auto it = replaced.find(i); it != replaced.end())
                    e.whole = &it->second, e.length = static_cast<std::uint32_t>(it->second.size()), e.command = 0xffffffffu;
                else
                    first = std::min(first, e.at);
                entries.push_back(std::move(e));
            }
            require(first != ~std::uint64_t{0} && first % 0x800 == 0 && first >= 56ull + a.toc);
            std::uint32_t number = n0;
            for (const auto &m : added)
            {
                Entry e;
                e.hash = fnv1a_lower(m.name), e.prefix = m.prefix, e.name = m.name, e.whole = &m.data;
                e.length = static_cast<std::uint32_t>(m.data.size()), e.command = 0xffffffffu;
                while (numbers.count(number))
                    ++number;
                numbers.insert(number), e.second = number << 8;
                entries.push_back(std::move(e));
            }
            std::sort(entries.begin(), entries.end(), [](const Entry &x, const Entry &y) { return x.hash < y.hash; });
            for (std::size_t i = 1; i < entries.size(); ++i)
                require(entries[i - 1].hash != entries[i].hash);
            const auto n = static_cast<std::uint32_t>(entries.size());
            const auto toc = static_cast<std::uint32_t>(n * 20ull + blocks);
            const std::uint64_t start = std::max<std::uint64_t>(first, (56ull + toc + 0x7ff) & ~std::uint64_t{0x7ff}), delta = start - first;
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            require(static_cast<bool>(out));
            {
                const Bytes head(static_cast<std::size_t>(start));
                out.write(reinterpret_cast<const char *>(head.data()), static_cast<std::streamsize>(head.size()));
            }
            // the members as they are, 8 MB at a time
            std::uint64_t at = start;
            for (std::uint64_t p = first; p < names_at0;)
            {
                const auto chunk = std::min<std::uint64_t>(8ull << 20, names_at0 - p);
                const auto bytes = a.read(p, static_cast<std::size_t>(chunk));
                out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                require(static_cast<bool>(out));
                p += chunk, at += chunk;
            }
            // then the ones stored whole
            const Bytes zeros(0x800);
            const auto pad = [&] {
                const auto padded = (at + 0x7ff) & ~std::uint64_t{0x7ff};
                out.write(reinterpret_cast<const char *>(zeros.data()), static_cast<std::streamsize>(padded - at));
                at = padded;
            };
            for (auto &e : entries)
            {
                if (!e.whole)
                {
                    e.at += delta;
                    continue;
                }
                pad();
                e.at = at;
                out.write(reinterpret_cast<const char *>(e.whole->data()), static_cast<std::streamsize>(e.whole->size()));
                at += e.whole->size();
            }
            // the names: an offset each, then "prefix\0name\0" each
            pad();
            const auto names_at = at;
            Bytes names(n * 4ull);
            std::vector<std::uint32_t> hashes(n);
            for (std::uint32_t i = 0; i < n; ++i)
            {
                hashes[i] = entries[i].hash;
                put32(names, i * 4ull, static_cast<std::uint32_t>(names.size()));
                names.insert(names.end(), entries[i].prefix.begin(), entries[i].prefix.end());
                names.push_back(0);
                names.insert(names.end(), entries[i].name.begin(), entries[i].name.end());
                names.push_back(0);
            }
            out.write(reinterpret_cast<const char *>(names.data()), static_cast<std::streamsize>(names.size()));
            // the header and table (the block tables as they were: the compressed members' records still point into them)
            Bytes head(a.header.begin(), a.header.begin() + 56);
            const auto divider = hash_divider(n), slop = hash_slop(hashes, divider);
            put32(head, 8, toc), put32(head, 12, n), put32(head, 20, divider), put32(head, 24, slop);
            put64(head, 40, names_at), put32(head, 48, static_cast<std::uint32_t>(names.size()));
            Bytes table(toc);
            for (std::uint32_t i = 0; i < n; ++i)
            {
                const auto &e = entries[i];
                const auto r = n * 4ull + i * 16ull;
                put32(table, i * 4ull, e.hash);
                put32(table, r, static_cast<std::uint32_t>(e.at)), put32(table, r + 4, (e.second & ~1u) | static_cast<std::uint32_t>((e.at >> 32) & 1));
                put32(table, r + 8, e.length), put32(table, r + 12, e.command);
            }
            std::copy(table0.begin() + static_cast<std::ptrdiff_t>(n0 * 20ull), table0.end(), table.begin() + static_cast<std::ptrdiff_t>(n * 20ull));
            out.seekp(0);
            out.write(reinterpret_cast<const char *>(head.data()), 56);
            out.write(reinterpret_cast<const char *>(table.data()), static_cast<std::streamsize>(table.size()));
            out.flush();
            require(static_cast<bool>(out));
            notes.push_back("Roo Nitro: the copy: " + std::to_string(n) + " files, lookup numbers " + std::to_string(divider) + " and " + std::to_string(slop) + ", his files moved along " +
                            std::to_string(delta) + " bytes, " + std::to_string((names_at + names.size()) / (1024 * 1024)) + " MB");
        }
    }
    std::vector<std::string> roo_nitro_archive(const std::filesystem::path &folder, const std::filesystem::path &original, std::string &built, int mode)
    {
        std::vector<std::string> lines;
        std::filesystem::path beach;
        for (const auto &path : archives())
            if (lower(path.filename().string()) == "l101_nsanitybeach.pak")
                beach = path;
        require(!beach.empty() && std::filesystem::exists(original));
        Archive roo(original), source(beach);
        std::map<std::string, unsigned> in_roo, in_beach;
        for (unsigned i = 0; i < roo.count; ++i)
            in_roo.emplace(lower(roo.name(i)), i);
        for (unsigned i = 0; i < source.count; ++i)
            in_beach.emplace(lower(source.name(i)), i);
        // Roo's and the beach's package lists
        const std::string roo_pkg = "packages/generated/maps/crash1/bosses/b102_ripperroo/b102_ripperroo_pkg.igz";
        const std::string beach_pkg = "packages/generated/maps/crash1/l101_nsanitybeach/l101_nsanitybeach_pkg.igz";
        const std::string crates = "maps/crash1/l101_nsanitybeach/l101_nsanitybeach_crates.igz";
        require(in_roo.count(roo_pkg) && in_beach.count(beach_pkg) && in_beach.count(crates));
        // (only ever made from the game's own: a copy already has the beach's crate layer)
        if (in_roo.count(crates))
            throw std::runtime_error("this B102_RipperRoo.pak is already a patched copy, not the game's own. Restart the game once so the loader sorts it out, or "
                                     "Steam's verify of the game files puts the game's own back");
        const auto roo_list = package_read(roo.member(in_roo[roo_pkg]));
        const auto beach_list = package_read(source.member(in_beach[beach_pkg]));
        std::set<std::string> roo_files;
        for (const auto &[kind, file] : roo_list.pairs)
            roo_files.insert(lower(roo_list.strings[file]));
        std::map<std::string, std::string> beach_kind;
        for (const auto &[kind, file] : beach_list.pairs)
            beach_kind.emplace(lower(beach_list.strings[file]), beach_list.strings[kind]);
        // the crate layer and everything it needs that Roo's archive lacks. (V1.4.10, Eric's V1.4.9 run: "boss doesnt load.
        // it just stays as a black screen after loading": test modes. 1: added to the archive only, his package list as it
        // was (the copy itself); 2: the crate layer a plain file, with its language files (the beach lists them: the game
        // may look for them with the layer); 3: a map layer, with its language files and the beach's camera layer (its
        // checkpoints point to a camera in it) as a plain file)
        require(mode >= 1 && mode <= 3);
        const std::string camera = "maps/crash1/l101_nsanitybeach/l101_nsanitybeach_camera.igz";
        std::vector<std::string> add, queue{crates};
        if (mode >= 2)
            for (const char *language : {"de", "en", "es", "fr", "it", "ja"})
                queue.push_back("maps/crash1/l101_nsanitybeach/l101_nsanitybeach_crates_" + std::string(language) + ".lng");
        if (mode == 3)
            queue.push_back(camera);
        std::set<std::string> seen(queue.begin(), queue.end());
        unsigned missing = 0;
        std::string missing_names;
        while (!queue.empty())
        {
            const auto name = queue.back();
            queue.pop_back();
            if (in_roo.count(name))
                continue;
            const auto it = in_beach.find(name);
            if (it == in_beach.end())
            {
                ++missing, missing_names += (missing_names.empty() ? "" : ", ") + name;
                continue;
            }
            add.push_back(name);
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".igz") == 0)
                for (const auto &dep : igz_dependencies(source.member(it->second)))
                    if (seen.insert(dep).second)
                        queue.push_back(dep);
        }
        // the package list: the crate layer (3: as the beach lists it, a map layer, so its things are made, its spawners too:
        // Roo's arena has none to make a Nitro with; 2: a plain file), the camera layer a plain file, the others as the
        // beach lists them, when it does; 1: as it was
        const auto crates_kind = beach_kind.count(crates) ? beach_kind[crates] : std::string("igx_entities");
        std::vector<std::pair<std::string, std::string>> more{{mode == 3 ? crates_kind : std::string("igx_file"), crates}};
        for (const auto &name : add)
            if (name != crates && !roo_files.count(name))
            {
                if (name == camera)
                    more.emplace_back("igx_file", name);
                else if (const auto it = beach_kind.find(name); it != beach_kind.end())
                    more.emplace_back(it->second, name);
            }
        auto pkg = mode == 1 ? roo.member(in_roo[roo_pkg]) : package_add(roo_list, more);
        if (mode == 1)
            more.clear();
        // (read back)
        {
            const auto check = package_read(pkg);
            require(check.pairs.size() == roo_list.pairs.size() + more.size());
            require(more.empty() || lower(check.strings[check.pairs.back().second]) == lower(more.back().second));
        }
        // Roo's archive with his package list replaced and the beach's files added
        std::map<unsigned, Bytes> replaced;
        if (mode != 1)
            replaced.emplace(in_roo[roo_pkg], pkg);
        std::vector<NewMember> members;
        std::uint64_t added_bytes = 0;
        for (const auto &name : add)
        {
            const auto i = in_beach[name];
            members.push_back({source.name(i), source.prefix(i), source.member(i)});
            added_bytes += members.back().data.size();
        }
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        const auto target = folder / original.filename();
        auto temporary = target;
        temporary += L".tmp";
        write_archive(temporary, roo, replaced, members, lines);
        // read back: every name found by its hash, the lookup numbers right, his package list and the beach's files as
        // made, and every one of his files up to 1 MB the same as in his own archive (the bigger ones by their records)
        unsigned compared = 0;
        {
            Archive check(temporary);
            require(check.count == roo.count + members.size());
            std::vector<std::uint32_t> hashes(check.count);
            std::map<std::string, unsigned> names;
            for (unsigned i = 0; i < check.count; ++i)
            {
                const auto name = check.name(i);
                hashes[i] = word(check.read(56ull + i * 4ull, 4), 0);
                require(hashes[i] == fnv1a_lower(name) && (i == 0 || hashes[i - 1] < hashes[i]));
                names.emplace(lower(name), i);
            }
            require(word(check.header, 20) == hash_divider(check.count) && word(check.header, 24) == hash_slop(hashes, word(check.header, 20)));
            require(names.count(roo_pkg) && check.member(names[roo_pkg]) == pkg);
            for (const auto &m : members)
                require(names.count(lower(m.name)) && check.member(names[lower(m.name)]) == m.data);
            for (unsigned j = 0; j < roo.count; ++j)
            {
                const auto name = lower(roo.name(j));
                require(names.count(name));
                if (name == roo_pkg)
                    continue;
                const auto mine = roo.record(j), theirs = check.record(names[name]);
                require(word(mine, 8) == word(theirs, 8) && word(mine, 12) == word(theirs, 12) && (word(mine, 4) & ~1u) == (word(theirs, 4) & ~1u));
                if (roo.length(j) <= 1024 * 1024)
                    require(check.member(names[name]) == roo.member(j)), ++compared;
            }
        }
        // (on disk before it's named: the loader goes by its size and write time)
        if (const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr); file != INVALID_HANDLE_VALUE)
            FlushFileBuffers(file), CloseHandle(file);
        if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("couldn't write " + target.string() + " (Windows error " + std::to_string(GetLastError()) + ")");
        built = target.string();
        std::ostringstream line;
        line << "Roo Nitro: made " << target.string() << ": Roo's " << roo.count << " files as they were (" << compared << " read back the same) and " << add.size()
             << " from N. Sanity Beach (" << added_bytes / 1024 << " KB: its crate layer" << (mode == 1 ? std::string() : ", as a " + more.front().first)
             << (mode == 3 ? ", its camera layer" : "") << " and what they need), " << more.size() << " added to his package list (test " << mode << ")" << (missing ? "; " + std::to_string(missing) + " needed files found nowhere: " + missing_names : std::string());
        lines.push_back(line.str());
        std::string listed;
        for (std::size_t k = 0; k < add.size() && k < 80; ++k)
            listed += (k ? ", " : "") + add[k];
        lines.push_back("Roo Nitro: added: " + listed);
        return lines;
    }
    // /roo_nitro (Testing): the loader's plan for Roo's archive (see dinput_proxy.cpp, level patches). On: the copy made from
    // the game's own (in the archives folder, or kept aside in patches\original while a copy is in place) and its line in
    // the plan; off: its line taken out. Either way it happens at the next game start
    namespace
    {
        bool file_stamp(const std::filesystem::path &path, unsigned long long &size, unsigned long long &written)
        {
            WIN32_FILE_ATTRIBUTE_DATA data{};
            if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                return false;
            size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            written = (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
            return true;
        }
        std::vector<std::string> bar_fields(const std::string &line)
        {
            std::vector<std::string> parts;
            std::size_t from = 0;
            for (auto bar = line.find('|'); bar != std::string::npos; bar = line.find('|', from))
                parts.push_back(line.substr(from, bar - from)), from = bar + 1;
            parts.push_back(line.substr(from));
            return parts;
        }
    }
    std::vector<std::string> roo_nitro_set(int mode, const std::filesystem::path &state_folder)
    {
        const bool on = mode > 0;
        const auto root = game_archives();
        const auto patches = root.parent_path() / L"CrashNSTAP" / L"patches";
        std::filesystem::path game;
        for (const auto &path : archives())
            if (lower(path.filename().string()) == "b102_ripperroo.pak")
                game = path;
        if (game.empty())
            return {"Roo Nitro: Ripper Roo's archive (B102_RipperRoo.pak) isn't in " + root.string()};
        const auto relative = std::filesystem::relative(game, root);
        const auto key = lower(relative.string());
        const auto plan_file = state_folder / "level_patches.txt";
        // the plan's other lines, as they are
        std::vector<std::string> others;
        {
            std::ifstream in(plan_file);
            std::string line;
            while (std::getline(in, line))
                if (const auto f = bar_fields(line); f.size() == 5 && lower(f[0]) != key)
                    others.push_back(line);
        }
        const auto write_plan = [&](const std::string &mine) {
            std::error_code error;
            std::filesystem::create_directories(state_folder, error);
            auto temporary = plan_file;
            temporary += ".tmp";
            {
                std::ofstream out(temporary, std::ios::trunc | std::ios::binary);
                for (const auto &line : others)
                    out << line << '\n';
                if (!mine.empty())
                    out << mine << '\n';
            }
            if (!MoveFileExW(temporary.c_str(), plan_file.c_str(), MOVEFILE_REPLACE_EXISTING))
                throw std::runtime_error("couldn't write " + plan_file.string());
        };
        if (!on)
        {
            write_plan({});
            return {"Roo Nitro: off. At the next game start his own archive is put back."};
        }
        // the game's own: in the archives folder, or kept aside while a copy is in place
        unsigned long long size = 0, written = 0;
        require(file_stamp(game, size, written));
        auto original = game;
        unsigned long long source_size = size, source_written = written;
        {
            std::ifstream in(patches / L"installed.txt");
            std::string line;
            while (std::getline(in, line))
                if (const auto f = bar_fields(line); f.size() == 5 && lower(f[0]) == key && std::strtoull(f[1].c_str(), nullptr, 10) == size &&
                                                     std::strtoull(f[2].c_str(), nullptr, 10) == written)
                {
                    original = patches / L"original" / relative;
                    source_size = std::strtoull(f[3].c_str(), nullptr, 10), source_written = std::strtoull(f[4].c_str(), nullptr, 10);
                    unsigned long long kept_size = 0, kept_written = 0;
                    if (!file_stamp(original, kept_size, kept_written) || kept_size != source_size || kept_written != source_written)
                        return {"Roo Nitro: the game's own B102_RipperRoo.pak isn't where the client kept it (" + original.string() +
                                "). Steam's verify of the game files puts it back; then /roo_nitro on again."};
                }
        }
        std::string built;
        auto lines = roo_nitro_archive(patches / relative.parent_path(), original, built, mode);
        unsigned long long patch_size = 0, patch_written = 0;
        require(file_stamp(built, patch_size, patch_written));
        write_plan(relative.string() + "|" + std::to_string(source_size) + "|" + std::to_string(source_written) + "|" + std::to_string(patch_size) + "|" +
                   std::to_string(patch_written));
        std::ofstream(state_folder / "roo_nitro_test.txt", std::ios::trunc) << mode << '\n';
        lines.push_back("Roo Nitro: test " + std::to_string(mode) + " on. Restart the game: as it starts, his arena's archive is swapped for this copy (/roo_nitro off and a restart put his own back).");
        return lines;
    }
    // what the loader did with level patches as the game started (CRASHNSTAP_LEVEL_PATCHES: "in place|put back|skipped|why")
    std::string level_patch_status()
    {
        char text[1024]{};
        const auto count = GetEnvironmentVariableA("CRASHNSTAP_LEVEL_PATCHES", text, sizeof(text));
        if (!count || count >= sizeof(text))
            return {};
        const auto f = bar_fields(text);
        if (f.size() < 4)
            return {};
        std::string line;
        if (std::strtoul(f[0].c_str(), nullptr, 10))
        {
            // (which test: as /roo_nitro last made it)
            int mode = 0;
            char local[1024]{};
            if (const auto n = GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof(local)); n && n < sizeof(local))
                std::ifstream(std::filesystem::path(local) / "CrashNSTAP" / "state" / "roo_nitro_test.txt") >> mode;
            line = "Roo Nitro: his arena's archive has the Nitro's files this session" + (mode ? " (test " + std::to_string(mode) + ")" : std::string()) +
                   ". If his fight doesn't load: /roo_nitro off from the hub and a restart put his own back.";
        }
        if (std::strtoul(f[1].c_str(), nullptr, 10))
            line += std::string(line.empty() ? "" : " ") + "Roo Nitro: his own archive is back.";
        if (std::strtoul(f[2].c_str(), nullptr, 10))
            line += std::string(line.empty() ? "" : " ") + "Roo Nitro: couldn't swap his archive (" + f[3] + ").";
        return line;
    }
    bool level_patch_in_place()
    {
        // (set by the loader before the game started; the same all session)
        static const bool in_place = [] {
            char text[256]{};
            const auto count = GetEnvironmentVariableA("CRASHNSTAP_LEVEL_PATCHES", text, sizeof(text));
            return count && count < sizeof(text) && std::strtoul(text, nullptr, 10) > 0;
        }();
        return in_place;
    }
    std::vector<std::string> archive_census()
    {
        std::vector<std::string> lines;
        const auto root = game_archives();
        const auto paks = archives();
        std::string names;
        for (std::size_t i = 0; i < paks.size() && i < 12; ++i)
        {
            std::error_code error;
            auto relative = std::filesystem::relative(paks[i], root, error);
            names += (i ? ", " : "") + (error ? paks[i].filename() : relative).generic_string();
        }
        lines.push_back("Game archives: " + std::to_string(paks.size()) + " .pak files" + (names.empty() ? "" : ": " + names + (paks.size() > 12 ? ", ..." : "")));
        std::map<std::string, unsigned> kinds, per_archive;
        std::vector<std::string> examples;
        unsigned sounds = 0, unreadable = 0;
        for (const auto &path : paks)
            try
            {
                Archive a(path);
                for (unsigned i = 0; i < a.count; ++i)
                {
                    const auto name = a.name(i);
                    if (!has_word(name, {"sound", "audio", "sfx"}) && !sound_file(name))
                        continue;
                    ++sounds, ++kinds[extension(name)], ++per_archive[path.filename().string()];
                    if (examples.size() < 3 && sound_file(name))
                        examples.push_back(path.filename().string() + ": " + name + " (" + std::to_string(a.length(i)) + " bytes)");
                }
            }
            catch (const std::exception &)
            {
                ++unreadable;
            }
        std::string summary;
        for (const auto &[kind, count] : kinds)
            summary += (summary.empty() ? "" : ", ") + kind + " " + std::to_string(count);
        std::vector<std::pair<unsigned, std::string>> busiest;
        for (const auto &[pak, count] : per_archive)
            busiest.emplace_back(count, pak);
        std::sort(busiest.rbegin(), busiest.rend());
        std::string where;
        for (std::size_t i = 0; i < busiest.size() && i < 6; ++i)
            where += (i ? ", " : "") + busiest[i].second + " (" + std::to_string(busiest[i].first) + ")";
        lines.push_back("Sound files in the archives: " + std::to_string(sounds) + (summary.empty() ? "" : " (" + summary + ")") +
                        (where.empty() ? "" : "; most in " + where) + (unreadable ? "; " + std::to_string(unreadable) + " archives unreadable" : ""));
        for (const auto &example : examples)
            lines.push_back("Sound file: " + example);
        if (!sounds)
        {
            // maybe the sounds sit next to the archives instead
            std::error_code error;
            std::string top;
            unsigned shown = 0;
            for (std::filesystem::directory_iterator it(root.parent_path(), error), end; !error && it != end && shown < 20; it.increment(error), ++shown)
                top += (shown ? ", " : "") + it->path().filename().string() + (it->is_directory(error) ? "/" : "");
            lines.push_back("Game folder: " + top);
        }
        return lines;
    }
    std::vector<std::string> sound_search_report(const std::string &text, bool everywhere, bool choose, const std::string &prefer,
                                                 const std::filesystem::path &remember)
    {
        std::vector<std::string> lines;
        const auto root = game_archives();
        std::error_code error;
        if (root.empty() || !std::filesystem::is_directory(root, error))
            return {"Sound search: the game's archives folder was not found next to its .exe"};
        SoundScan scan;
        const auto sounds = find_game_sounds(text, everywhere, scan, 80);
        std::string formats, codecs;
        unsigned listed = 0;
        for (const auto &[format, count] : scan.formats)
            if (++listed <= 10)
                formats += (formats.empty() ? "" : ", ") + format + " " + std::to_string(count);
        for (const auto &[codec, count] : scan.codecs)
            codecs += (codecs.empty() ? "" : ", ") + codec + " " + std::to_string(count);
        lines.push_back("Sound search \"" + text + "\": " + std::to_string(sounds.size()) + " sounds in " + std::to_string(scan.files) + " sound files" +
                        (everywhere ? " (every sound file)" : " (sound files named for it; add \"all\" to read every sound file)") +
                        (formats.empty() ? "" : ". Files: " + formats) + (codecs.empty() ? "" : ". Codecs: " + codecs) +
                        ". Named samples: " + std::to_string(scan.named) + ", unnamed: " + std::to_string(scan.unnamed));
        for (const auto &note : scan.notes)
            lines.push_back("Sound file without a sound bank: " + note);
        for (const auto &s : sounds)
            lines.push_back("Game sound: " + describe(s));
        if (!choose)
            return lines;
        const auto pick = countdown_choice(sounds, prefer);
        if (!pick)
        {
            lines.push_back("Nitro Trap countdown: no " + (prefer.empty() ? std::string("TNT countdown sound") : "sound matching \"" + prefer + "\"") +
                            " found; it keeps the client's tick");
            return lines;
        }
        lines.push_back(use_countdown(*pick, remember));
        return lines;
    }
    std::vector<std::string> startup_sound_search(const std::filesystem::path &remember)
    {
        // a background search: below the game's threads, so loading and playing don't wait on it
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
        std::vector<std::string> lines;
        // a sound found (or picked with /nitro_sound) before is loaded straight away
        if (std::ifstream in{remember}; in)
        {
            std::string pak, member, index, name;
            if (std::getline(in, pak) && std::getline(in, member) && std::getline(in, index) && std::getline(in, name))
                try
                {
                    Bytes data;
                    for (const auto &s : reread(pak, member, data))
                        if (std::to_string(s.index) == index && label(s) == name)
                            return {use_countdown(s, remember, true)};
                }
                catch (const std::exception &)
                {
                }
        }
        lines = archive_census();
        auto found = sound_search_report("tnt", false, true, {}, remember);
        lines.insert(lines.end(), found.begin(), found.end());
        // not found under "tnt": what other countdown-like sounds are called, to pick one with /nitro_sound
        if (countdown_sound_name().empty())
            for (const char *other : {"countdown", "fuse"})
            {
                auto more = sound_search_report(other, false, false);
                lines.insert(lines.end(), more.begin(), more.end());
            }
        return lines;
    }

    namespace
    {
        unsigned member_index(Archive &archive, const std::string &name, const std::filesystem::path &pak)
        {
            const auto want = lower(name);
            for (unsigned i = 0; i < archive.count; ++i)
                if (lower(archive.name(i)) == want)
                    return i;
            throw std::runtime_error(name + " isn't in " + pak.filename().string());
        }
    }
    std::vector<ArchiveCopies> find_archive_copies(const std::vector<std::string> &names)
    {
        std::vector<std::string> wanted;
        for (const auto &n : names)
            wanted.push_back(lower(n));
        std::vector<ArchiveCopies> result;
        for (const auto &path : archives())
            try
            {
                Archive a(path);
                ArchiveCopies copies{path, a.size, {}};
                for (unsigned i = 0; i < a.count; ++i)
                {
                    const auto name = lower(a.name(i));
                    if (std::find(wanted.begin(), wanted.end(), name) == wanted.end())
                        continue;
                    MemberRecord m;
                    m.name = name, m.at = 56ull + a.count * 4ull + i * 16ull;
                    const auto bytes = a.read(m.at, 16);
                    std::copy(bytes.begin(), bytes.end(), m.record.begin());
                    copies.members.push_back(std::move(m));
                }
                if (!copies.members.empty())
                    result.push_back(std::move(copies));
            }
            catch (const std::exception &)
            {
            }
        return result;
    }
    std::vector<unsigned char> read_archive_member(const std::filesystem::path &pak, const std::string &name)
    {
        Archive archive(pak);
        return archive.member(member_index(archive, name, pak));
    }
    void rewrite_archive(const std::filesystem::path &source, const std::filesystem::path &target,
                         const std::vector<std::pair<std::string, std::vector<unsigned char>>> &replacements)
    {
        std::vector<unsigned> indices;
        std::uint64_t original_size = 0;
        unsigned count = 0;
        {
            Archive archive(source);
            count = archive.count, original_size = archive.size;
            for (const auto &[name, bytes] : replacements)
            {
                const auto index = member_index(archive, name, source);
                if (archive.length(index) != bytes.size())
                    throw std::runtime_error(name + " would change size (" + std::to_string(archive.length(index)) + " to " + std::to_string(bytes.size()) +
                                             " bytes)");
                indices.push_back(index);
            }
        }
        auto temporary = target;
        temporary += L".tmp";
        std::error_code error;
        std::filesystem::create_directories(target.parent_path(), error);
        if (!CopyFileW(source.c_str(), temporary.c_str(), FALSE))
            throw std::runtime_error("couldn't copy " + source.filename().string() + " (Windows error " + std::to_string(GetLastError()) + ")");
        try
        {
            {
                std::fstream file(temporary, std::ios::binary | std::ios::in | std::ios::out);
                require(static_cast<bool>(file));
                const std::vector<char> zeros(0x800);
                std::uint64_t end = original_size;
                for (std::size_t r = 0; r < replacements.size(); ++r)
                {
                    // each copy starts on a 2 KiB sector, like the archive's own members
                    const auto at = (end + 0x7ff) & ~std::uint64_t{0x7ff};
                    file.seekp(static_cast<std::streamoff>(end));
                    file.write(zeros.data(), static_cast<std::streamsize>(at - end));
                    const auto &bytes = replacements[r].second;
                    file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                    end = at + bytes.size();
                    // its record: where it starts (the low 32 bits, and bit 32 in the second word's lowest bit), its
                    // length, and 0xffffffff for "stored whole"
                    const auto record = 56ull + count * 4ull + indices[r] * 16ull;
                    std::array<unsigned char, 16> fields{};
                    file.seekg(static_cast<std::streamoff>(record));
                    file.read(reinterpret_cast<char *>(fields.data()), 16);
                    std::uint32_t low = static_cast<std::uint32_t>(at), second = 0, stored_whole = 0xffffffff;
                    std::memcpy(&second, fields.data() + 4, 4);
                    second = (second & ~1u) | static_cast<std::uint32_t>((at >> 32) & 1);
                    std::memcpy(fields.data(), &low, 4), std::memcpy(fields.data() + 4, &second, 4), std::memcpy(fields.data() + 12, &stored_whole, 4);
                    file.seekp(static_cast<std::streamoff>(record));
                    file.write(reinterpret_cast<const char *>(fields.data()), 16);
                }
                const auto padded = (end + 0x7ff) & ~std::uint64_t{0x7ff};
                file.seekp(static_cast<std::streamoff>(end));
                file.write(zeros.data(), static_cast<std::streamsize>(padded - end));
                file.flush();
                require(file.good());
            }
            // read back the way the game would
            Archive check(temporary);
            for (std::size_t r = 0; r < replacements.size(); ++r)
                if (check.member(indices[r]) != replacements[r].second)
                    throw std::runtime_error("the new copy of " + replacements[r].first + " doesn't read back the same");
        }
        catch (...)
        {
            std::filesystem::remove(temporary, error);
            throw;
        }
        if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            const auto code = GetLastError();
            std::filesystem::remove(temporary, error);
            throw std::runtime_error("couldn't write " + target.string() + " (Windows error " + std::to_string(code) + ")");
        }
    }
}
