#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace save_probe
{

    struct Zone
    {
        std::uint32_t completions;
        std::uint32_t relic;
        std::uint32_t collectibles;

        bool operator==(const Zone &other) const
        {
            return completions == other.completions && relic == other.relic &&
                   collectibles == other.collectibles;
        }
        bool operator!=(const Zone &other) const { return !(*this == other); }
    };

    using Zones = std::map<std::string, Zone>;
    std::string level_key(const std::string &zone);

    std::vector<std::uint8_t> read_file(const std::filesystem::path &path);
    Zones zones(const std::vector<std::uint8_t> &data);
    std::map<char, std::string> recent_slots(const std::vector<std::uint8_t> &data);
    bool license_accepted(const std::vector<std::uint8_t> &data);

} // namespace save_probe
