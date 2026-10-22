#include "skins.h"
#include "standalone.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <fstream>

namespace skins
{
    namespace
    {
        std::string lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }
        std::string trim(const std::string &text)
        {
            const auto first = text.find_first_not_of(" \t\r\n");
            const auto last = text.find_last_not_of(" \t\r\n");
            return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
        }
        std::string read_line(const std::filesystem::path &path)
        {
            std::ifstream in(path);
            std::string line;
            return in && std::getline(in, line) ? trim(line) : std::string();
        }
        void write_line(const std::filesystem::path &path, const std::string &line)
        {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            std::ofstream out(path, std::ios::trunc);
            out << line << '\n';
        }
        // "name|wearing|path", "name|missing|still worn", "name|failed|reason"
        std::vector<std::string> report()
        {
            char text[4096]{};
            const auto count = GetEnvironmentVariableA("CRASHNSTAP_SKIN", text, sizeof(text));
            if (!count || count >= sizeof(text))
                return {};
            const std::string value(text);
            const auto first = value.find('|'), second = first == std::string::npos ? first : value.find('|', first + 1);
            if (second == std::string::npos)
                return {};
            return {value.substr(0, first), value.substr(first + 1, second - first - 1), value.substr(second + 1)};
        }
    }
    std::filesystem::path folder()
    {
        HMODULE self = nullptr;
        wchar_t path[32768]{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&folder), &self) ||
            !GetModuleFileNameW(self, path, static_cast<DWORD>(std::size(path))))
            return {};
        return std::filesystem::path(path).parent_path() / L"skins";
    }
    std::vector<std::string> available()
    {
        std::vector<std::string> names;
        std::error_code error;
        for (std::filesystem::directory_iterator it(folder(), error), end; !error && it != end; it.increment(error))
        {
            const bool directory = it->is_directory(error);
            if (!directory && lower(it->path().extension().string()) != ".pak")
                continue;
            const auto name = directory ? it->path().filename().string() : it->path().stem().string();
            if (lower(name) != "default")
                names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        return names;
    }
    std::string normalize(const std::string &name)
    {
        const auto key = lower(trim(name));
        if (key.empty() || key == "default_crash" || key == "default" || key == "normal" || key == "vanilla" || key == "off")
            return "default";
        if (key == "playstation_1" || key == "playstation 1" || key == "ps1")
            return "playstation_1";
        if (key == "jet pack" || key == "jet_pack" || key == "jet-pack")
            return "jetpack";
        return key;
    }
    std::string display(const std::string &name)
    {
        const auto key = normalize(name);
        if (key == "default")
            return "Default Crash";
        if (key == "playstation_1")
            return "PlayStation 1 Crash";
        if (key == "biker")
            return "Biker Crash";
        if (key == "jetpack")
            return "Jetpack Crash";
        return name;
    }
    std::string chosen() { return normalize(read_line(standalone::state_directory() / "skin.txt")); }
    void choose(const std::string &name) { write_line(standalone::state_directory() / "skin.txt", normalize(name)); }
    std::string worn()
    {
        const auto parts = report();
        if (parts.empty())
            return {};
        return parts[1] == "wearing" ? normalize(parts[0]) : parts[1] == "missing" ? normalize(parts[2]) : std::string("default");
    }
    std::string status()
    {
        const auto parts = report();
        const auto next = chosen();
        if (parts.empty())
            return next == "default" ? std::string()
                                     : "Skin: " + display(next) + " is chosen, but this dinput8.dll is older than skins. Copy the new dinput8.dll from the client zip next to the game's .exe.";
        std::string line;
        if (parts[1] == "wearing")
            line = "Skin: wearing " + display(parts[0]) + ".";
        else if (parts[1] == "missing")
        {
            std::string list;
            for (const auto &name : available())
                list += (list.empty() ? "" : ", ") + name;
            line = "Skin: " + display(parts[0]) + " is chosen, but CrashNSTAP\\skins has no skin by that name (" +
                   (list.empty() ? std::string("the folder is empty") : "it has: " + list) + "). Still wearing " + display(parts[2]) + ".";
        }
        else
            line = "Skin: couldn't put on " + display(parts[0]) + ": " + parts[2] + ".";
        if (next != normalize(parts[0]))
            line += " " + display(next) + " is chosen for the next time the game starts.";
        return line;
    }
    std::string apply_seed_choice(const std::string &seed, const std::string &skin)
    {
        const auto wanted = normalize(skin);
        const auto key = seed + "|" + wanted;
        const auto file = standalone::state_directory() / "skin_seed.txt";
        if (read_line(file) == key)
            return {};
        write_line(file, key);
        if (chosen() == wanted)
            return {};
        choose(wanted);
        const auto now = worn();
        return now == wanted ? "Skin: " + display(wanted) + " (from your YAML)."
                             : "Skin: " + display(wanted) + " from your YAML. Restart the game to wear it.";
    }
}
