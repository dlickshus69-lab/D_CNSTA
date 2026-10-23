#include "crash_colors.h"
#include "game_archive.h"
#include "skins.h"
#include "standalone.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace crash_colors
{
    namespace
    {
        std::filesystem::path state_file() { return standalone::state_directory() / "colors.txt"; }
        std::string trim(const std::string &text)
        {
            const auto first = text.find_first_not_of(" \t\r\n");
            const auto last = text.find_last_not_of(" \t\r\n");
            return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
        }
        void save(const Choice &choice)
        {
            std::error_code error;
            std::filesystem::create_directories(state_file().parent_path(), error);
            std::ofstream out(state_file(), std::ios::trunc);
            for (int p = 0; p < PartCount; ++p)
                out << part_name(static_cast<Part>(p)) << '=' << color_text(choice[p]) << '\n';
        }
        bool all_default(const Choice &choice)
        {
            return std::all_of(choice.begin(), choice.end(), [](const std::string &c) { return color_text(c) == "default"; });
        }
        // the skin's name for a combination: colors_ and 8 hex digits of its FNV-1a hash
        std::string skin_name(const Choice &choice)
        {
            // the recolor's version goes in too, so a client that colors differently makes the skin again
            std::uint32_t hash = 2166136261u;
            for (const char c : std::string(recolor_version) + ";")
                hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
            for (int p = 0; p < PartCount; ++p)
                for (const char c : std::string(part_name(static_cast<Part>(p))) + "=" + color_text(choice[p]) + ";")
                    hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
            char text[16];
            std::snprintf(text, sizeof(text), "%08x", hash);
            return std::string("colors_") + text;
        }
        bool colors_skin(const std::string &name) { return name.rfind("colors_", 0) == 0; }
        // makes the colored Crash.pak for the chosen colors (once per combination) and chooses it as the skin
        std::vector<std::string> prepare()
        {
            const auto choice = chosen();
            if (all_default(choice))
            {
                if (!colors_skin(skins::chosen()))
                    return {};
                skins::choose("default");
                return {skins::worn() == "default" ? "Crash colors: back to the normal colors." : "Crash colors: back to the normal colors. Restart the game to see them."};
            }
            const auto name = skin_name(choice);
            const auto folder = skins::folder();
            const auto pak = folder / name / "Crash.pak";
            std::error_code error;
            if (!std::filesystem::exists(pak, error))
            {
                // the game's own Crash.pak: the copy kept before the first skin went on, or the game's while it wears none
                auto source = folder / "default" / "Crash.pak";
                if (!std::filesystem::exists(source, error))
                {
                    const auto worn = skins::worn();
                    if (!worn.empty() && worn != "default")
                        return {"Crash colors: the game's own Crash.pak isn't saved (it wears " + skins::display(worn) +
                                "). Use /skin default, restart the game, then /colors again."};
                    source = ingame::game_archives() / "Crash.pak";
                }
                try
                {
                    const auto set = [&](Part part) { return parse_color(choice[part]); };
                    std::vector<std::pair<std::string, std::vector<unsigned char>>> replacements;
                    if (set(Fur) || set(Chest))
                        replacements.emplace_back(fur_texture, recolor_fur(ingame::read_archive_member(source, fur_texture), set(Fur), set(Chest)));
                    if (set(Pants) || set(Shoes))
                        replacements.emplace_back(cloth_texture, recolor_cloth(ingame::read_archive_member(source, cloth_texture), set(Pants), set(Shoes)));
                    if (set(Hair))
                        replacements.emplace_back(hair_texture, recolor_hair(ingame::read_archive_member(source, hair_texture), set(Hair)));
                    ingame::rewrite_archive(source, pak, replacements);
                }
                catch (const std::exception &e)
                {
                    return {std::string("Crash colors: couldn't make the colored Crash: ") + e.what()};
                }
                // older combinations aren't needed anymore (the game's copy is replaced at the next start)
                for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
                {
                    const auto other = it->path().filename().string();
                    if (it->is_directory(error) && colors_skin(other) && other != name)
                        std::filesystem::remove_all(it->path(), error);
                }
            }
            skins::choose(name);
            return {skins::worn() == name ? "Crash colors: wearing " + describe(choice) + "."
                                          : "Crash colors: " + describe(choice) + " ready. Restart the game to wear them."};
        }
    }

    Choice chosen()
    {
        Choice choice;
        choice.fill("default");
        std::ifstream in(state_file());
        std::string line;
        while (std::getline(in, line))
        {
            const auto equals = line.find('=');
            if (equals == std::string::npos)
                continue;
            const auto part = part_by_name(trim(line.substr(0, equals)));
            if (part != PartCount)
                choice[part] = color_text(trim(line.substr(equals + 1)));
        }
        return choice;
    }
    std::string describe(const Choice &choice)
    {
        std::string text;
        for (int p = 0; p < PartCount; ++p)
            if (color_text(choice[p]) != "default")
                text += (text.empty() ? "" : ", ") + std::string(part_name(static_cast<Part>(p))) + " " + color_text(choice[p]);
        return text.empty() ? "the normal colors" : text;
    }
    std::vector<std::string> command(const std::string &argument)
    {
        std::istringstream words(argument);
        std::string first, rest;
        words >> first;
        std::getline(words, rest);
        rest = trim(rest);
        if (first.empty())
        {
            std::string names;
            for (const auto &[name, rgb] : palette())
                names += (names.empty() ? "" : ", ") + name;
            return {"Crash colors: " + describe(chosen()) + ".",
                    "/colors fur|chest|pants|shoes|hair <color> changes one part (default puts it back), /colors reset all of them. Colors: " + names +
                        ", or #RRGGBB. They show from the next game start."};
        }
        auto choice = chosen();
        if (first == "reset" || first == "off" || first == "default")
            choice.fill("default");
        else
        {
            const auto part = part_by_name(first);
            if (part == PartCount)
                return {"Crash colors: \"" + first + "\" isn't a part. Parts: fur, chest, pants, shoes, hair."};
            const auto color = color_text(rest);
            if (color == "default" && trim(rest) != "default")
                return {"Crash colors: \"" + rest + "\" isn't a color. /colors lists them, or use #RRGGBB."};
            choice[part] = color;
        }
        save(choice);
        return prepare();
    }
    std::vector<std::string> keep_current()
    {
        const auto choice = chosen();
        const auto worn = skins::chosen();
        if (!colors_skin(worn) || all_default(choice) || worn == skin_name(choice))
            return {};
        auto lines = prepare();
        for (auto &line : lines)
            if (line.rfind("Crash colors: ", 0) == 0 && line.find("couldn't") == std::string::npos)
                line.insert(12, " (made again with this update's colors)");
        return lines;
    }
    std::vector<std::string> apply_seed_choice(const std::string &seed, const Choice &yaml, const std::string &yaml_skin)
    {
        Choice normalized;
        std::string key = seed;
        for (int p = 0; p < PartCount; ++p)
            key += "|" + (normalized[p] = color_text(yaml[p]));
        const auto file = standalone::state_directory() / "colors_seed.txt";
        {
            std::ifstream in(file);
            std::string line;
            if (in && std::getline(in, line) && line == key)
                return {};
        }
        {
            std::ofstream out(file, std::ios::trunc);
            out << key << '\n';
        }
        save(normalized);
        if (!all_default(normalized) && skins::normalize(yaml_skin) != "default")
            return {"Crash colors from your YAML (" + describe(normalized) + ") are for the normal Crash. Your YAML also picks the " +
                    skins::display(yaml_skin) + " skin, which stays."};
        auto lines = prepare();
        for (auto &line : lines)
            if (line.rfind("Crash colors: ", 0) == 0 && line.find("couldn't") == std::string::npos)
                line.insert(12, " (from your YAML)");
        return lines;
    }
}
