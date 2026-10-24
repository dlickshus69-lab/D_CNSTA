#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "check_progress.h"
#include "ingame_protocol.h"

struct BoxMarker
{
    std::string level;
    std::array<float, 3> position;
    std::string name;
    unsigned color = 0;
};
class BoxMarkers
{
public:
    explicit BoxMarkers(DWORD pid, bool startup = false, unsigned marker_mode = ingame::MarkersTint);
    ~BoxMarkers();
    void update(std::vector<BoxMarker> markers);
    void message(std::string text);
    void goal(const std::string &text);
    void death_tally(const std::vector<std::string> &lines);
    void progress(const CheckProgress &counts, const std::map<std::string, std::string> &placements,
                  const std::set<std::string> &unlocked);
    void feed(ingame::Line line, bool popup = true);
    void completion(const std::vector<std::string> &items, const std::vector<std::string> &locations);
    void configure(const std::map<std::string, unsigned> &styles, bool death_link, bool move_shuffle = false,
                   unsigned owned_moves = 0, bool trap_kills = false);
    // start a trap now; timed traps last `seconds` (Freeze always lasts freeze_trap_ms).
    // Life Loss runs through AbilityAccess instead. Slow, Slippery and Reverse take turns: when this one has to wait
    // for another to end, returns that one's name ("" when it starts now).
    // starts a timed trap; `already` (when given) says one of that kind was running or waiting, so it added no time
    std::string apply_trap(unsigned kind, unsigned seconds, bool *already = nullptr);
    // research: log the current level's Nitro crates and their spawners (read-only)
    void request_nitro_probe();
    // research (experimental): move the nearest Nitro crate next to Crash
    void request_nitro_test();
    void request_nitro_scan();
    void request_ice_probe();
    void ice_watch(bool on);
    void move_probe(bool on);
    void ice_test(unsigned mode);
    void request_ice_find();
    // the Nitro Trap: countdown, then kind 0 a coin flip, 1 a Nitro falls onto Crash, 2 one appears ahead of him
    void request_nitro_drop(unsigned kind = 0);
    std::string take_ice_note(); // a new console message from the game thread, or empty
    void request_nitro_spawn();
    bool nitro_ready();
    bool nitro_drop_ready();
    // a Nitro Trap is waiting (lets the game thread get a Nitro ready in levels without Nitro crates)
    void nitro_wanted(bool wanted);
    bool player_ready();
    bool aku_hold();
    bool player_settled();
    void pause_traps(bool paused, std::uint64_t elapsed_ms);
    void c3_buttons(bool ready, const std::set<std::string> &allowed = {});
    unsigned collectibles(std::vector<ingame::Collectible> config, unsigned relic_min_rank = 1);
    std::vector<ingame::CollectibleObservation> collectible_observations();
    void connection(const std::string &text);
    std::vector<std::string> commands();
    void receive_death();
    unsigned deaths();
    std::uintptr_t tick() const { return tick_; }
    std::uintptr_t double_jump_filter() const { return double_jump_filter_; }
    std::uintptr_t crate_identity() const { return crate_identity_; }
    bool rendering();
    bool startup() const { return startup_; }
    void startup_popup(bool show);
    void marker_mode(unsigned mode);
    void hub_distance(const std::array<unsigned, 3> &distances);
    void tint_opacity(float opacity);
    void slow_tilt(float tilt);
    void aku_lock(bool on);
    // Harder bosses: health and speed in percent (100 the game's own), and the rules on bosses that can be hit any time
    void boss_settings(unsigned health, unsigned speed, bool rules);
    void request_aku_give();
    // Crash's colors (research): log his model's parts; blend a color over him (amount 0 turns it off)
    void request_crash_probe();
    void crash_tint(float red, float green, float blue, float amount);
    float tuned_slow_tilt();
    bool toggle(const std::string &target, const std::string &value);
    void stop(bool handover = false);

private:
    void lock();
    void unlock();
    HANDLE mapping_ = nullptr, mutex_ = nullptr;
    ingame::Shared *shared_ = nullptr;
    std::uintptr_t tick_ = 0;
    unsigned ice_note_seen_ = 0;
    std::uintptr_t double_jump_filter_ = 0;
    std::uintptr_t crate_identity_ = 0;
    bool owned_ = false;
    bool startup_ = false;
    DWORD pid_ = 0;
    std::uintptr_t movement_site_ = 0;
    std::array<unsigned char, 5> movement_call_{};
};
