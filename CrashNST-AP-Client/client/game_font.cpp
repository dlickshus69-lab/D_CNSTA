#include "game_font.h"
#include <lzma.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace ingame
{
    namespace
    {
        constexpr std::size_t limit = 32 * 1024 * 1024;
        using Bytes = std::vector<unsigned char>;
        void require(bool ok)
        {
            if (!ok)
                throw std::runtime_error("Unsupported or incomplete NST Zoinks font asset");
        }
        template <class T>
        T value(const Bytes &bytes, std::size_t offset)
        {
            require(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset);
            T result;
            std::memcpy(&result, bytes.data() + offset, sizeof(result));
            return result;
        }
        std::uint32_t word(const Bytes &b, std::size_t p) { return value<std::uint32_t>(b, p); }
        std::string string_at(const Bytes &b, std::size_t p)
        {
            require(p < b.size());
            const auto end = std::find(b.begin() + p, b.end(), 0);
            require(end != b.end());
            return std::string(b.begin() + p, end);
        }
        struct Archive
        {
            std::ifstream stream;
            std::uint64_t size;
            explicit Archive(const std::filesystem::path &path) : stream(path, std::ios::binary)
            {
                if (!stream)
                    throw std::runtime_error("NST font archive is unavailable");
                stream.seekg(0, std::ios::end);
                const auto length = stream.tellg();
                require(length >= 64);
                size = length;
            }
            Bytes read(std::uint64_t p, std::size_t n)
            {
                require(n <= limit && p <= size && n <= size - p);
                Bytes b(n);
                stream.clear();
                stream.seekg(p);
                require(static_cast<bool>(stream.read(reinterpret_cast<char *>(b.data()), n)));
                return b;
            }
        };
        Bytes font_member(const std::filesystem::path &path)
        {
            // IGA v11: 56-byte header, per-file hashes/16-byte records, block indices, then the filename table
            Archive a(path);
            const auto h = a.read(0, 56);
            require(word(h, 0) == 0x1a414749 && word(h, 4) == 11 && word(h, 16) == 0x800);
            const auto toc = word(h, 8), count = word(h, 12), large = word(h, 28), medium = word(h, 32), small = word(h, 36);
            require(count > 0 && count <= 100000 && toc >= count * 20 && toc <= limit && 56ull + toc <= a.size);
            const auto names_start = value<std::uint64_t>(h, 40);
            require(names_start >= 56ull + toc);
            const auto names = a.read(names_start, word(h, 48));
            require(names.size() >= count * 4ull);
            unsigned found = count;
            for (unsigned i = 0; i < count; ++i)
            {
                auto p = word(names, i * 4);
                require(p >= count * 4);
                const auto build = string_at(names, p);
                p += build.size() + 1;
                if (string_at(names, p) == "fonts/zoinks.igz")
                {
                    require(found == count);
                    found = i;
                }
            }
            require(found != count);
            const auto info = a.read(56ull + count * 4ull + found * 16ull, 16);
            const auto source = word(info, 0) + ((static_cast<std::uint64_t>(word(info, 4)) & 1) << 32);
            const auto length = word(info, 8), command = word(info, 12);
            require(length > 0 && length <= limit);
            if (command == 0xffffffff)
                return a.read(source, length);
            // compression kind is the high nibble
            // the low 28 bits index size-dependent block-offset tables
            require(command >> 28 == 2);
            std::uint64_t table = 56ull + count * 20ull;
            unsigned elements = large, width = 4, flag = 0x80000000;
            if (length <= 0x7f * 0x800)
            {
                table += large * 4ull + medium * 2ull;
                elements = small;
                width = 1;
                flag = 0x80;
            }
            else if (length <= 0x7fff * 0x800)
            {
                table += large * 4ull;
                elements = medium;
                width = 2;
                flag = 0x8000;
            }
            const auto blocks = (length + 0x7fff) / 0x8000, first = command & 0xfffffff;
            require(first <= elements && blocks <= elements - first && table + (first + blocks) * static_cast<std::uint64_t>(width) <= 56ull + toc);
            const auto indices = a.read(table + first * static_cast<std::uint64_t>(width), blocks * width);
            Bytes result;
            result.reserve(length);
            for (unsigned i = 0; i < blocks; ++i)
            {
                unsigned entry = 0;
                std::memcpy(&entry, indices.data() + i * width, width);
                const auto p = source + (entry & (flag - 1)) * 0x800ull;
                // offsets use 2 KiB sectors. 
                // each block produces up to 32 KiB, with the index high bit marking LZMA
                const auto expected = std::min<unsigned>(0x8000, length - result.size());
                Bytes block;
                if (entry & flag)
                {
                    const auto header = a.read(p, 7);
                    const auto packed = value<std::uint16_t>(header, 0);
                    const auto props = header[2];
                    lzma_options_lzma options{};
                    options.lc = props % 9;
                    options.lp = props / 9 % 5;
                    options.pb = props / 45;
                    options.dict_size = std::max(4096u, word(header, 3));
                    require(packed && options.pb <= 4 && options.lc + options.lp <= 4 && options.dict_size <= limit);
                    options.ext_flags = LZMA_LZMA1EXT_ALLOW_EOPM;
                    lzma_set_ext_size(options, expected);
                    lzma_filter filters[] = {{LZMA_FILTER_LZMA1EXT, &options}, {LZMA_VLI_UNKNOWN, nullptr}};
                    const auto payload = a.read(p + 7, packed);
                    block.resize(expected);
                    std::size_t in = 0, out = 0;
                    require(lzma_raw_buffer_decode(filters, nullptr, payload.data(), &in, payload.size(), block.data(), &out, block.size()) == LZMA_OK && out == expected && in == payload.size());
                }
                else
                    block = a.read(p, expected);
                result.insert(result.end(), block.begin(), block.end());
            }
            return result;
        }
    }
    GameFont parse_game_font(const Bytes &b)
    {
        require(b.size() <= limit && word(b, 0) == 0x49475a01 && word(b, 4) == 10);
        std::vector<std::pair<unsigned, unsigned>> chunks;
        for (unsigned p = 0x14; p < 0x214; p += 16)
        {
            const auto start = word(b, p + 4), size = word(b, p + 8);
            if (!start)
                break;
            require(start <= b.size() && size <= b.size() - start);
            chunks.push_back({start, size});
        }
        require(chunks.size() >= 2);
        const auto absolute = [&](unsigned encoded, std::size_t length = 1)
        {
            // IGZ references encode a data-chunk index in the upper five bits and a chunk-relative offset below it
            const auto index = (encoded >> 27) + 1, relative = encoded & 0x7ffffff;
            require(index < chunks.size());
            const auto [start, size] = chunks[index];
            require(relative <= size && length <= size - relative);
            return start + relative;
        };
        struct Fixup
        {
            unsigned count;
            Bytes bytes;
        };
        std::map<std::string, Fixup> fixups;
        const auto [start, size] = chunks[0];
        for (std::size_t p = start; p + 16 <= start + size;)
        {
            if (!word(b, p))
                break;
            const auto count = word(b, p + 4), length = word(b, p + 8), header = word(b, p + 12);
            require(count <= 100000 && header >= 16 && length >= header && length <= start + size - p);
            const std::string tag(reinterpret_cast<const char *>(b.data() + p), 4);
            require(!fixups.count(tag));
            fixups.emplace(tag, Fixup{count, Bytes(b.begin() + p + header, b.begin() + p + length)});
            p += length;
        }
        require(fixups.count("TMET") && fixups.count("RVTB"));
        std::vector<std::string> types;
        const auto &t = fixups.at("TMET");
        std::size_t p = 0;
        for (unsigned i = 0; i < t.count; ++i)
        {
            const auto s = string_at(t.bytes, p);
            types.push_back(s);
            p += s.size() + 1;
            p += p & 1;
        }
        const auto &v = fixups.at("RVTB");
        // RVTB packs delta offsets as low-first nibbles: three value bits, a continuation bit, and four-byte units
        std::vector<unsigned> offsets;
        unsigned accumulated = 0, number = 0, shift = 0;
        for (const auto byte : v.bytes)
        {
            for (const auto nibble : {byte & 15, byte >> 4})
            {
                require(shift <= 28);
                const auto part = nibble & 7;
                require(part <= ((0xffffffffu - number) >> shift));
                number |= part << shift;
                if (nibble & 8)
                    shift += 3;
                else
                {
                    require(number <= (0xffffffffu - accumulated) / 4);
                    accumulated += number * 4;
                    offsets.push_back(absolute(accumulated, 16));
                    number = shift = 0;
                }
                if (offsets.size() == v.count)
                    break;
            }
            if (offsets.size() == v.count)
                break;
        }
        require(offsets.size() == v.count && !shift);
        const auto typed = [&](unsigned at, const char *expected)
        {const auto type=word(b,at);require(type<types.size() && types[type]==expected); };
        unsigned font = 0;
        for (const auto at : offsets)
        {
            const auto type = word(b, at);
            require(type < types.size());
            if (types[type] == "igBitmapFont")
            {
                require(!font);
                font = at;
            }
        }
        require(font);
        GameFont result;
        result.line_height = value<float>(b, font + 24);
        require(std::isfinite(result.line_height) && result.line_height >= 16 && result.line_height <= 256);
        const auto bind = absolute(word(b, font + 112), 40);
        // Follow igBitmapFont -> texture binding -> texture -> image rather than assuming atlas data follows the font
        typed(bind, "igTextureBindAttr2");
        const auto texture = absolute(word(b, bind + 24), 96);
        typed(texture, "igTextureAttr2");
        const auto image = absolute(word(b, texture + 56), 88);
        typed(image, "igImage2");
        result.width = value<std::uint16_t>(b, image + 24);
        result.height = value<std::uint16_t>(b, image + 26);
        require(result.width > 0 && result.width <= 4096 && result.height > 0 && result.height <= 4096 &&
                value<std::uint16_t>(b, image + 28) == 1 && value<std::uint16_t>(b, image + 30) == 1 && value<std::uint16_t>(b, image + 32) == 1);
        const auto bytes = word(b, image + 56);
        require(bytes == result.width * result.height * 4ull);
        const auto pixels = absolute(word(b, image + 64), bytes);
        result.pixels.resize(bytes / 4);
        std::memcpy(result.pixels.data(), b.data() + pixels, bytes);
        const auto metrics = absolute(word(b, font + 80), 40);
        typed(metrics, "igCharMetricsList");
        const auto count = word(b, metrics + 16);
        require(count > 0 && count <= 8192 && word(b, metrics + 24) >= count * 8);
        const auto list = absolute(word(b, metrics + 32), count * 8);
        for (unsigned i = 0; i < count; ++i)
        {
            const auto at = absolute(word(b, list + i * 8), 64);
            typed(at, "igCharMetrics");
            FontGlyph g;
            // igCharMetrics stores the character at +16 and eleven float metrics/UV values starting at +20
            float *fields[] = {&g.left, &g.right, &g.step, &g.x, &g.y, &g.width, &g.height, &g.u, &g.v, &g.uu, &g.vv};
            for (unsigned j = 0; j < 11; ++j)
            {
                *fields[j] = value<float>(b, at + 20 + j * 4);
                require(std::isfinite(*fields[j]));
            }
            require(std::abs(g.left) <= 256 && std::abs(g.right) <= 256 && std::abs(g.x) <= 256 && std::abs(g.y) <= 256 &&
                    g.step > 0 && g.step <= 256 && g.width > 0 && g.width <= 256 && g.height > 0 && g.height <= 256 &&
                    g.u >= 0 && g.v >= 0 && g.uu > g.u && g.vv > g.v && g.uu <= 1 && g.vv <= 1);
            require(result.glyphs.emplace(value<std::uint16_t>(b, at + 16), g).second);
        }
        require(result.glyphs.count(L'?') && result.glyphs.count(L' ') && result.glyphs.count(L'A') && result.glyphs.count(L'0'));
        return result;
    }
    GameFont load_game_font(const std::filesystem::path &archive) { return parse_game_font(font_member(archive)); }
}
