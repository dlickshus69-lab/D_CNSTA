#include "client_runtime.h"
#include "trap_rules.h"
#include "movement_rules.h"
#include "game_archive.h"
#include "skins.h"
#include "crash_colors.h"
#include <fstream>
#include "game_sound.h"
#include <algorithm>
#include <iostream>
using save_probe::level_key;
// stages where Crash (or Coco) rides or flies something: the hog, polar bear, jet ski, jetpack, plane, bike, tiger and
// scuba levels (the gimmick levels). The on-foot death script a DeathLink runs breaks the ride there (on Coco's jet ski
// it left her invisible and respawned her in the wrong place), and taking a life through the lives reward didn't work,
// so a DeathLink received there waits until the player is on foot again. (V1.3.43) The Crash 2 jet-board levels (Hang
// Eight, Air Crash, Plant Food) mix board parts with parts on foot, so they aren't on this list any more: a DeathLink
// there goes to the game, which strikes once Crash is on foot (MondoF's was held as "on a ride" while he walked in Plant
// Food)
static bool ridden_level(const std::string &key)
{
    static const char *const levels[]{"L107", "L114", "L208", "L213", "L215", "L222", "L224", "L226", "L302", "L303",
                                      "L305", "L308", "L310", "L312", "L314", "L317", "L318", "L322", "L324", "L326",
                                      "L328", "L330", "L331", "L332"};
    return std::find(std::begin(levels), std::end(levels), key) != std::end(levels);
}

void ClientRuntime::update_routing(ClientTick &tick)
{
    if (session.ready() && !session_failed)
    {
        try
        {
            if (cloud && cloud->ready())
                bound_saves = session.bind_saves(save_dir, tick.levels);
            session_error.clear();
        }
        catch (const std::exception &error)
        {
            session_error = error.what();
            bound_saves.clear();
        }
    }
    tick.room_valid = session.ready() && seed && !session_failed && server_matches_session && session_error.empty() &&
                      (cloud && cloud->ready() && cloud->game_pid() == pid);
    if (tick.room_valid && !level_routing)
    {
        try
        {
            level_routing = std::make_unique<LevelRouting>(pid, tick.placements);
            routing_start_count = 0;
            routing_secret_return_count = 0;
            gem_refresh.reset();
            std::cout << "Campaign-start routing attached. All campaigns start at their default hub spawn.\n";
            routing_error.clear();
        }
        catch (const std::exception &e)
        {
            if (routing_error != e.what())
            {
                if (std::string(e.what()) == "Zone registry is not loaded")
                    std::cout << "Startup waiting: zone registry is not loaded yet.\n";
                else
                    std::cerr << "Entrance routing: " << e.what() << std::endl;
            }
            routing_error = e.what();
        }
    }
    if (tick.room_valid && level_routing)
        try
        {
            std::set<std::string> allowed;
            for (const auto &[destination, source] : secret_stages)
                if (tick.levels.count(destination) && !level_locked(*seed, tick.items, destination))
                    allowed.insert(destination);
            level_routing->secrets(allowed);
            // starting Crash 1 puts Crash at the earliest open map entrance, not where the save last left him. Kept
            // current from the title screen on, since routing is ready only after the first map loads.
            std::set<std::string> c1_locked;
            for (const auto &placement : tick.placements)
                if (placement.first.size() == 4 && placement.first[1] == '1' && level_locked(*seed, tick.items, placement.first))
                    c1_locked.insert(placement.first);
            level_routing->c1_start(c1_locked);
            const auto count = level_routing->start_count();
            if (count != routing_start_count)
            {
                routing_start_count = count;
                std::cout << "Campaign start redirected to its hub.\n";
            }
            const auto secret_returns = level_routing->secret_return_count();
            if (secret_returns != routing_secret_return_count)
            {
                routing_secret_return_count = secret_returns;
                std::cout << "CB2 secret-room return selected. Original entrance preserved.\n";
            }
            if (gem_cache.secret_handle && level_routing->bind_secret(*gem_cache.secret_handle))
            {
                gem_refresh.reset();
                routing_error.clear();
                std::cout << "Entrance routing ready. Secret stages use their vanilla paths and Access items.\n";
            }
        }
        catch (const std::exception &e)
        {
            if (routing_error != e.what())
                std::cerr << "Entrance routing: " << e.what() << std::endl;
            routing_error = e.what();
        }
    if (!tick.room_valid)
        level_routing.reset();
    tick.room_ready = tick.room_valid && level_routing && level_routing->ready();
    // Secret Exit Checks: the routing dispatcher marks each secret exit as it is taken (Crash 2's through the secret
    // warp room, Crash 3's straight into Hot Coco or Eggipus Rex), whether or not the place it leads is unlocked
    if (tick.room_ready && seed)
        try
        {
            std::set<std::int64_t> found;
            for (const auto &source : level_routing->secret_exits())
            {
                const auto check = tick.checks.find(CheckKey{source, "secret_exit"});
                if (check == tick.checks.end())
                    continue;
                if (!tick.checked.count(check->second))
                    found.insert(check->second);
                if (announced_secret_exits.insert(source).second)
                {
                    const auto rule = std::find_if(seed->rules.checks.begin(), seed->rules.checks.end(),
                                                   [&](const auto &r) { return r.id == check->second; });
                    print_console("Secret exit found: " + (rule == seed->rules.checks.end() ? source : rule->title) + "!", true);
                }
            }
            for (const auto id : session.queue(found))
            {
                std::cout << "AP secret exit check earned: " << id << '\n';
                if (tick.online && submitted_checks.insert(id).second)
                    AP_SendItem(id);
            }
        }
        catch (const std::exception &e)
        {
            if (routing_error != e.what())
                std::cerr << "Secret exits: " << e.what() << std::endl;
            routing_error = e.what();
        }
    if (tick.room_ready)
        try
        {
            // backstop for the hub gates: a locked entrance sends you back to its hub even if its gate
            // was not attached yet. secret stages keep their own route.
            std::set<std::string> locked;
            for (const auto &placement : tick.placements)
            {
                const auto &source = placement.first;
                if (std::none_of(secret_stages.begin(), secret_stages.end(), [&](const auto &s) { return s.first == source; }) &&
                    level_locked(*seed, tick.items, source))
                    locked.insert(source);
            }
            level_routing->lock_entrances(locked);
            // /boss (test): every entrance leads to that boss fight until it has loaded once
            {
                const auto here = level_key(read_scene(GetCurrentProcess()).zone);
                if (!boss_wanted.empty() && here == boss_wanted)
                {
                    print_console("Boss test: in " + boss_wanted + ". Entrances lead to their levels again.", true);
                    boss_wanted.clear();
                }
                if (level_routing->boss_test_key() != boss_wanted)
                    try
                    {
                        level_routing->boss_test(boss_wanted);
                    }
                    catch (const std::exception &e)
                    {
                        print_console("Boss test: couldn't send the entrances to " + boss_wanted + " (" + e.what() + "). Turned off.", true);
                        std::cerr << "Boss test: " << boss_wanted << ": " << e.what() << std::endl;
                        boss_wanted.clear();
                        try
                        {
                            level_routing->boss_test({});
                        }
                        catch (const std::exception &) {}
                    }
            }
            const auto refused = level_routing->refused_entrance();
            if (refused != routing_refused)
            {
                if (!refused.empty())
                    print_console("That level is still locked, so you are back in the hub.", true);
                routing_refused = refused;
            }
        }
        catch (const std::exception &e)
        {
            if (routing_error != e.what())
                std::cerr << "Entrance locks: " << e.what() << std::endl;
            routing_error = e.what();
        }
}

void ClientRuntime::update_zone(ClientTick &tick)
{
    if (tick.room_ready)
    {
        for (const auto &level : c2_access_levels)
        {
            const bool access = !level_locked(*seed, tick.items, level);
            const auto previous_access_it = previous_access.find(level);
            if (previous_access_it == previous_access.end() || previous_access_it->second != access)
                std::cout << "Desired " << level << " access: " << (access ? "open" : "closed") << std::endl;
            previous_access[level] = access;
        }
    }
    else
    {
        previous_access.clear();
    }
    ActiveZone active_zone;
    if (tick.room_ready)
        try
        {
            if (!zone_base)
                zone_base = nst_module_base(GetCurrentProcess());
            active_zone = read_active_zone(GetCurrentProcess(), zone_base);
            zone_error.clear();
        }
        catch (const std::exception &e)
        {
            if (zone_error != e.what())
                std::cerr << "Active zone: " << e.what() << std::endl;
            zone_error = e.what();
        }
    if (!(active_zone == previous_zone))
    {
        previous_zone = active_zone;
        ++zone_epoch;
        rediscover_c2 = rediscover_c3 = true;
        next_gate_probe = next_c1_probe = next_c3_probe = std::chrono::steady_clock::now();
        next_c3_access_attach = next_c3_portal_attach = std::chrono::steady_clock::now();
    }
    const bool scene_ready = active_zone.campaign && active_zone.entity_context && active_zone.script_context;
    tick.c1_active = scene_ready && active_zone.hub == 1;
    tick.c2_active = scene_ready && active_zone.hub == 2;
    tick.c3_active = scene_ready && active_zone.hub == 3;
    if (box_markers && tick.online && AP_DeathLinkPending())
    {
        const bool apply =
            tick.room_ready && session.slot_data()["death_link"].asBool() && death_link_level(active_zone);
        const bool ride = apply && ridden_level(active_zone.key);
        if (ride)
            deathlink_held = true;
        else if (apply)
            box_markers->receive_death();
        AP_DeathLinkClear();
        print_console(ride    ? "DeathLink received while you're on a ride. It gets you once you're on foot again."
                      : apply ? "DeathLink received."
                              : "DeathLink ignored outside an active level.");
    }
    // a DeathLink held from a ride: it strikes in the next level played on foot (once, however many came in)
    if (deathlink_held && box_markers && tick.online && tick.room_ready && session.slot_data()["death_link"].asBool() &&
        death_link_level(active_zone) && !ridden_level(active_zone.key))
    {
        deathlink_held = false;
        box_markers->receive_death();
        print_console("The DeathLink from your ride catches up with you.");
    }
}

void ClientRuntime::update_ownership(ClientTick &tick)
{
    AbilityOwnership desired_abilities{};
    GemOwnership desired_gems{};
    if (tick.room_ready)
    {
        std::set<std::int64_t> received;
        std::set<char> campaigns;
        for (const auto &item : tick.items)
            received.insert(item.first);
        for (auto id : session.local_collectibles())
        {
            const auto item = ingame::local_collectible_item(session.slot_data(), id);
            if (ingame::colored_gem_item(item))
                received.insert(item);
        }
        for (const auto &level : tick.levels)
            if (level.size() == 4)
                campaigns.insert(level[1]);
        desired_gems = gem_ownership(received, campaigns);
        desired_abilities = ability_ownership(received, campaigns);
    }
    const auto attach_abilities = [&](const GemDiscovery &found) {
        if (!found.power_meta ||
            (ability_access && ability_access->meta() == found.power_meta &&
             ability_access->menu_meta() == found.ability_menu_meta && ability_access->hud_meta() == found.hud_meta &&
             ability_access->jump_filter() == (box_markers ? box_markers->double_jump_filter() : 0)))
            return;
        if (ability_access && ability_access->filler_pending())
            std::cerr << "Reserved filler interrupted by loading. It will not replay.\n";
        ability_access.reset();
        try
        {
            ability_access = std::make_unique<AbilityAccess>(pid, found, desired_abilities,
                                                             box_markers ? box_markers->double_jump_filter() : 0);
            ability_error.clear();
            ability_refresh_pending = true;
            std::cout << "AP ability adapter attached. Ability-cache refresh queued.\n" << std::flush;
        }
        catch (const std::exception &error)
        {
            if (ability_error != error.what())
                std::cerr << "Ability attachment: " << error.what() << std::endl;
            ability_error = error.what();
        }
    };
    // filler needs HUD metadata
    if (tick.room_ready && (!ability_access || !ability_access->hud_meta()) &&
        std::chrono::steady_clock::now() >= next_ability_probe)
    {
        next_ability_probe = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        try
        {
            attach_abilities(find_ability_metadata(pid));
        }
        catch (const std::exception &error)
        {
            if (ability_error != error.what())
                std::cerr << "Ability discovery: " << error.what() << std::endl;
            ability_error = error.what();
        }
    }
    // the box observer listens to one level's crate script. If the game has since registered another one (a level
    // reload, a restart, a bonus round's reset), it is attached again to the current one, about once a second
    if (crate_events && tick.box_checks && tick.room_ready && std::chrono::steady_clock::now() >= next_crate_recheck)
    {
        next_crate_recheck = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        try
        {
            const auto meta = find_crate_metadata(pid, crate_pointer);
            // (V1.3.43) also the same script set up afresh, or a new level or scene since it was attached: in MondoF's log
            // (after a game over's Continue in N. Sanity Beach) and in Eric's (from the Crash 2 warp room into a level) no box
            // counted any more, with nothing said: the level's crate script kept its place, but its breaks no longer went
            // through the listener. What it already caught is counted first
            const char *why = !meta ? nullptr
                              : meta != crate_events->meta() ? "the level's crate script changed; attaching to the new one"
                              : crate_epoch != zone_epoch     ? "a new level or scene; attaching to its crate script"
                              : !crate_events->hooked()       ? "the level's crate script was set up again; attaching again"
                                                              : nullptr;
            if (why)
            {
                const bool new_scene = meta == crate_events->meta() && crate_epoch != zone_epoch;
                drain_crates(tick);
                if (!new_scene || box_reattach_lines < 40)
                    ++box_reattach_lines, std::cout << "Box observer: " << why << ".\n" << std::flush;
                crate_events.reset();
                next_crate_probe = std::chrono::steady_clock::now();
            }
        }
        catch (const std::exception &)
        {
        } // mid-load; the next check or the drain sorts it out
    }
    // box checks shouldnt wait for full gem/ability handle discovery during level loading
    if (!crate_events && tick.box_checks && tick.room_ready && std::chrono::steady_clock::now() >= next_crate_probe)
    {
        next_crate_probe = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        try
        {
            if (const auto meta = find_crate_metadata(pid, crate_pointer))
            {
                crate_events =
                    std::make_unique<CrateEvents>(pid, meta, box_markers ? box_markers->crate_identity() : 0);
                crate_epoch = zone_epoch;
                crate_error.clear();
                std::cout << "Box observer attached.\n" << std::flush;
            }
        }
        catch (const std::exception &error)
        {
            if (crate_error != error.what())
                std::cerr << "Box observer: " << error.what() << std::endl;
            crate_error = error.what();
        }
    }
    if (gem_discovery.valid() && gem_discovery.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try
        {
            const auto found = gem_discovery.get();
            if (tick.room_valid && found.registry_metadata && !gem_cache.registry_metadata)
                std::cout << "Engine metadata registry attached. Script discovery uses validated pointers.\n";
            if (tick.room_valid)
                gem_cache = found;
            if (tick.room_ready)
            {
                gem_cache = found;
                if (found.crate_meta && !crate_events && tick.box_checks)
                {
                    try
                    {
                        crate_events = std::make_unique<CrateEvents>(pid, found.crate_meta,
                                                                     box_markers ? box_markers->crate_identity() : 0);
                        crate_epoch = zone_epoch;
                        crate_error.clear();
                        std::cout << "Box observer attached.\n" << std::flush;
                    }
                    catch (const std::exception &error)
                    {
                        if (crate_error != error.what())
                            std::cerr << "Box observer: " << error.what() << std::endl;
                        crate_error = error.what();
                    }
                }
                attach_abilities(found);
                for (const auto &[name, meta] : found.graphs)
                {
                    const auto old = gem_access.find(name);
                    if (old != gem_access.end() && old->second->meta() == meta)
                        continue;
                    gem_refresh.reset();
                    gem_access.erase(name);
                    gem_access.emplace(name, std::make_unique<GemRouteAccess>(pid, found, name, desired_gems));
                    std::cout << "Gem route attached: " << name
                              << ". Ownership uses AP items for the active campaign.\n"
                              << std::flush;
                }
                gem_error.clear();
            }
        }
        catch (const std::exception &error)
        {
            if (gem_error != error.what())
            {
                if (std::string(error.what()) == "Campaign-check script graph is not loaded")
                    std::cout << "Gameplay waiting: campaign-check script graph is not loaded yet." << std::endl;
                else
                    std::cerr << "Gem discovery: " << error.what() << std::endl;
            }
            gem_error = error.what();
            gem_cache = {};
        }
        next_gem_probe = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    }
    if (tick.room_valid && !gem_discovery.valid() && std::chrono::steady_clock::now() >= next_gem_probe)
    {
        // periodically bootstrap again to cover script slabs allocated outside the cached allocator
        if (std::chrono::steady_clock::now() >= next_gem_bootstrap)
        {
            if (!gem_cache.registry_metadata)
                gem_cache = {};
            next_gem_bootstrap = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        }
        const auto cached = gem_cache;
        gem_discovery = std::async(std::launch::async, [pid = pid, cached] { return find_gem_routes(pid, cached); });
    }
    for (auto it = gem_access.begin(); it != gem_access.end();)
    {
        try
        {
            it->second->update(desired_gems);
            ++it;
        }
        catch (const std::exception &error)
        {
            std::cerr << "Gem route " << it->first << ": " << error.what() << std::endl;
            gem_refresh.reset();
            it = gem_access.erase(it);
            gem_cache = {};
        }
    }
    if (!gem_refresh && (!gem_access.empty() || level_routing) && gem_cache.power_meta && tick.room_ready)
    {
        try
        {
            std::vector<GemRouteProbe> jobs;
            for (const auto &[name, access] : gem_access)
                jobs.push_back(access->refresh_job());
            gem_refresh = std::make_unique<GemRefreshAccess>(pid, gem_cache, jobs, desired_gems,
                                                             box_markers ? box_markers->tick() : 0);
            gem_refresh_pending = true;
            std::cout << "AP gem game-thread refresh attached.\n" << std::flush;
        }
        catch (const std::exception &error)
        {
            if (gem_error != error.what())
                std::cerr << "Gem refresh attachment: " << error.what() << std::endl;
            gem_error = error.what();
        }
    }
    if (gem_refresh)
    {
        try
        {
            gem_refresh->update(desired_gems);
        }
        catch (const std::exception &error)
        {
            std::cerr << "Gem refresh: " << error.what() << std::endl;
            gem_refresh.reset();
            gem_cache = {};
        }
    }
    if (ability_access)
    {
        try
        {
            if (!ability_access->update(desired_abilities))
            {
                ability_access.reset();
                gem_cache = {};
                ability_refresh_pending = true;
                next_gem_probe = std::chrono::steady_clock::now();
                std::cout << "Ability graph changed during loading. Hooks detached, rediscovering.\n" << std::flush;
            }
        }
        catch (const std::exception &error)
        {
            std::cerr << "Ability access: " << error.what() << std::endl;
            ability_access.reset();
            gem_cache = {};
        }
    }
    if (previous_abilities != desired_abilities)
    {
        ability_refresh_pending = true;
        std::cout << "AP ability ownership updated. Ability-cache refresh queued.\n" << std::flush;
        previous_abilities = desired_abilities;
    }
    if (ability_access && ability_refresh_pending)
    {
        try
        {
            if (ability_access->refreshed())
            {
                std::cout << "AP ability-cache refresh applied on the game thread.\n" << std::flush;
                ability_refresh_pending = false;
            }
        }
        catch (const std::exception &error)
        {
            std::cerr << "Ability refresh acknowledgement: " << error.what() << std::endl;
        }
    }
    if (tick.room_ready && ability_access && !ability_refresh_pending)
    {
        try
        {
            for (unsigned index = session.filler_cursor(); index < tick.items.size(); ++index)
            {
                const auto item = tick.items[index].first;
                if (item != 0xCB0000 + 2201 && item != 0xCB0000 + 2202)
                    continue;
                if (!ability_access->filler_ready() || ability_access->filler_pending())
                    break;
                const auto scene = read_scene(GetCurrentProcess());
                const auto level = level_key(scene.zone);
                if (!scene.camera_active || !level_accessible(*seed, tick.items, level))
                {
                    // say once per level why rewards are held back, so logs show it
                    static std::string noted;
                    const auto note = level.empty() ? std::string() : level;
                    if (!note.empty() && note != noted && scene.camera_active)
                        std::cout << "1-Up/Wumpa rewards wait: " << note
                                  << " counts as locked for this seed, so rewards arrive in the next unlocked level.\n"
                                  << std::flush;
                    noted = note;
                    break;
                }
                if (session.reserve_filler(index))
                {
                    ability_access->queue_filler(index, item, scene.zone_address);
                    std::cout << "Filler queued on the game thread: "
                              << (item == 0xCB0000 + 2201 ? "1-Up"
                                                          : std::to_string(wumpa_amount(index)) + " Wumpa Fruit")
                              << " (AP index " << index << ").\n"
                              << std::flush;
                }
                break; // apply rewards one at a time
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Filler delivery: " << e.what() << std::endl;
        }
    }
    // Aku Aku Mask filler: one mask each, in a level once Crash can move, a couple of seconds apart. It goes through with
    // Aku Aku Sanity too. In a boss fight one is enough (V1.3.13): the rest wait for the next level
    if (tick.room_ready && box_markers && std::chrono::steady_clock::now() >= next_aku_mask)
        try
        {
            for (unsigned index = session.aku_cursor(); index < tick.items.size(); ++index)
            {
                if (tick.items[index].first != 0xCB0000 + 2204)
                    continue;
                const auto scene = read_scene(GetCurrentProcess());
                if (!scene.camera_active || level_key(scene.zone).empty() || !box_markers->player_ready() || box_markers->aku_hold())
                    break;
                if (session.reserve_aku(index))
                {
                    box_markers->request_aku_give();
                    print_console("Aku Aku Mask!", true);
                    next_aku_mask = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                }
                break;
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Aku Aku Mask delivery: " << e.what() << std::endl;
        }
    // running traps pause outside levels: hubs, maps, menus and loading screens
    if (box_markers)
        try
        {
            const auto now = GetTickCount64();
            const auto scene = read_scene(GetCurrentProcess());
            const bool in_level = scene.camera_active && !level_key(scene.zone).empty();
            const auto elapsed = last_pause_tick && now > last_pause_tick ? std::min<std::uint64_t>(now - last_pause_tick, 5000) : 0;
            box_markers->pause_traps(!in_level, elapsed);
            last_pause_tick = now;
        }
        catch (const std::exception &)
        {
            last_pause_tick = 0;
        }
    // the TNT crate's countdown sound for the Nitro Trap, from the game's own sound banks: searched once, in the
    // background, when the in-game part is up; results print when the search ends
    if (box_markers && !sound_search_started)
    {
        sound_search_started = true;
        // the countdown volume picked with /nitro_volume
        if (std::ifstream volume{standalone::state_directory() / "nitro_volume.txt"}; volume)
            if (unsigned percent = 0; volume >> percent && percent <= 100)
                ingame::set_countdown_volume(percent);
        if (const auto skin = skins::status(); !skin.empty())
            print_console(skin, false);
        if (const auto colors = crash_colors::patch_status(); !colors.empty())
            print_console(colors, false);
        start_sound_job([] { return ingame::startup_sound_search(standalone::state_directory() / "tnt_sound.txt"); });
    }
    if (sound_job.valid() && sound_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        try
        {
            for (const auto &line : sound_job.get())
                print_console(line, false);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Sound search: " << e.what() << std::endl;
        }
    if (box_markers)
        if (const auto note = box_markers->take_ice_note(); !note.empty())
            print_console(note, true);
    // the in-game tuner found a better Slow tilt: keep it as /slow_tilt so the next session starts there
    if (box_markers)
        try
        {
            const auto tuned = box_markers->tuned_slow_tilt();
            if (ingame::valid_slow_tilt(tuned) && std::abs(tuned - slow_tilt) > 0.004f)
            {
                slow_tilt = tuned;
                auto config = std::filesystem::exists(display_file) ? read_state_json(display_file) : Json::Value(Json::objectValue);
                config["slow_tilt"] = tuned;
                std::filesystem::create_directories(display_file.parent_path());
                write_state_json(display_file, config);
                box_markers->slow_tilt(tuned);
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Slow tilt save: " << e.what() << std::endl;
        }
    // traps fire one at a time, in any level (not hubs or menus) with the camera active, trap_gap_ms apart.
    // unlike 1-Ups they do not wait for an AP-unlocked level.
    // each is saved as applied before it starts, so reconnecting never replays it.
    const auto trap_seconds = [&] {
        const auto &duration = session.slot_data()["trap_duration"];
        return duration.isUInt() && duration.asUInt() >= 10 && duration.asUInt() <= 120 ? duration.asUInt() : 30u;
    };
    // Move Lock locks Spin, Body Slam, Crouch Jump and Slide: with Ability Shuffle it waits until Crash has one of them
    const auto lockable_move = [&] {
        const auto &ability_shuffle = session.slot_data()["ability_shuffle"];
        if (!ability_shuffle.isIntegral() || ability_shuffle.asInt64() <= 0)
            return true;
        for (const auto &item : tick.items)
            for (unsigned i = 0; i < 4; ++i)
                if (item.first == ingame::move_item(i))
                    return true;
        return false;
    };
    if (tick.room_ready && box_markers && seed && std::chrono::steady_clock::now() >= next_trap)
    {
        try
        {
            for (unsigned index = session.trap_cursor(); index < tick.items.size(); ++index)
            {
                const auto kind = ingame::trap_kind(tick.items[index].first);
                if (kind < 0)
                    continue;
                const auto scene = read_scene(GetCurrentProcess());
                const auto level = level_key(scene.zone);
                // traps start once Crash can move, and has for 1.5 seconds: not during the level intro, a death or a respawn
                if (!scene.camera_active || level.empty() || !box_markers->player_settled())
                    break;
                // a Nitro Trap joins the waiting count and fires when the game has a Nitro to borrow
                if (kind == ingame::TrapNitro)
                {
                    if (session.defer_nitro(index) && !box_markers->nitro_ready())
                        print_console("Nitro Trap incoming! It strikes as soon as Crash is in a level where it can.", true);
                    continue;
                }
                if (kind == ingame::TrapMoveLock && !lockable_move())
                {
                    if (session.defer_move_lock(index))
                        print_console("Move Lock Trap incoming! It strikes once you have a move to lock.", true);
                    continue;
                }
                // Life Loss uses the game's lives reward, which must be attached and idle
                if (kind == ingame::TrapLifeLoss &&
                    (!ability_access || ability_refresh_pending || !ability_access->filler_ready() ||
                     ability_access->filler_pending() || !scene.zone_address))
                    break;
                if (session.reserve_trap(index))
                {
                    const unsigned seconds = trap_seconds();
                    std::string after;
                    bool already = false;
                    if (kind == ingame::TrapLifeLoss)
                        ability_access->queue_life_loss(index, scene.zone_address);
                    else
                        after = box_markers->apply_trap(static_cast<unsigned>(kind), seconds, &already);
                    const auto timed = ingame::timed_trap(static_cast<unsigned>(kind));
                    // Slow, Slippery and Reverse take turns: a second one starts when the first ends. One of a kind
                    // already running adds no time.
                    print_console(already ? std::string(ingame::trap_names[kind]) + " again! It's already on, so it keeps its time."
                                          : std::string(ingame::trap_names[kind]) + (timed ? " for " + std::to_string(seconds) + " seconds" : "") +
                                                (after.empty() ? "!" : ", once the " + after + " ends!"),
                                  true);
                    next_trap = std::chrono::steady_clock::now() + std::chrono::milliseconds(ingame::trap_gap_ms);
                }
                break; // one trap per pass
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Trap delivery: " << e.what() << std::endl;
        }
    }
    // waiting Move Lock Traps fire once Crash has a move to lock, in the trap rhythm
    if (tick.room_ready && box_markers && session.ready() && session.move_lock_pending() && std::chrono::steady_clock::now() >= next_trap &&
        lockable_move())
        try
        {
            const auto scene = read_scene(GetCurrentProcess());
            if (scene.camera_active && !level_key(scene.zone).empty() && box_markers->player_settled())
            {
                session.take_move_lock();
                bool already = false;
                box_markers->apply_trap(ingame::TrapMoveLock, trap_seconds(), &already);
                print_console(already ? std::string("Move Lock Trap again! It's already on, so it keeps its time.")
                                      : "Move Lock Trap for " + std::to_string(trap_seconds()) + " seconds!",
                              true);
                next_trap = std::chrono::steady_clock::now() + std::chrono::milliseconds(ingame::trap_gap_ms);
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Move Lock Trap: " << e.what() << std::endl;
        }
    // waiting Nitro Traps fire one at a time, in the trap rhythm, once the game reports a Nitro can spawn here
    if (box_markers)
        box_markers->nitro_wanted(session.ready() && session.nitro_pending());
    if (tick.room_ready && box_markers && session.ready() && session.nitro_pending() && std::chrono::steady_clock::now() >= next_trap)
        try
        {
            // every Nitro Trap starts with "NITRO INCOMING 3, 2, 1" at a TNT crate's tempo; then the game thread flips a
            // coin: a Nitro appears ahead of Crash, or one falls onto him (if he isn't moving when it should appear
            // ahead, he gets a moment to start, then it falls instead)
            const auto scene = read_scene(GetCurrentProcess());
            const auto now = std::chrono::steady_clock::now();
            if (scene.camera_active && !level_key(scene.zone).empty() && box_markers->player_settled() && box_markers->nitro_drop_ready())
            {
                session.take_nitro();
                box_markers->request_nitro_drop(0);
                print_console("Nitro Trap! NITRO INCOMING!", true);
                next_trap = now + std::chrono::milliseconds(ingame::trap_gap_ms);
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Nitro Trap: " << e.what() << std::endl;
        }
    if (previous_gems != desired_gems)
    {
        gem_refresh_pending = true;
        std::cout << "AP colored-gem ownership updated. Live route refresh queued.\n" << std::flush;
        previous_gems = desired_gems;
    }
    if (gem_refresh && gem_refresh_pending)
    {
        try
        {
            if (gem_refresh->refreshed())
            {
                std::cout << "AP gem refresh acknowledged on the game thread: " << gem_refresh->applied()
                          << " active components refreshed (" << gem_refresh->diagnostics() << ").\n";
                if (!gem_refresh->applied())
                    std::cout << "No matching live gem component refreshed. Future components use game activation.\n";
                std::cout << std::flush;
                gem_refresh_pending = false;
            }
        }
        catch (const std::exception &error)
        {
            std::cerr << "Gem refresh acknowledgement: " << error.what() << std::endl;
        }
    }
}
