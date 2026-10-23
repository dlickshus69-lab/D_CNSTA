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
