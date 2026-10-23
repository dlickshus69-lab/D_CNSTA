#include "crash_colors_core.h"
#include "crash_colors_masks.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace crash_colors
{
    namespace
    {
        // V1.2.43 research (the game's own textures): each .igz here is a 0x9c8-byte header, then the image and its
        // smaller copies (mipmaps) from 1024x1024 down to 4x4: BC1 (8 bytes a block) for the fur and clothes, BC3 (16
        // bytes, the color half after 8 bytes of transparency) for the hair
        constexpr std::size_t header_size = 0x9c8;
        constexpr std::size_t bc1_bytes = 699048, bc3_bytes = 1398096;

        struct Hsv
        {
            double h = 0, s = 0, v = 0;
        };
        // as Python's colorsys, which the prototype used
        Hsv to_hsv(double r, double g, double b)
        {
            const double maxc = std::max({r, g, b}), minc = std::min({r, g, b});
            if (maxc == minc)
                return {0, 0, maxc};
            const double s = (maxc - minc) / maxc;
            const double rc = (maxc - r) / (maxc - minc), gc = (maxc - g) / (maxc - minc), bc = (maxc - b) / (maxc - minc);
            double h = r == maxc ? bc - gc : g == maxc ? 2.0 + rc - bc : 4.0 + gc - rc;
            h = std::fmod(h / 6.0, 1.0);
            if (h < 0)
                h += 1.0;
            return {h, s, maxc};
        }
        void from_hsv(double h, double s, double v, double out[3])
        {
            if (s == 0.0)
            {
                out[0] = out[1] = out[2] = v;
                return;
            }
            const int i = static_cast<int>(h * 6.0) % 6;
            const double f = h * 6.0 - std::floor(h * 6.0);
            const double p = v * (1.0 - s), q = v * (1.0 - s * f), t = v * (1.0 - s * (1.0 - f));
            const double table[6][3] = {{v, t, p}, {q, v, p}, {p, v, t}, {p, q, v}, {t, p, v}, {v, p, q}};
            out[0] = table[i][0], out[1] = table[i][1], out[2] = table[i][2];
        }
        double wrap(double x)
        {
            x = std::fmod(x, 1.0);
            return x < 0 ? x + 1.0 : x;
        }
        // a 5:6:5 block color as 0-255 channels, and back
        void decode565(std::uint16_t c, double rgb[3])
        {
            rgb[0] = ((c >> 11) & 31) * 255 / 31;
            rgb[1] = ((c >> 5) & 63) * 255 / 63;
            rgb[2] = (c & 31) * 255 / 31;
        }
        std::uint16_t encode565(const double rgb[3])
        {
            unsigned v[3];
            for (int i = 0; i < 3; ++i)
                v[i] = static_cast<unsigned>(std::clamp(std::nearbyint(rgb[i]), 0.0, 255.0));
            return static_cast<std::uint16_t>((((v[0] * 31 + 127) / 255) << 11) | (((v[1] * 63 + 127) / 255) << 5) | ((v[2] * 31 + 127) / 255));
        }
        Hsv hsv_of(const double rgb[3]) { return to_hsv(rgb[0] / 255, rgb[1] / 255, rgb[2] / 255); }
        Hsv hsv_of(Rgb c) { return to_hsv(c.r / 255.0, c.g / 255.0, c.b / 255.0); }

        // the part's typical color in the game's texture (the median of its block colors) and the chosen color: a
        // block color that was as bright as the typical one becomes exactly the chosen color, a darker one as much
        // darker, and half of its hue difference stays, so the shading and the small color changes stay
        struct Target
        {
            Hsv reference, color;
        };
        // brightness: as much brighter or darker than the part's typical color as the game's was, but highlights ease
        // off toward 0.86 instead of clipping to full white (V1.2.43: white fur glowed under the hubs' lights; the
        // game's own orange fur is about 0.58 at its typical color)
        double brightness(double raw)
        {
            constexpr double knee = 0.70, top = 0.86;
            return raw <= knee ? raw : knee + (top - knee) * (1.0 - std::exp(-(raw - knee) / (top - knee)));
        }
        std::uint16_t colorize(std::uint16_t c, const Target &t)
        {
            double rgb[3];
            decode565(c, rgb);
            const auto [h, s, v] = hsv_of(rgb);
            const auto &[h0, s0, v0] = t.reference;
            const auto &[th, ts, tv] = t.color;
            const double nv = brightness(tv * v / std::max(v0, 0.05));
            const double ns = ts > 0.08 ? std::min(1.0, ts * (s / std::max(s0, 0.05))) : ts;
            const double dh = wrap(h - h0 + 0.5) - 0.5;
            double out[3];
            from_hsv(wrap(th + dh * 0.5), ns, nv, out);
            for (auto &x : out)
                x *= 255;
            return encode565(out);
        }
        Target target(Rgb reference, Rgb chosen) { return {hsv_of(reference), hsv_of(chosen)}; }

        // which target (if any) a block's color gets: (block index, the color as 0-255 channels, its hsv)
        using Choose = std::function<const Target *(std::size_t, const double *, const Hsv &)>;
        // recolors one mipmap level's blocks; `color_at` is where a block's two colors start (0 for BC1, 8 for BC3)
        void recolor_blocks(unsigned char *data, std::size_t blocks, std::size_t block_bytes, std::size_t color_at, const Choose &choose)
        {
            for (std::size_t k = 0; k < blocks; ++k)
            {
                auto *b = data + k * block_bytes + color_at;
                std::uint16_t c0, c1;
                std::uint32_t idx;
                std::memcpy(&c0, b, 2), std::memcpy(&c1, b + 2, 2), std::memcpy(&idx, b + 4, 4);
                std::uint16_t n[2] = {c0, c1};
                for (int e = 0; e < 2; ++e)
                {
                    double rgb[3];
                    decode565(n[e], rgb);
                    if (const auto *t = choose(k, rgb, hsv_of(rgb)))
                        n[e] = colorize(n[e], *t);
                }
                if (n[0] == c0 && n[1] == c1)
                    continue;
                // BC1 blocks have two kinds: 4 colors when the first is the larger number, else 3 colors and clear.
                // A block keeps its kind: if the new colors swap order, they swap places and the pattern follows.
                // BC3's color half is always 4-color.
                const bool four = c0 > c1 || block_bytes == 16;
                if (block_bytes == 8 && ((four && n[0] <= n[1]) || (!four && n[0] > n[1])))
                {
                    if (n[0] == n[1] && four)
                        idx = 0; // one color now
                    else
                    {
                        std::swap(n[0], n[1]);
                        if (four)
                            idx ^= 0x55555555u; // 0<->1, 2<->3
                        else
                            idx ^= ~((idx >> 1) & 0x55555555u) & 0x55555555u; // 0<->1; 2 and 3 (clear) stay
                    }
                }
                std::memcpy(b, &n[0], 2), std::memcpy(b + 2, &n[1], 2), std::memcpy(b + 4, &idx, 4);
            }
        }
        // the mipmap levels: (offset into the image data, blocks per row)
        std::vector<std::pair<std::size_t, std::size_t>> levels(std::size_t block_bytes)
        {
            std::vector<std::pair<std::size_t, std::size_t>> out;
            std::size_t at = 0;
            for (std::size_t size = 1024; size >= 4; size /= 2)
            {
                out.emplace_back(at, size / 4);
                at += (size / 4) * (size / 4) * block_bytes;
            }
            return out;
        }
        std::vector<unsigned char> checked(const std::vector<unsigned char> &igz, std::size_t data_bytes, const char *what)
        {
            std::uint16_t w = 0, h = 0;
            if (igz.size() == header_size + data_bytes)
                std::memcpy(&w, igz.data() + 0x950, 2), std::memcpy(&h, igz.data() + 0x952, 2);
            if (igz.size() != header_size + data_bytes || std::memcmp(igz.data(), "\x01ZGI", 4) != 0 || w != 1024 || h != 1024)
                throw std::runtime_error(std::string(what) + " isn't the game's own texture (another game version, or a skin mod's)");
            return igz;
        }
        // a block map (256x256, from crash_colors_masks.h) shrunk to a mipmap level: a block is in it when any of the
        // full-size blocks it covers is
        std::vector<bool> block_map(const std::uint16_t *runs, std::size_t run_count, std::size_t per_row, int grow = 0)
        {
            std::vector<bool> full(256 * 256);
            std::size_t at = 0;
            bool on = false;
            for (std::size_t i = 0; i < run_count; ++i, on = !on)
            {
                for (std::size_t j = 0; j < runs[i] && at < full.size(); ++j)
                    full[at++] = on;
            }
            // `grow` blocks around it too
            if (grow > 0)
            {
                auto grown = full;
                for (int y = 0; y < 256; ++y)
                    for (int x = 0; x < 256; ++x)
                        if (full[y * 256 + x])
                            for (int dy = -grow; dy <= grow; ++dy)
                                for (int dx = -grow; dx <= grow; ++dx)
                                    if (y + dy >= 0 && y + dy < 256 && x + dx >= 0 && x + dx < 256)
                                        grown[(y + dy) * 256 + x + dx] = true;
                full.swap(grown);
            }
            const auto f = 256 / per_row;
            std::vector<bool> out(per_row * per_row);
            for (std::size_t y = 0; y < 256; ++y)
                for (std::size_t x = 0; x < 256; ++x)
                    if (full[y * 256 + x])
                        out[(y / f) * per_row + x / f] = true;
            return out;
        }
        // the parts' typical colors in the game's textures
        constexpr Rgb fur_reference{148, 64, 32}, chest_reference{189, 121, 115}, pants_reference{8, 52, 197}, shoes_reference{82, 36, 57},
            hair_reference{98, 48, 41};
    }

    const char *part_name(Part part)
    {
        static const char *const names[]{"fur", "chest", "pants", "shoes", "hair"};
        return part < PartCount ? names[part] : "";
    }
    Part part_by_name(const std::string &name)
    {
        std::string key;
        for (const char c : name)
            key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (int p = 0; p < PartCount; ++p)
            if (key == part_name(static_cast<Part>(p)))
                return static_cast<Part>(p);
        if (key == "mohawk")
            return Hair;
        if (key == "jaw" || key == "muzzle" || key == "belly")
            return Chest;
        return PartCount;
    }
    const std::vector<std::pair<std::string, Rgb>> &palette()
    {
        static const std::vector<std::pair<std::string, Rgb>> colors{
            {"red", {184, 28, 28}},      {"dark_red", {110, 16, 16}},  {"orange", {210, 105, 30}},  {"yellow", {224, 187, 26}},
            {"gold", {199, 154, 34}},    {"lime", {120, 200, 30}},     {"green", {33, 154, 56}},    {"dark_green", {20, 80, 36}},
            {"teal", {18, 130, 130}},    {"cyan", {28, 180, 204}},     {"light_blue", {90, 162, 230}}, {"blue", {29, 88, 200}},
            {"navy", {26, 39, 102}},     {"purple", {106, 45, 182}},   {"violet", {150, 86, 208}},  {"pink", {224, 106, 162}},
            {"magenta", {192, 34, 138}}, {"brown", {98, 56, 28}},      {"tan", {196, 154, 100}},    {"white", {158, 158, 158}},
            {"light_gray", {130, 130, 130}}, {"gray", {98, 98, 98}}, {"black", {30, 30, 30}},
            // V1.2.45: "white" is close to Crash's own chest in brightness (which doesn't glow); this is V1.2.44's brighter
            // white, which can glow under strong lights (the Crash 3 hub's bridge)
            {"bright_white", {214, 214, 214}}};
        return colors;
    }
    std::string color_text(const std::string &text)
    {
        std::string key;
        for (const char c : text)
            if (!std::isspace(static_cast<unsigned char>(c)) || (!key.empty() && key.back() != '_'))
                key += std::isspace(static_cast<unsigned char>(c)) || c == '-' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        while (!key.empty() && key.back() == '_')
            key.pop_back();
        if (key == "grey")
            key = "gray";
        if (key == "light_grey")
            key = "light_gray";
        for (const auto &[name, rgb] : palette())
            if (key == name)
                return name;
        if (key.size() == 7 && key[0] == '#' && std::all_of(key.begin() + 1, key.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
            return key;
        return "default";
    }
    std::optional<Rgb> parse_color(const std::string &text)
    {
        const auto key = color_text(text);
        for (const auto &[name, rgb] : palette())
            if (key == name)
                return rgb;
        if (key.size() == 7 && key[0] == '#')
            return Rgb{std::stoi(key.substr(1, 2), nullptr, 16), std::stoi(key.substr(3, 2), nullptr, 16), std::stoi(key.substr(5, 2), nullptr, 16)};
        return std::nullopt;
    }

    std::vector<unsigned char> recolor_fur(const std::vector<unsigned char> &igz, const std::optional<Rgb> &fur, const std::optional<Rgb> &chest)
    {
        auto out = checked(igz, bc1_bytes, "CrashFur_C");
        const auto fur_t = fur ? std::optional<Target>(target(fur_reference, *fur)) : std::nullopt;
        const auto chest_t = chest ? std::optional<Target>(target(chest_reference, *chest)) : std::nullopt;
        for (const auto &[at, per_row] : levels(8))
        {
            const auto fur_map = block_map(fur_block_runs, std::size(fur_block_runs), per_row);
            const auto chest_map = block_map(chest_block_runs, std::size(chest_block_runs), per_row);
            // the jaw's edge and the lips around it (V1.2.45: the blue mod left the lips and the jaw's pinkish rim as
            // they were, so they stayed reddish by Crash's mouth): their colors are paler than the fur's strong orange
            const auto near_chest = block_map(chest_block_runs, std::size(chest_block_runs), per_row, 3);
            recolor_blocks(out.data() + header_size + at, per_row * per_row, 8, 0, [&](std::size_t k, const double *, const Hsv &c) -> const Target * {
                const bool warm = (c.h >= 0.9 || c.h <= 0.16) && c.s >= 0.2;
                if (!warm)
                    return nullptr;
                // the chest's and jaw's light, pinkish colors, also on the fur's edge
                if (chest_map[k] && (!fur_map[k] || c.v > 0.7 || (c.h >= 0.07 && c.h <= 0.16) || c.h >= 0.9))
                    return chest_t ? &*chest_t : nullptr;
                // next to them: the lips (dark, pinkish) and the jaw's rim, but not the fur
                if (near_chest[k] && c.v > 0.5 && (c.s < 0.6 || c.h >= 0.9 || c.h < 0.015))
                    return chest_t ? &*chest_t : nullptr;
                if (fur_map[k] && c.h >= 0.015 && c.h <= 0.14 && c.s >= 0.35)
                    return fur_t ? &*fur_t : nullptr;
                return nullptr;
            });
        }
        return out;
    }
    std::vector<unsigned char> recolor_cloth(const std::vector<unsigned char> &igz, const std::optional<Rgb> &pants, const std::optional<Rgb> &shoes)
    {
        auto out = checked(igz, bc1_bytes, "CrashCloth_C");
        const auto pants_t = pants ? std::optional<Target>(target(pants_reference, *pants)) : std::nullopt;
        const auto shoes_t = shoes ? std::optional<Target>(target(shoes_reference, *shoes)) : std::nullopt;
        for (const auto &[at, per_row] : levels(8))
        {
            auto *data = out.data() + header_size + at;
            const auto blocks = per_row * per_row;
            // the gloves' dark brown leather: its blocks keep their reds out of the shoes' color
            std::vector<bool> leather(blocks);
            for (std::size_t k = 0; k < blocks; ++k)
                for (int e = 0; e < 2; ++e)
                {
                    std::uint16_t c;
                    std::memcpy(&c, data + k * 8 + e * 2, 2);
                    double rgb[3];
                    decode565(c, rgb);
                    const auto hsv = hsv_of(rgb);
                    if (hsv.h >= 0.035 && hsv.h <= 0.12 && hsv.s >= 0.4 && hsv.v <= 0.42)
                        leather[k] = true;
                }
            recolor_blocks(data, blocks, 8, 0, [&](std::size_t k, const double *, const Hsv &c) -> const Target * {
                if (pants_t && c.h >= 0.5 && c.h <= 0.75 && c.s >= 0.3)
                    return &*pants_t;
                if (shoes_t && !leather[k] && (c.h >= 0.75 || c.h <= 0.03) && c.s >= 0.15)
                    return &*shoes_t;
                return nullptr;
            });
        }
        return out;
    }
    std::vector<unsigned char> recolor_hair(const std::vector<unsigned char> &igz, const std::optional<Rgb> &hair)
    {
        auto out = checked(igz, bc3_bytes, "CrashHair_C");
        if (!hair)
            return out;
        const auto hair_t = target(hair_reference, *hair);
        for (const auto &[at, per_row] : levels(16))
        {
            // the dark strip along the top (the eyebrows) stays
            const auto row = per_row, top = static_cast<std::size_t>(per_row * 0.19);
            recolor_blocks(out.data() + header_size + at, row * row, 16, 8, [&](std::size_t k, const double *, const Hsv &c) -> const Target * {
                if (k / row < top)
                    return nullptr;
                return (c.h >= 0.9 || c.h <= 0.12) && c.s >= 0.2 ? &hair_t : nullptr;
            });
        }
        return out;
    }
}
