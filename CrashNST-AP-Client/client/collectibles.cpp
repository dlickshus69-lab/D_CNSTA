#include "collectibles.h"
#include "ingame_protocol.h"
#include <algorithm>
#include <stdexcept>

namespace ingame
{
    std::int64_t local_collectible_item(const Json::Value &slot, std::int64_t location)
    {
        const auto &source = slot["local_reward_sources"][std::to_string(location)];
        if (!source.isObject() || !source["level"].isString() || !source["event"].isString() || !source["item"].isInt64())
            throw std::runtime_error("Invalid local collectible source");
        const auto spec = collectible_spec(source["level"].asCString());
        if (spec)
            for (unsigned i = 0; i < 4; ++i)
                if (spec->items[i] && location == collectible_location(*spec, i) &&
                    source["item"].asInt64() == spec->items[i] && source["event"].asString() == collectible_events[i] &&
                    !slot["checks"].isMember(std::to_string(location)))
                    return spec->items[i];
        throw std::runtime_error("Local collectible identity differs");
    }
    bool colored_gem_item(std::int64_t item)
    {
        for (const auto &spec : collectible_specs)
            for (unsigned i = 0; i < 2; ++i)
                if (spec.items[i] == item && (spec.masks[i] & 0xfc))
                    return true;
        return false;
    }
    std::vector<Collectible> collectible_configuration(const Json::Value &slot, const std::set<std::string> &levels,
                                                       const std::vector<std::pair<std::int64_t, int>> &received, const std::set<std::int64_t> &local,
                                                       const std::set<std::int64_t> &checked)
    {
        std::vector<Collectible> result;
        if (!slot["collectible_locations"].isObject() ||
            !slot["local_reward_sources"].isObject())
            throw std::runtime_error("Invalid collectible slot data");
        std::set<std::int64_t> owned;
        for (const auto &item : received)
            owned.insert(item.first);
        for (auto id : local)
            owned.insert(local_collectible_item(slot, id));
        unsigned selected_count = 0, local_reward_count = 0;
        for (const auto &spec : collectible_specs)
            if (levels.count(spec.key))
            {
                Collectible c;
                copy(c.key, spec.key);
                for (unsigned i = 0; i < 4; ++i)
                    if (spec.items[i])
                    {
                        const auto id = std::to_string(collectible_location(spec, i));
                        {
                            const auto &selected = slot["collectible_locations"][id];
                            const auto &local_source = slot["local_reward_sources"][id];
                            // a selected relic blocks all three medal bits, even though AP grants only the base relic.
                            const auto mask = i == 2 ? 0x7000 : spec.masks[i];
                            if (!selected.isNull())
                            {
                                if (!local_source.isNull() || selected["item"] != Json::Int64(spec.items[i]) ||
                                    (selected["mode"] != "randomized" && selected["mode"] != "vanilla") ||
                                    slot["checks"][id]["level"] != spec.key || slot["checks"][id]["event"] != collectible_events[i])
                                    throw std::runtime_error("Selected collectible identity differs");
                                c.selected |= mask;
                                if (selected["mode"] == "randomized")
                                    c.blocked |= mask;
                                ++selected_count;
                                if (checked.count(collectible_location(spec, i)))
                                    c.checked |= spec.masks[i];
                            }
                            else
                            {
                                local_collectible_item(slot, collectible_location(spec, i));
                                ++local_reward_count;
                            }
                        }
                        if (owned.count(spec.items[i]))
                            c.owned |= spec.masks[i];
                    }
                result.push_back(c);
            }
        if (selected_count != slot["collectible_locations"].size() || local_reward_count != slot["local_reward_sources"].size())
            throw std::runtime_error("Collectible sources outside the selected pool");
        return result;
    }
}
