#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <vector>

namespace ingame
{
    struct FontGlyph
    {
        float left = 0, right = 0, step = 0, x = 0, y = 0, width = 0, height = 0, u = 0, v = 0, uu = 0, vv = 0;
    };
    struct GameFont
    {
        unsigned width = 0, height = 0;
        float line_height = 0;
        std::vector<std::uint32_t> pixels;
        std::map<wchar_t, FontGlyph> glyphs;
    };
    // load font from game assets
    GameFont load_game_font(const std::filesystem::path &archive);
    GameFont parse_game_font(const std::vector<unsigned char> &data);
}
