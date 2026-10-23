#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>

// Crash colors: Crash's fur, chest (and jaw), pants, shoes and mohawk recolored in the game's own textures, keeping
// their shading. The textures are compressed in blocks of 4x4 pixels, each with two colors and a blend pattern; only
// the two colors of each block move, so the pattern (the fur strands, the folds, the shading) stays as it was.
// Portable: no Windows or game calls here, only bytes in and bytes out.
namespace crash_colors
{
    enum Part
    {
        Fur,
        Chest,
        Pants,
        Shoes,
        Hair,
        PartCount
    };
    // "fur", "chest", "pants", "shoes", "hair"
    const char *part_name(Part part);
    // the part by name (also "mohawk" for hair, "jaw" or "muzzle" for chest), or PartCount
    Part part_by_name(const std::string &name);

    struct Rgb
    {
        int r = 0, g = 0, b = 0;
        bool operator==(const Rgb &o) const { return r == o.r && g == o.g && b == o.b; }
    };
    // the color names, in the order the options list them
    const std::vector<std::pair<std::string, Rgb>> &palette();
    // a color name ("blue", "Light Blue", "light_blue") or "#RRGGBB"; nothing for "default" or anything unknown
    std::optional<Rgb> parse_color(const std::string &text);
    // the color's name as it should be written back ("light_blue", "#3366ff"), or "default"
    std::string color_text(const std::string &text);

    // the five parts' colors; an empty one stays as the game has it
    using Colors = std::array<std::optional<Rgb>, PartCount>;

    // the game's textures as stored in Crash.pak (whole .igz files, 1024x1024): CrashFur_C (BC1), CrashCloth_C (BC1),
    // CrashHair_C (BC3). Each returns the recolored file, the same size, or throws when the file isn't laid out as
    // expected (another game version or a skin mod's texture)
    std::vector<unsigned char> recolor_fur(const std::vector<unsigned char> &igz, const std::optional<Rgb> &fur, const std::optional<Rgb> &chest);
    std::vector<unsigned char> recolor_cloth(const std::vector<unsigned char> &igz, const std::optional<Rgb> &pants, const std::optional<Rgb> &shoes);
    std::vector<unsigned char> recolor_hair(const std::vector<unsigned char> &igz, const std::optional<Rgb> &hair);

    // changes whenever the recolor or a color changes (V1.2.44: highlights ease off; V1.2.45: softer white), so the
    // client makes each colored Crash again with the new one
    inline constexpr const char *recolor_version = "3";
    // the archive members holding those textures
    inline constexpr const char *fur_texture = "textures/ColorMap,textures@!actors!Playable!Crash!CrashFur_C`tga,111.igz";
    inline constexpr const char *cloth_texture = "textures/ColorMap,textures@!actors!Playable!Crash!CrashCloth_C`tga,111.igz";
    inline constexpr const char *hair_texture = "textures/ColorMap,textures@!actors!Playable!Crash!CrashHair_C`tga,111.igz";
}
