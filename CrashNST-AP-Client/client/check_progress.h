#pragma once
#include <json/json.h>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <array>

unsigned box_marker_color(bool spoilers, const Json::Value &location_flags, std::int64_t location);
struct CheckCounts
{
    // in_logic counts every check whose requirements are met (found or not); available only the ones not found yet
    unsigned done = 0, in_logic = 0, total = 0, available = 0;
    bool logic_known = true;
    std::string text() const;
};
struct LevelProgress : CheckCounts
{
    std::string title;
    std::array<CheckCounts, 3> collectibles{};
    CheckCounts clear{}; // the level's clear check
    CheckCounts secret_exit{}; // Secret Exit Checks: the level's secret exit, when it has one and the seed has them
    CheckCounts boxes{};       // Boxsanity: the level's box checks
    // hub label line: Level Clear, C, G, R, B (box checks) as tab-separated pieces; a leading \x01 marks red, \x02 green
    std::string collectible_text() const;
    // hub label line: checks found out of the level's total and how many are available now
    std::string label_text() const;
    // hub label line four: the level's secret exit (empty when it has none), marked red or green
    std::string secret_exit_text() const;
    // the level panel in the top-left corner: one spelled-out line for all checks, then one per collectible type
    std::string detail_text() const;
};
struct CheckProgress
{
    std::map<std::string, LevelProgress> levels;
    std::set<std::int64_t> reachable;
    std::string error;
};
struct CheckRules
{
    struct Rule
    {
        std::pair<std::string, std::string> key;
        std::int64_t id;
        std::string title;
        std::map<std::int64_t, unsigned> required;
        bool known;
    };
    std::vector<Rule> checks;
    std::map<std::int64_t, std::int64_t> local_reward_items;
    std::string error;
};
CheckRules compile_check_rules(const std::map<std::pair<std::string, std::string>, std::int64_t> &checks,
                               const Json::Value &slot_data, const std::set<std::string> &selected);
CheckProgress check_progress(const CheckRules &rules, const std::vector<std::pair<std::int64_t, int>> &items,
                             const std::set<std::int64_t> &checked, const std::set<std::int64_t> &pending,
                             const std::set<std::int64_t> &local_collected = {});
std::set<std::int64_t> box_marker_ids(const CheckProgress &progress,
                                      const std::map<std::pair<std::string, std::string>, std::int64_t> &checks,
                                      const std::set<std::int64_t> &checked, const std::set<std::int64_t> &pending,
                                      bool out_of_logic);
std::array<LevelProgress, 3> campaign_progress(const CheckProgress &progress,
                                               const std::map<std::string, std::string> &placements);
