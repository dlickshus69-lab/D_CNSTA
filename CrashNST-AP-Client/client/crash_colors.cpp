#include "crash_colors.h"
#include "game_archive.h"
#include "skins.h"
#include "standalone.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <set>
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

        // V1.2.52: a few levels (Lights Out, Fumbling in the Dark, some bosses and cutscenes) carry their own copies of
        // Crash's textures and use those instead of Crash.pak's, so he showed his normal colors there. Making a copy
        // of each of those archives would take gigabytes, so the colored textures go into them instead: at the next
        // game start, dinput8.dll adds the colored copies at the end of each archive and points the texture's record
        // at them, keeping the original records (and the file's original size) to put back. This file tells it what
        // to do: "plan <skin>", then "archive|its original size|record offset|original record (hex)|colored file|length"
        std::filesystem::path plan_file() { return standalone::state_directory() / "color_patches.txt"; }
        // what dinput8.dll did: "archive|original size|patched size|plan|record offset|original record" per record
        std::filesystem::path patched_file() { return skins::folder() / "patched.txt"; }
        std::string hex16(const std::array<unsigned char, 16> &bytes)
        {
            static const char digits[] = "0123456789abcdef";
            std::string text;
            for (const auto b : bytes)
                text += digits[b >> 4], text += digits[b & 15];
            return text;
        }
        // a path as UTF-8 (dinput8.dll reads the plan as UTF-8)
        std::string utf8(const std::wstring &text)
        {
            if (text.empty())
                return {};
            std::string result(WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), static_cast<int>(result.size()), nullptr, nullptr);
            return result;
        }
        std::string lowercase(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }
        std::vector<std::string> split(const std::string &line, char by)
        {
            std::vector<std::string> parts;
            std::string part;
            std::istringstream in(line);
            while (std::getline(in, part, by))
                parts.push_back(part);
            return parts;
        }
        void remove_plan()
        {
            std::error_code error;
            std::filesystem::remove(plan_file(), error);
        }
        // the plan for this skin's colored textures (member name -> colored file); a console line, or empty
        std::string write_plan(const std::string &skin, const std::map<std::string, std::filesystem::path> &colored)
        {
            // what is patched now: an archive's original size and original records, while it still has the size
            // dinput8.dll left it at
            struct Patched
            {
                std::uint64_t original = 0, patched = 0;
                std::map<std::uint64_t, std::string> records;
            };
            std::map<std::string, Patched> patched;
            {
                std::ifstream in(patched_file());
                std::string line;
                while (std::getline(in, line))
                {
                    const auto f = split(trim(line), '|');
                    if (f.size() < 6)
                        continue;
                    auto &p = patched[f[0]];
                    p.original = std::strtoull(f[1].c_str(), nullptr, 10), p.patched = std::strtoull(f[2].c_str(), nullptr, 10);
                    p.records[std::strtoull(f[4].c_str(), nullptr, 10)] = f[5];
                }
            }
            std::vector<std::string> names;
            std::map<std::string, std::uint64_t> lengths;
            for (const auto &[member, file] : colored)
            {
                std::error_code error;
                names.push_back(member);
                lengths[member] = std::filesystem::file_size(file, error);
            }
            const auto root = ingame::game_archives();
            std::ostringstream out;
            out << "plan " << skin << '\n';
            unsigned archives = 0;
            std::string listed;
            for (const auto &copies : ingame::find_archive_copies(names))
            {
                std::error_code error;
                const auto relative = utf8(std::filesystem::relative(copies.pak, root, error).generic_wstring());
                if (error || relative.empty() || lowercase(relative) == "crash.pak")
                    continue; // Crash.pak itself is the colored skin
                const auto p = patched.find(relative);
                const bool ours = p != patched.end() && p->second.patched == copies.size;
                const auto original_size = ours ? p->second.original : copies.size;
                unsigned lines = 0;
                for (const auto &m : copies.members)
                {
                    auto original = hex16(m.record);
                    if (ours)
                        if (const auto r = p->second.records.find(m.at); r != p->second.records.end())
                            original = r->second;
                    // only where the archive's copy is the same size as Crash.pak's (the same texture); the length is the
                    // record's third word, little-endian
                    if (original.size() != 32)
                        continue;
                    std::uint64_t length = 0;
                    for (int b = 3; b >= 0; --b)
                        length = length * 256 + std::strtoul(original.substr(16 + b * 2, 2).c_str(), nullptr, 16);
                    std::string member;
                    for (const auto &[name, file] : colored)
                        if (lowercase(name) == m.name)
                            member = name;
                    if (member.empty() || length != lengths[member])
                        continue;
                    out << relative << '|' << original_size << '|' << m.at << '|' << original << '|' << utf8(colored.at(member).wstring()) << '|'
                        << lengths[member] << '\n';
                    ++lines;
                }
                if (lines)
                    ++archives, listed += (listed.empty() ? "" : ", ") + copies.pak.stem().string();
            }
            std::error_code error;
            std::filesystem::create_directories(plan_file().parent_path(), error);
            auto temporary = plan_file();
            temporary += ".tmp";
            {
                std::ofstream file(temporary, std::ios::trunc | std::ios::binary);
                file << out.str();
            }
            std::filesystem::rename(temporary, plan_file(), error);
            if (error)
                return "Crash colors: couldn't save the list of other archives to color (" + error.message() + ").";
            return archives ? "Crash colors: also going into " + std::to_string(archives) + " levels and scenes with their own copy of Crash (" + listed + ")."
                            : std::string();
        }
        // makes the colored Crash.pak for the chosen colors (once per combination) and chooses it as the skin; the
        // colored textures are also kept on their own (patches\), and the other archives holding copies of them are
        // listed for dinput8.dll
        std::vector<std::string> prepare()
        {
            const auto choice = chosen();
            if (all_default(choice))
            {
                remove_plan();
                if (!colors_skin(skins::chosen()))
                    return {};
                skins::choose("default");
                return {skins::worn() == "default" ? "Crash colors: back to the normal colors." : "Crash colors: back to the normal colors. Restart the game to see them."};
            }
            const auto name = skin_name(choice);
            const auto folder = skins::folder();
            const auto pak = folder / name / "Crash.pak";
            const auto patches = folder / name / "patches";
            std::error_code error;
            if (!std::filesystem::exists(pak, error) || !std::filesystem::exists(patches / "members.txt", error))
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
                    using Recolor = std::function<std::vector<unsigned char>(const std::vector<unsigned char> &)>;
                    // a texture (required) and its splashed variants (skipped when this game version lacks them)
                    const auto add = [&](const char *member, bool required, const Recolor &recolor)
                    {
                        std::vector<unsigned char> bytes;
                        try
                        {
                            bytes = ingame::read_archive_member(source, member);
                            replacements.emplace_back(member, recolor(bytes));
                        }
                        catch (const std::exception &)
                        {
                            if (required)
                                throw;
                        }
                    };
                    if (set(Fur) || set(Chest))
                    {
                        const Recolor fur = [&](const std::vector<unsigned char> &b) { return recolor_fur(b, set(Fur), set(Chest)); };
                        add(fur_texture, true, fur), add(fur_fruit_texture, false, fur), add(fur_monkey_texture, false, fur);
                    }
                    if (set(Pants) || set(Shoes))
                    {
                        const Recolor cloth = [&](const std::vector<unsigned char> &b) { return recolor_cloth(b, set(Pants), set(Shoes)); };
                        add(cloth_texture, true, cloth), add(cloth_fruit_texture, false, cloth);
                    }
                    if (set(Hair))
                    {
                        const Recolor hair = [&](const std::vector<unsigned char> &b) { return recolor_hair(b, set(Hair)); };
                        add(hair_texture, true, hair), add(hair_fruit_texture, false, hair);
                    }
                    ingame::rewrite_archive(source, pak, replacements);
                    // the colored textures on their own, for the archives with their own copies
                    std::filesystem::create_directories(patches, error);
                    std::ofstream list(patches / "members.txt", std::ios::trunc);
                    for (std::size_t r = 0; r < replacements.size(); ++r)
                    {
                        const auto file = std::to_string(r) + ".igz";
                        std::ofstream out(patches / file, std::ios::binary | std::ios::trunc);
                        out.write(reinterpret_cast<const char *>(replacements[r].second.data()), static_cast<std::streamsize>(replacements[r].second.size()));
                        if (!out)
                            throw std::runtime_error("couldn't save " + (patches / file).string());
                        list << replacements[r].first << '|' << file << '\n';
                    }
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
            std::vector<std::string> lines{skins::worn() == name ? "Crash colors: wearing " + describe(choice) + "."
                                                                  : "Crash colors: " + describe(choice) + " ready. Restart the game to wear them."};
            // the levels and scenes with their own copy of Crash's textures get the colored ones too
            std::map<std::string, std::filesystem::path> colored;
            {
                std::ifstream list(patches / "members.txt");
                std::string line;
                while (std::getline(list, line))
                {
                    const auto f = split(trim(line), '|');
                    if (f.size() == 2)
                        colored[f[0]] = patches / f[1];
                }
            }
            if (colored.empty())
                remove_plan();
            else if (const auto note = write_plan(name, colored); !note.empty())
                lines.push_back(note);
            return lines;
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
    std::string patch_status()
    {
        char text[1024]{};
        const auto count = GetEnvironmentVariableA("CRASHNSTAP_COLORS", text, sizeof(text));
        if (!count || count >= sizeof(text))
            return {};
        const auto f = split(text, '|');
        if (f.size() < 3)
            return {};
        const auto in = std::strtoul(f[0].c_str(), nullptr, 10), skipped = std::strtoul(f[2].c_str(), nullptr, 10);
        std::string line;
        if (in)
            line = "Crash colors: also in " + std::to_string(in) + " levels and scenes that have their own copy of Crash.";
        if (skipped)
            line += std::string(line.empty() ? "Crash colors:" : "") + " " + std::to_string(skipped) + " couldn't be colored" +
                    (f.size() > 3 && !f[3].empty() ? " (" + f[3] + ")" : std::string()) + ".";
        return line;
    }
    std::vector<std::string> keep_current()
    {
        const auto choice = chosen();
        const auto worn = skins::chosen();
        if (!colors_skin(worn) || all_default(choice) || worn == skin_name(choice))
            return {};
        auto lines = prepare();
        // the first line is about the colors themselves
        if (!lines.empty() && lines.front().rfind("Crash colors: ", 0) == 0 && lines.front().find("couldn't") == std::string::npos)
            lines.front().insert(12, " (made again with this update's colors)");
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
        // the first line is about the colors themselves
        if (!lines.empty() && lines.front().rfind("Crash colors: ", 0) == 0 && lines.front().find("couldn't") == std::string::npos)
            lines.front().insert(12, " (from your YAML)");
        return lines;
    }
}
