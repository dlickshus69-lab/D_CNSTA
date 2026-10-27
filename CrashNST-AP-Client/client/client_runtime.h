#pragma once
#include <atomic>
#include <random>
#include "Archipelago.h"
#include "cloud_saves.h"
#include "console.h"
#include "game_hooks.h"
#include "ingame_bridge.h"
#include "seed_config.h"
#include "session.h"
#include "standalone.h"
#include <chrono>
#include <deque>
#include <filesystem>
#include <future>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>

// (V1.3.45) a death in the shared death count: "time|player", the time as its DeathLink gave it (whole seconds, or
// seconds with 6 decimals), so every client writes the same entry for the same death; empty when it can't be keyed
inline std::string death_entry_key(const Json::Value &time, const std::string &player)
{
    if (player.empty() || !time.isNumeric())
        return {};
    const double t = time.asDouble();
    if (!std::isfinite(t) || t < 0)
        return {};
    char text[64];
    if (time.isIntegral() || t == std::floor(t))
        std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(t));
    else
        std::snprintf(text, sizeof(text), "%.6f", t);
    return std::string(text) + "|" + player;
}

struct ClientTick
{
    AP_ConnectionStatus status = AP_ConnectionStatus::Disconnected;
    std::vector<std::pair<std::int64_t, int>> items;
    std::set<std::int64_t> checked;
    const std::map<CheckKey, std::int64_t> &checks;
    const std::set<std::string> &levels;
    const std::map<std::string, std::string> &placements;
    bool online, room_valid = false, room_ready = false, box_checks = false, seed_matches = false;
    bool c1_active = false, c2_active = false, c3_active = false;
};
struct C3HubState
{
    bool active;
    std::set<std::string> allowed;
    std::map<std::string, std::uintptr_t> entities;
};

class ClientRuntime
{
  public:
    ClientRuntime(std::string server, std::string slot) : server(std::move(server)), slot(std::move(slot))
    {
    }
    ~ClientRuntime()
    {
        AP_Shutdown();
    }
    int run();

  private:
    std::string server, slot;
    const DWORD pid = GetCurrentProcessId();
    bool running = true;
    std::mutex state_mutex;
    std::deque<std::string> ap_packets;
    std::size_t ap_packet_bytes = 0;
    std::string ap_packet_error;
    std::deque<std::string> console_notifications;
    // who sent each DeathLink received (filled by the AP thread), counted on the next update
    std::deque<std::string> deaths_seen;
    // (V1.3.45) the shared death count. DeathLinks come in as raw bounces, so each death is known by its time and its
    // player: "time|player" entries in the room's DataStorage (CrashNST:deaths:team<N>), written by every client that
    // sees the death (the same entry from several clients is one), read back by all of them, so every client counts
    // the same deaths, also the ones while it was offline. The AP thread fills these two:
    std::deque<std::string> death_entries_out; // entries to add to the room's list
    std::atomic<unsigned> deathlinks_pending{0}; // DeathLinks from others, waiting to strike (several at once strike once)
    Json::Value shared_deaths = Json::Value(Json::objectValue), shared_death_bases = Json::Value(Json::objectValue);
    bool shared_deaths_ready = false, shared_death_bases_ready = false, shared_death_base_sent = false;
    std::string shared_deaths_key, shared_death_bases_key;
    void connect_shared_deaths(int team);
    void receive_shared_deaths(const Json::Value &message);
    void send_shared_deaths();
    void send_deathlink();
    // DeathLink deaths per player for this seed (yours and everyone's), saved in the state folder
    std::map<std::string, unsigned> death_tally;
    std::string death_tally_seed;
    bool death_tally_dirty = false;
    std::vector<std::string> death_tally_shown;
    std::chrono::steady_clock::time_point death_tally_refresh{};
    void count_death(const std::string &player);
    void update_death_tally();
    const std::vector<std::string> c2_regular_levels = c2_portal_levels();
    std::filesystem::path save_dir;
    std::map<char, std::string> bound_saves;
    const std::filesystem::path session_directory = standalone::state_directory() / L"sessions";
    Session session;
    std::optional<SeedConfig> seed;
    std::unique_ptr<CloudSaves> cloud;
    std::string observed_seed, session_error;
    bool server_matches_session = false, session_failed = false;
    std::string session_profile = server + '\n' + slot;

    std::string staged_server = server, staged_slot = slot, connection_password;
    std::vector<std::string> c2_access_levels = c2_regular_levels;
    ingame::StartupConnection startup_connection;
    std::chrono::steady_clock::time_point transport_connected_at{};
    bool popups_ready = false, receipt_history_ready = false;
    std::map<int, std::string> player_aliases;
    std::vector<Json::Value> fresh_receipts;
    std::vector<Json::Value> deferred_receipts;
    std::size_t deferred_receipt_bytes = 0;

    AP_ConnectionStatus previous = AP_ConnectionStatus::Disconnected;
    std::size_t previous_items = 0, previous_checks = 0;
    bool first = true;
    std::map<std::string, save_probe::Zones> previous_zones;
    std::map<std::string, std::string> previous_save_errors;
    std::map<std::string, std::set<std::int64_t>> displayed_candidates;
    std::string previous_progress_message;
    std::map<std::string, bool> previous_access;
    std::map<std::string, std::unique_ptr<C2PortalGate>> c2_gates;
    std::map<std::string, std::unique_ptr<C2LiftTraversal>> c2_lifts;
    std::unique_ptr<C2BossPortals> c2_boss;
    std::map<std::string, std::unique_ptr<PortalTriggerGate>> c3_gates;
    std::unique_ptr<C3HubAccess> c3_access;
    std::unique_ptr<C3PortalAccess> c3_portals;
    std::unique_ptr<C1MapAccess> c1_map;
    std::future<C1MapDiscovery> c1_discovery;
    std::chrono::steady_clock::time_point next_c1_probe = std::chrono::steady_clock::now();
    std::string c1_error;
    std::unique_ptr<AbilityAccess> ability_access;
    AbilityOwnership previous_abilities{};
    bool ability_refresh_pending = false;
    std::string ability_error;
    std::chrono::steady_clock::time_point next_ability_probe = std::chrono::steady_clock::now();
    GemDiscovery gem_cache;
    std::unique_ptr<CrateEvents> crate_events;
    CrateMetadataPointer crate_pointer;
    std::unique_ptr<BoxMarkers> box_markers;
    const std::filesystem::path display_file = standalone::state_directory() / "display.json";
    unsigned box_marker_mode = ingame::MarkersTint;
    bool out_of_logic_markers = false;
    std::array<unsigned, 3> hub_label_distances = ingame::default_hub_distances;
    float tint_opacity = ingame::default_tint_opacity;
    float slow_tilt = ingame::default_slow_tilt;

    std::string marker_error;
    std::chrono::steady_clock::time_point next_marker_attach = std::chrono::steady_clock::now();
    std::deque<ingame::Line> feed_history;
    std::deque<std::string> console_commands;
    bool names_loaded = false, render_announced = false;
    std::string completion_error;
    unsigned last_deaths = 0, last_collectible_awards = 0;
    // a DeathLink received on a ride (hog, bear, jet ski, ...), waiting for the next level played on foot
    bool deathlink_held = false;
    std::chrono::steady_clock::time_point next_trap{}, next_aku_mask{};
    // /aku_lock: -1 follows the seed (Aku Aku Sanity and the Aku Aku item), 0 off, 1 on
    int aku_lock_override = -1;
    // the kind of the next Nitro Trap: -1 not picked yet, 0 ahead of Crash, 1 falling onto him
    std::uint64_t last_pause_tick = 0;
    std::string routing_refused;
    std::unique_ptr<LevelRouting> level_routing;
    // /boss (test, Harder bosses): the boss fight the next entrance loads ("B204"), empty when none
    std::string boss_wanted;
    // Harder bosses test commands (/boss_health, /boss_speed, /boss_rules); -1 leaves it to the seed
    int boss_health_test = -1, boss_speed_test = -1, boss_rules_test = -1;
    // (V1.3.57 test, /boss_slam) Papu Papu's slam at 200% boss speed, in ms after he stands; -1: 450 (the speeds between follow it)
    int boss_slam_test = -1;
    // (V1.3.58 test, /boss_window) how long Papu Papu can be hit once his red ends, in ms (V1.3.59: at 200% boss speed, the
    // speeds below follow it); -1: the seed's (V1.3.61: boss_hit_window), or 2200 at 200% (V1.3.71)
    int boss_window_test = -1;
    // (V1.3.66 test, /boss_lead) how far Papu Papu's slam aim leads Crash, in percent; -1: 55 (V1.3.67)
    int boss_lead_test = -1;
    // (V1.5.1 test, /invincible) Crash takes no hits; not saved
    bool invincible_test = false;
    // (V1.4.3 research, /roo_files) what the background export of the Nitro's files has to say, for the console
    std::mutex research_mutex;
    std::vector<std::string> research_lines;
    std::atomic_bool research_running{false};
    // (Testing V1.4.24) a seed with Ripper Roo's Nitros: his arena's file checked once a session; the background copy's
    // result, for a line shown in the game
    bool roo_nitro_checked = false;
    std::atomic_int roo_nitro_made{0}; // 1 made, 2 couldn't
    bool boss_bar_grow = true; // /boss_bar (test; V1.3.22: grow by default, asked for)
    unsigned routing_start_count = 0, routing_secret_return_count = 0;
    std::string routing_error;
    std::string crate_error;
    std::set<std::int64_t> crate_checks;
    std::chrono::steady_clock::time_point next_crate_probe = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point next_crate_recheck = std::chrono::steady_clock::now();
    unsigned unmatched_crates_logged = 0;
    std::future<GemDiscovery> gem_discovery;
    std::map<std::string, std::unique_ptr<GemRouteAccess>> gem_access;
    std::unique_ptr<GemRefreshAccess> gem_refresh;
    bool gem_refresh_pending = false;
    std::string gem_error;
    std::chrono::steady_clock::time_point next_gem_probe = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point next_gem_bootstrap = std::chrono::steady_clock::now();
    GemOwnership previous_gems{};
    std::set<std::string> previous_c1_allowed;
    std::chrono::steady_clock::time_point next_c3_access_attach = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point next_c3_portal_attach = std::chrono::steady_clock::now();
    std::future<C3ButtonDiscovery> c3_discovery;
    std::chrono::steady_clock::time_point next_c3_probe = std::chrono::steady_clock::now();
    std::map<std::string, std::string> c3_errors;
    std::map<std::string, bool> c3_previous_locked;
    std::chrono::steady_clock::time_point next_gate_probe = std::chrono::steady_clock::now();
    std::uintptr_t zone_base = 0;
    ActiveZone previous_zone;
    std::uint64_t zone_epoch = 0, gate_epoch = 0, c1_epoch = 0, c3_epoch = 0;
    // (V1.3.43) the zone_epoch the box observer was attached in (it attaches again in each new level or scene), and how
    // many of its re-attaches went to the log
    std::uint64_t crate_epoch = 0;
    unsigned box_reattach_lines = 0;
    bool rediscover_c2 = true, rediscover_c3 = true;
    std::string zone_error;
    std::string previous_gate_error;
    std::map<std::string, std::string> gate_errors;
    std::future<std::map<std::string, std::uintptr_t>> gate_discovery;
    std::string previous_seed_error;
    std::set<std::int64_t> submitted_checks;
    // Secret Exit Checks: source levels whose secret exit was already announced in the console
    std::set<std::string> announced_secret_exits;
    // the game's sound banks are searched in the background (the TNT countdown sound at startup, /sound_find):
    // the lines to print when the search ends
    std::future<std::vector<std::string>> sound_job;
    bool sound_search_started = false;
    void start_sound_job(std::function<std::vector<std::string>()> job);
    bool goal_sent = false;
    std::string previous_gameplay_state;
    std::map<char, std::string> previous_goal_progress;

    void queue_ap_packet(std::string raw);
    void register_packet_callbacks();
    void receive_items(const Json::Value &message);
    void initialize_ap();
    void save_connection();
    void print_console(const std::string &text, bool popup = true);
    int initialize();
    void handle_commands();
    void process_packets(AP_ConnectionStatus status);
    void update_bridge(AP_ConnectionStatus status);
    void update_routing(ClientTick &tick);
    void update_display(ClientTick &tick);
    void update_saves(ClientTick &tick);
    void update_zone(ClientTick &tick);
    void update_c2(ClientTick &tick);
    C3HubState update_c3_gates(ClientTick &tick);
    void update_c1(ClientTick &tick);
    void update_c3_access(ClientTick &tick, const C3HubState &hub);
    void update_ownership(ClientTick &tick);
    void log_status(ClientTick &tick);
    void wait_for_commands();
    void drain_crates(ClientTick &tick);
    void finish();
};
