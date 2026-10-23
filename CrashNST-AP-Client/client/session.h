#pragma once
#include "save_format.h"
#include <json/json.h>
#include <windows.h>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

// indexed AP receipts stay separate from local collectibles earned in this seed
class Session
{
public:
    ~Session();
    void open(const std::filesystem::path &directory, const std::string &seed,
              int team, int slot, const Json::Value &slot_data);
    bool received(unsigned index, const Json::Value &incoming); // false requests an authoritative sync
    bool resume(const std::filesystem::path &directory, const std::string &profile,
                const std::string &expected_seed);
    void remember(const std::filesystem::path &directory, const std::string &profile);
    void acknowledge(const Json::Value &locations);
    std::set<std::int64_t> scout_locations() const;
    void scouted(const Json::Value &locations);
    const Json::Value &location_flags() const { return data_["location_flags"]; }
    void connected(const Json::Value &locations, bool can_reset);
    void reset_server_state();
    bool server_reset_pending() const { return ready() && data_["server_reset_pending"].asBool(); }
    void finish_server_reset();
    std::set<std::int64_t> queue(const std::set<std::int64_t> &locations);
    void flush();
    bool ready() const { return lease_ != INVALID_HANDLE_VALUE; }
    std::string seed() const { return data_["seed"].asString(); }
    int team() const { return data_["team"].asInt(); }
    int slot() const { return data_["slot"].asInt(); }
    std::set<std::int64_t> pending() const;
    std::set<std::int64_t> checked() const;
    std::vector<std::pair<std::int64_t, int>> items() const;
    std::set<std::int64_t> local_collectibles() const;
    void observe_collectible(std::int64_t location);
    unsigned received_count() const { return ready() ? data_["items"].size() : 0; }
    unsigned filler_cursor() const { return ready() ? data_["filler_cursor"].asUInt() : 0; }
    bool reserve_filler(unsigned index); // persist before game-thread reward dispatch
    // traps are applied once each; the cursor stays local (cloud progress keeps its two fields)
    unsigned trap_cursor() const { return ready() && data_["trap_cursor"].isUInt() ? data_["trap_cursor"].asUInt() : 0; }
    // Aku Aku Mask fillers handed over so far (receipts before this index are done)
    unsigned aku_cursor() const { return ready() && data_["aku_cursor"].isUInt() ? data_["aku_cursor"].asUInt() : 0; }
    bool reserve_aku(unsigned index);
    bool reserve_trap(unsigned index);
    // Nitro Traps wait for a level with Nitro crates without holding up other traps
    unsigned nitro_pending() const { return ready() && data_["nitro_pending"].isUInt() ? data_["nitro_pending"].asUInt() : 0; }
    bool defer_nitro(unsigned index); // reserves the trap and adds it to the waiting count in one save
    void take_nitro();
    // Move Lock Traps received before Crash has any move to lock (Ability Shuffle) wait the same way
    unsigned move_lock_pending() const { return ready() && data_["move_lock_pending"].isUInt() ? data_["move_lock_pending"].asUInt() : 0; }
    bool defer_move_lock(unsigned index);
    void take_move_lock();
    std::map<char, std::string> bind_saves(const std::filesystem::path &save_directory,
                                           const std::set<std::string> &levels);
    std::filesystem::path profile_directory() const
    {
        auto p = path_.parent_path() / "profiles" / path_.stem();
        return p;
    }
    std::string storage_key() const { return "CrashNST:" + path_.stem().string() + ":progress:v2"; }
    Json::Value cloud_progress() const;
    void restore_cloud_progress(const Json::Value &progress);
    const Json::Value &slot_data() const { return data_["slot_data"]; }

private:
    Json::Value data_;
    std::filesystem::path path_;
    HANDLE lease_ = INVALID_HANDLE_VALUE;
    bool dirty_ = false;
};

std::string state_digest(const std::string &bytes);
Json::Value read_state_json(const std::filesystem::path &path);
void write_state_json(const std::filesystem::path &path, const Json::Value &value);
