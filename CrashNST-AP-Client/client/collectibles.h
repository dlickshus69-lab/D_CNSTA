#pragma once
#include <cstdint>
#include <cstring>
#include <json/json.h>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ingame
{
    struct CollectibleSpec
    {
        const char *key;
        unsigned masks[4];
        std::int64_t items[4];
    };
    inline constexpr CollectibleSpec collectible_specs[]{
#include "collectible_specs.inc"
    };
    inline const CollectibleSpec *collectible_spec(const char *key)
    {
        for (const auto &spec : collectible_specs)
            if (!std::strcmp(spec.key, key))
                return &spec;
        return nullptr;
    }
    inline unsigned collectible_mask(const CollectibleSpec &spec)
    {
        return spec.masks[0] | spec.masks[1] | spec.masks[2] | spec.masks[3];
    }
    inline unsigned collectible_bits(const CollectibleSpec &spec)
    {
        const auto mask = collectible_mask(spec);
        return (mask & 0x2000) ? mask | 0x7000 : mask;
    }
    inline unsigned collectible_observed(unsigned mask) { return (mask & 0x7000) ? (mask & ~0x7000) | 0x2000 : mask; }
    inline constexpr const char *collectible_events[]{"gem", "extra_gem", "relic", "crystal"};
    inline std::int64_t collectible_location(const CollectibleSpec &spec, unsigned i)
    {
        const unsigned number = (spec.key[1] - '0') * 100 + (spec.key[2] - '0') * 10 + spec.key[3] - '0';
        return 0xCB0000 + 10000 + number * 10 + (i == 0 ? 1 : i == 1 ? 2
                                                          : i == 2   ? 3
                                                                     : 4);
    }
    struct Collectible;
    std::vector<Collectible> collectible_configuration(const Json::Value &slot, const std::set<std::string> &levels,
                                                       const std::vector<std::pair<std::int64_t, int>> &received, const std::set<std::int64_t> &local,
                                                       const std::set<std::int64_t> &checked);
    std::int64_t local_collectible_item(const Json::Value &slot, std::int64_t location);
    bool colored_gem_item(std::int64_t item);
}
