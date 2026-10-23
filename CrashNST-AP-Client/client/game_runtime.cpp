#include "game_runtime.h"
#include "engine_memory.h"
#include "function_hook.h"
#include "movement_rules.h"
#include "trap_rules.h"
#include "game_sound.h"
#include "game_pickups.h"
#include "game_memory.h"
#include "crate_specs.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <atomic>
#include <mutex>
#include <memory>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <set>
#include <deque>
#include <random>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <string_view>
#include <cctype>
#include <map>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#include <tlhelp32.h>
#include <cstdio>

namespace ingame
{
    unsigned hub_campaign(std::string zone)
    {
        std::transform(zone.begin(), zone.end(), zone.begin(), [](unsigned char c)
                       { return std::tolower(c); });
        if (zone.rfind("maps/", 0) == 0)
            zone.erase(0, 5);
        for (unsigned i = 1; i <= 3; ++i)
        {
            const auto campaign = std::to_string(i), hub = "l" + campaign + "00_hub";
            if (zone == "crash" + campaign + "/" + hub + "/" + hub)
                return i;
        }
        return 0;
    }
    std::string level_key(std::string zone)
    {
        std::transform(zone.begin(), zone.end(), zone.begin(), [](unsigned char c)
                       { return std::tolower(c); });
        if (zone.rfind("maps/", 0) == 0)
            zone.erase(0, 5);
        const auto slash = zone.find('/');
        if (slash == std::string::npos || zone.size() < slash + 6)
            return {};
        auto key = zone.substr(slash + 1, 4);
        if ((key[0] != 'l' && key[0] != 'b') || key.substr(1, 3) == "100" || key.substr(1, 3) == "200" || key.substr(1, 3) == "300")
            return {};
        key[0] = static_cast<char>(std::toupper(key[0]));
        return key;
    }
    namespace
    {
        using namespace engine_memory;
        FunctionHook hub_bool_hook, hub_int_hook, hub_gate_query_hook;
        std::atomic<unsigned> hub_areas{0x7fff};

        void hub_gate_query(Address context, Address args, Address outputs)
        {
            const auto instance = value<Address>(context);
            const auto meta = value<Address>(instance + 56);
            const auto inputs = value<Address>(args + 16);
            if (typed(instance, 0x13bbbd0) && typed(meta, 0x1745d78) &&
                string(value<Address>(meta + 16)) == "Scripts.Graph.L300_Hub_C3_Hub_ElectricGate" &&
                value<Address>(inputs) == meta - 23764 + 20316 &&
                value<Address>(inputs + 8) == meta - 23764 + 20508 &&
                typed(meta - 23764 + 20316, 0x1747910) && typed(meta - 23764 + 20508, 0x1749820) &&
                value<unsigned>(meta - 23764 + 20508 + 24) == 0x800)
            {
                const auto branches = list(outputs, 2);
                const auto owner = value<Address>(instance + 24);
                const auto name = string(value<Address>(owner + 128));
                const bool known = typed(owner, 0x141d490) && name.size() == 21 &&
                                   name.compare(0, 20, "C3_Hub_ElectricGate0") == 0 && name[20] >= '2' && name[20] <= '5';
                const bool unlocked = known && (hub_areas.load(std::memory_order_relaxed) & (1u << (10 + name[20] - '1')));
                // OnStart otherwise disables gates from saved boss rewards before the contact adapter attaches
                if (branches.size() == 2)
                    reinterpret_cast<void (*)(Address, Address)>(base() + 0xcb6db0)(context, branches[unlocked ? 0 : 1]);
                return;
            }
            reinterpret_cast<void (*)(Address, Address, Address)>(hub_gate_query_hook.original)(context, args, outputs);
        }

        unsigned hub_islands()
        {
            const auto mask = hub_areas.load(std::memory_order_relaxed);
            return (mask & 6) == 6 ? 3 : (mask & 2) ? 2 : 1;
        }

        bool hub_bool_field(Address accessor, Address context)
        {
            // getter arguments and their field metadata are owned by the engine for this call
            const auto field = *reinterpret_cast<const Address *>(accessor + 24);
            const auto instance = *reinterpret_cast<const Address *>(context);
            if (field && instance && *reinterpret_cast<const Address *>(instance) == base() + 0x13bbbd0)
            {
                const auto meta = *reinterpret_cast<const Address *>(instance + 56);
                const auto name = meta ? *reinterpret_cast<const char *const *>(meta + 16) : nullptr;
                // compiled C1 accessor descriptors omit names. match the verified descriptor and instance offset.
                if (name && !std::strcmp(name, "Scripts.Graph.common_Crash1_Hub_Manager") &&
                    field == meta - 142276 + 117340 && *reinterpret_cast<const unsigned short *>(field + 24) == 64)
                    return false;
                const auto offset = *reinterpret_cast<const unsigned short *>(field + 24);
                // +224/+225 cache HUD choices and must still clear when the player leaves the lift
                if (name && (offset == 161 || offset == 162) &&
                    !std::strcmp(name, "Scripts.Graph.common_Crash2_Hub_WarpLift"))
                {
                    const auto entity = *reinterpret_cast<const Address *>(instance + 24);
                    const auto owner = entity ? *reinterpret_cast<const char *const *>(entity + 128) : nullptr;
                    if (owner && !std::strncmp(owner, "Floor0", 6) && owner[6] >= '1' && owner[6] <= '5' &&
                        !std::strcmp(owner + 7, "_WarpLift"))
                    {
                        const auto floor = unsigned(owner[6] - '0');
                        const auto area = offset == 161 ? floor + 1 : floor - 1;
                        return area >= 1 && area <= 5 && (hub_areas.load(std::memory_order_relaxed) & (1u << (area + 4)));
                    }
                }
            }
            return reinterpret_cast<bool (*)(Address, Address)>(hub_bool_hook.original)(accessor, context);
        }

        int hub_int_field(Address accessor, Address context)
        {
            const auto original = reinterpret_cast<int (*)(Address, Address)>(hub_int_hook.original)(accessor, context);
            const auto field = *reinterpret_cast<const Address *>(accessor + 24);
            const auto instance = *reinterpret_cast<const Address *>(context);
            if (field && instance && *reinterpret_cast<const Address *>(instance) == base() + 0x13bbbd0)
            {
                const auto meta = *reinterpret_cast<const Address *>(instance + 56);
                const auto name = meta ? *reinterpret_cast<const char *const *>(meta + 16) : nullptr;
                const auto offset = *reinterpret_cast<const unsigned short *>(field + 24);
                // Only this navigation comparison uses the count. The separate initialization count must stay complete.
                if (name && !std::strcmp(name, "Scripts.Graph.common_Crash1_Hub_Manager") &&
                    field == meta - 142276 + 120244 && offset == 152)
                    return std::min(original, static_cast<int>(hub_islands()));
                // Reject a direction in the input router, before it changes map indices or starts the camera.
                if (name && !std::strcmp(name, "Scripts.Graph.common_Crash1_Hub_Manager") &&
                    field == meta - 142276 + 145340 && offset == 176)
                {
                    const auto island = *reinterpret_cast<const int *>(instance + 192);
                    const auto spot = *reinterpret_cast<const int *>(instance + 208);
                    const auto end = *reinterpret_cast<const int *>(instance + 204);
                    const auto islands = static_cast<int>(hub_islands());
                    const auto stormy = (original == 5 || original == 6) &&
                        string(value<Address>(value<Address>(instance + 72) + 128)) == "Isle01_MapSpot_00_StormyTeleport";
                    // direction 4 takes the engine's no-move/input-resume branch. counts used by initialization stay intact.
                    if (island < 0 || island > 2 ||
                        (original == 1 && (spot < 0 || end <= 0 || end > 64 || (spot >= end - 1 && island + 1 >= islands))) ||
                        (island == 0 && islands < 3 && ((original == 0 && spot <= 0) || stormy)))
                        return 4;
                }
            }
            return original;
        }
        bool event_owner(Address entity)
        {
            return entity && (typed(entity, 0x1592220) || typed(entity, 0x14c8ef8) || typed(entity, 0x14c7fb0) || typed(entity, 0x141d490)) && (value<unsigned>(entity + 56) & 4);
        }
        std::vector<Address> event_owners()
        {
            // context+120 maps metadata keys (+40) to parallel instance lists (+24). select CEntityEvents.
            const auto context = value<Address>(base() + 0x1ac1dd8), meta = value<Address>(base() + 0x1a744a0);
            const auto table = value<Address>(context + 120);
            const auto bytes = value<std::uint64_t>(table + 16) & 0xfffffffffffffULL;
            if (!context || !meta || !table || bytes != (value<std::uint64_t>(table + 32) & 0xfffffffffffffULL) || bytes > 65536 * 8 || bytes % 8)
                return {};
            const auto keys = value<Address>(table + 40), lists = value<Address>(table + 24);
            Address events = 0;
            for (unsigned i = 0; i < bytes / 8; ++i)
                if (value<Address>(keys + i * 8) == meta)
                {
                    events = value<Address>(lists + i * 8);
                    break;
                }
            std::vector<Address> result;
            for (const auto event : list(events, 16384))
                if (typed(event, 0x14cb600))
                {
                    const auto actor = value<Address>(event + 24);
                    if (event_owner(actor))
                        result.push_back(actor);
                }
            return result;
        }

        FunctionHook ui_audio_hook;
        std::atomic<Address> permanent_audio_pool{0};
        bool shared_ui_audio(Address path)
        {
            std::array<char, 256> bytes{};
            if (!read(path, bytes))
                return false;
            const auto end = static_cast<char *>(std::memchr(bytes.data(), 0, bytes.size()));
            if (!end)
                return false;
            std::transform(bytes.data(), end, bytes.data(), [](unsigned char c) { return std::tolower(c); });
            std::string_view name(bytes.data(), end - bytes.data());
            // loaders can receive archive paths before the engine expands their virtual-directory aliases
            if (name.rfind("sounds:", 0) == 0)
                name.remove_prefix(7);
            else if (name.rfind("sounds/", 0) == 0)
                name.remove_prefix(6);
            for (const auto bank : {"/banks/ui_bank_c1_bank.igz", "/banks/ui_bank_c2_bank.igz", "/banks/ui_bank_c3_bank.igz"})
                if (name == bank)
                    return true;
            if (name.rfind("data:/", 0) == 0)
                name.remove_prefix(6);
            if (name.rfind("sound_samples/", 0) == 0)
                for (const auto bank : {",ui_bank_c1_bank,", ",ui_bank_c2_bank,", ",ui_bank_c3_bank,"})
                    if (name.find(bank) != name.npos)
                        return true;
            return false;
        }
        Address load_ui_audio(Address path, Address pool, unsigned flags)
        {
            if (shared_ui_audio(path))
            {
                auto permanent = permanent_audio_pool.load();
                if (!permanent)
                {
                    // the streaming manager's inline chunk vector has count +24, byte capacity +32 and entries +40
                    const auto manager = value<Address>(base() + 0x1aa9eb8);
                    const auto count = value<unsigned>(manager + 24);
                    const auto capacity = value<std::uint64_t>(manager + 32) & 0xfffffffffffffULL;
                    if (typed(manager, 0x1616f60) && count <= 4096 && capacity >= count * 8)
                        for (unsigned i = 0; i < count; ++i)
                        {
                            const auto chunk = value<Address>(value<Address>(manager + 40) + i * 8);
                            if (typed(chunk, 0x1615820) && string(value<Address>(chunk + 16)) == "permanent_pkg")
                            {
                                permanent = value<Address>(chunk + 96);
                                if (!typed(permanent, 0x17a2808))
                                    permanent = 0;
                                permanent_audio_pool = permanent;
                                break;
                            }
                        }
                }
                // C2's hub shares C3 UI audio. a level pool reset would invalidate the persistent campaign package.
                if (permanent)
                    pool = permanent;
            }
            return reinterpret_cast<Address (*)(Address, Address, unsigned)>(ui_audio_hook.original)(path, pool, flags);
        }
        FunctionHook license_hook;
        void license_query(unsigned key, unsigned char *result)
        {
            // option 460 is license acceptance. inheritance changes only this key in the verified game-state object.
            reinterpret_cast<void (*)(unsigned, unsigned char *)>(license_hook.original)(key, result);
            if (key != 460 || !result || *result)
                return;
            const auto state = value<Address>(value<Address>(base() + 0x1a69a98) + 32);
            if (typed(state, 0x155e730))
            {
                reinterpret_cast<void (*)(Address, unsigned, unsigned char)>(base() + 0x4fdb50)(state, 460, 1);
                *result = 1;
            }
        }
        bool special_level(const std::string &key)
        {
            static const std::vector<std::string> levels{"L107", "L114", "L208", "L213", "L215", "L222", "L224", "L226",
                                                         "L302", "L303", "L305", "L308", "L310", "L312", "L314", "L317", "L318", "L322", "L324", "L326", "L328", "L330", "L331", "L332"};
            return std::find(levels.begin(), levels.end(), key) != levels.end();
        }
        // traps treat the Crash 2 jet-board levels (Hang Eight, Air Crash, Plant Food) like gimmick levels too
        bool trap_gimmick_level(const std::string &key)
        {
            return special_level(key) || key == "L203" || key == "L207" || key == "L211";
        }
        // gimmick levels the Nitro Trap still strikes in (V1.2.46): the hog levels (Hog Wild, Whole Hog), the Crash 2
        // jet-board levels (Hang Eight, Air Crash, Plant Food) and polar bear levels (Bear It, Bear Down, Un-Bearable,
        // Totally Bear), and since V1.2.73 the scuba levels (Under Pressure, Deep Trouble), where it only ever appears
        // ahead of Crash (see nitro_ahead_only). The chase levels (boulders, bear, dinosaur) were never gimmick levels.
        // Off: jetpack, motorcycle, plane, Coco's jet ski (off since V1.2.73), Coco's tiger and Baby T levels.
        bool nitro_gimmick_ok(const std::string &key)
        {
            static const std::set<std::string> ok{"L107", "L114", "L203", "L207", "L211", "L208", "L213", "L215", "L226", "L302", "L312"};
            return ok.count(key) != 0;
        }
        // the scuba levels: the Nitro Trap's Nitro only appears ahead of Crash there, floating in the water (a Nitro
        // dropping onto a diver makes no sense), and it waits for a spot ahead as long as it takes
        bool nitro_ahead_only_level(const std::string &key)
        {
            return key == "L302" || key == "L312";
        }
        std::mutex movement_mutex;
        std::map<std::string, unsigned> movement_styles;
        std::atomic_bool vanilla_movement{false};
        // Ability Shuffle state copied from the shared block every gameplay update
        std::atomic_bool move_shuffle{false};
        std::atomic_uint owned_moves{MoveAll};
        std::mutex move_log_mutex;
        std::set<std::string> logged_attacks;
        bool gimmick_level()
        {
            // hog, polar bear, jetpack and vehicle stages keep vanilla controls, as with Crashsanity
            return special_level(level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184))));
        }
        // Move Lock Trap end time (GetTickCount64). F10 does not lift it; it simply runs out.
        std::atomic<std::uint64_t> move_lock_until{0};
        bool move_lock_active()
        {
            return timer_active(move_lock_until, GetTickCount64()) &&
                   !trap_gimmick_level(level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184))));
        }
        bool crouch_locked()
        {
            return move_blocked(MoveCrouch, move_shuffle, vanilla_movement, owned_moves) && !gimmick_level();
        }
        FunctionHook movement_check_hook, movement_input_hook, movement_attack_hook, movement_skill_hook;
        Address missing_crouch_actor = 0, movement_input_code = 0;
        std::atomic<Address> blocked_crouch_controller{0};
        unsigned assigned_movement()
        {
            const auto world = value<Address>(base() + 0x1a64690);
            const auto key = level_key(string(value<Address>(world + 184)));
            if (special_level(key))
                return 0;
            std::lock_guard lock(movement_mutex);
            const auto found = movement_styles.find(key);
            return found == movement_styles.end() ? 0 : found->second;
        }
        unsigned scene_campaign()
        {
            const auto world = value<Address>(base() + 0x1a64690);
            return world ? reinterpret_cast<unsigned (*)(Address)>(base() + 0x131400)(world) : 0;
        }
        unsigned effective_movement()
        {
            return movement_style(assigned_movement(), scene_campaign(), vanilla_movement);
        }
        std::uint64_t movement_attack(Address component, Address input, unsigned char ignore_blocks)
        {
            const auto selected = reinterpret_cast<std::uint64_t (*)(Address, Address, unsigned char)>(movement_attack_hook.original)(component, input, ignore_blocks);
            if (selected < 4)
            {
                const auto style = effective_movement();
                const bool lock = move_lock_active();
                const bool moves = (move_shuffle && !vanilla_movement && !gimmick_level()) || lock;
                const auto actor = value<Address>(component + 24);
                if ((style == 1 || moves) && typed(component, 0x1427ae0) && actor && actor == player() && typed(value<Address>(actor + 424), 0x1589128))
                {
                    // filter pressed and buffered slides before the attack state or animation changes
                    const auto event = reinterpret_cast<Address (*)(Address, unsigned)>(base() + 0x47cfa0)(component, static_cast<unsigned>(selected));
                    const auto action = string(event);
                    if (moves)
                    {
                        const auto move = attack_move(action);
                        {
                            // record each attack name once so testers can confirm which one is the spin
                            std::lock_guard lock(move_log_mutex);
                            if (logged_attacks.insert(action).second)
                                std::cout << "Ability Shuffle: attack \"" << action << "\" -> "
                                          << (move == MoveSpin ? "Spin" : move == MoveSlide ? "Slide" : move == MoveBodySlam ? "Body Slam" : "not shuffled")
                                          << std::endl;
                        }
                        // a Move Lock Trap blocks every shuffled attack; Ability Shuffle blocks unowned ones
                        if (move && (lock || move_blocked(move, move_shuffle, vanilla_movement, owned_moves)))
                            return 4;
                    }
                    if (style == 1)
                        return movement_attack_selection(static_cast<unsigned>(selected), style, action);
                }
            }
            return selected;
        }
        unsigned char movement_skill(Address filter, Address actor)
        {
            const auto original = reinterpret_cast<unsigned char (*)(Address, Address)>(movement_skill_hook.original)(filter, actor);
            if (typed(filter, 0x1496238) && actor && actor == player() && typed(value<Address>(actor + 424), 0x1589128))
            {
                // OButton is the base slide/body-slam skill. AP power skills retain their original ownership checks.
                const auto skill = resolve(value<Address>(filter + 24));
                if (typed(skill, 0x15ad140))
                    return movement_skill_filter(original != 0, effective_movement(), value<unsigned>(filter + 16),
                                                string(value<Address>(skill + 24)));
            }
            return original;
        }
        void movement_check(Address context, Address args, Address outputs)
        {
            const auto accessor = value<Address>(value<Address>(args + 16));
            bool crouch = false, jump = false;
            if (typed(accessor, 0x1749820) && value<unsigned>(accessor + 24) == 1)
                for (const auto &[delta, name] : std::array<std::pair<unsigned, const char *>, 3>{{
                         {824, "Scripts.Graph.Crash_CrouchCrawlSlide"},
                         {3856, "Scripts.Graph.CrashBandicoot_JumpFlip"},
                         {3104, "Scripts.Graph.CrashBandicoot_JumpFlip"}}})
                {
                    // Slab distances identify these three graph constants without changing shared script data.
                    const auto meta = accessor + delta;
                    if (typed(meta, 0x1745d78) && string(value<Address>(meta + 16)) == name)
                    {
                        crouch = delta == 824;
                        jump = !crouch;
                        break;
                    }
                }
            const auto branches = (crouch || jump) ? list(outputs, 2) : std::vector<Address>{};
            if (branches.size() == 2)
            {
                // OnStart removes crouch in Crash 1. retain it before initialization for either advanced style
                // retaining it across F10 switches is safe because the input hook blocks Crash 1 crouch
                // Ability Shuffle removes crouch and its high jump the same way until Crouch Jump is found
                const bool matches = crouch ? !movement_needs_crouch(assigned_movement(), scene_campaign()) || crouch_locked()
                                            : effective_movement() == 1;
                reinterpret_cast<void (*)(Address, Address)>(base() + 0xcb6db0)(context, branches[matches ? 0 : 1]);
                return;
            }
            reinterpret_cast<void (*)(Address, Address, Address)>(movement_check_hook.original)(context, args, outputs);
        }
        Address last_actor = 0;
        std::atomic_bool remote_death{false};
        std::atomic<Address> death_handle{0};
        std::atomic_uint observed_deaths{0};
        // deaths not yet taken off the trap timers (trap_death_mercy_ms each)
        std::atomic_uint mercy_deaths{0};
        // when PlayerDied last turned on
        std::atomic<std::uint64_t> died_flag_at{0};
        FunctionHook death_hook;
        // research: every graph event the game sends goes through +0x767da0 (DeathLink sends one through it too)
        FunctionHook event_hook;
        std::uint64_t remote_deadline = 0;
        Address bound_player_died()
        {
            const auto handle = death_handle.load(), variable = resolve(handle);
            if ((value<unsigned>(handle) & 0xf0000000) != 0x40000000 ||
                string(value<Address>(handle + 8)) != "PlayerDied" || string(value<Address>(handle + 24)) != "gamevariables" ||
                !typed(variable, 0x155ebf0) || value<unsigned>(variable + 16) > 2)
                return 0;
            return variable;
        }
        // Crash_DamageManager's BonusRoundActive game variable: on from a bonus round's start until it ends
        std::atomic<Address> bonus_round_variable{0};
        // ... and TimeTrialActive: on during a time trial run (deaths there, and restarts, aren't sent)
        std::atomic<Address> time_trial_variable{0};
        // the last time the pause menu was open: a "death" right after it is the menu's Restart
        std::atomic<std::uint64_t> menu_seen_at{0};
        // Aku watch (research for an Aku Aku filler): the AkuAkuCount and WumpaCount game variables; their first
        // bytes are logged whenever they change, to see where the game keeps the mask count
        std::atomic<Address> aku_variable{0}, wumpa_variable{0};
        // the variable's first 0x100 bytes, then 0x80 bytes where its +0x28 points (its value may live there)
        using AkuWords = std::array<std::uint64_t, 48>;
        bool read_aku_words(Address variable, AkuWords &out)
        {
            out = {};
            std::array<std::uint64_t, 32> head{};
            if (!read(variable, head))
                return false;
            std::copy(head.begin(), head.end(), out.begin());
            std::array<std::uint64_t, 16> tail{};
            const auto external = static_cast<Address>(head[5]);
            if (external > 0x10000 && external < 0x7ffffffeffffULL && read(external, tail))
                std::copy(tail.begin(), tail.end(), out.begin() + 32);
            return true;
        }
        void update_aku_watch()
        {
            static AkuWords last_aku{}, last_wumpa{};
            static Address last_aku_at = 0, last_wumpa_at = 0;
            const auto watch = [](const char *name, Address variable, AkuWords &last, Address &last_at)
            {
                if (!variable)
                    return;
                AkuWords now{};
                if (!read_aku_words(variable, now))
                    return;
                if (variable != last_at)
                {
                    std::ostringstream out;
                    out << "Aku watch: " << name << " is 0x" << std::hex << variable << " (type +0x" << (now[0] - base()) << "):";
                    for (const auto w : now)
                        out << " " << w;
                    std::cout << out.str() << std::dec << "\n" << std::flush;
                    last_at = variable, last = now;
                    return;
                }
                if (now == last)
                    return;
                std::ostringstream out;
                out << "Aku watch: " << name << " changed:";
                for (unsigned i = 1; i < now.size(); ++i)
                    if (now[i] != last[i])
                    {
                        float f[2];
                        std::memcpy(f, &now[i], 8);
                        out << (i < 32 ? " +0x" : " ext+0x") << std::hex << (i < 32 ? i * 8 : (i - 32) * 8) << " 0x" << last[i] << " -> 0x" << now[i]
                            << std::dec << " (int " << static_cast<int>(last[i] & 0xffffffff) << " -> " << static_cast<int>(now[i] & 0xffffffff)
                            << ", float " << f[0] << ")";
                    }
                std::cout << out.str() << "\n" << std::flush;
                last = now;
            };
            watch("AkuAkuCount", aku_variable.load(), last_aku, last_aku_at);
            watch("WumpaCount", wumpa_variable.load(), last_wumpa, last_wumpa_at);
        }
        bool bonus_round_active()
        {
            const auto variable = bonus_round_variable.load();
            return variable && typed(variable, 0x155ebf0) && reinterpret_cast<unsigned char (*)(Address)>(base() + 0x4e5c60)(variable);
        }
        bool time_trial_active()
        {
            const auto variable = time_trial_variable.load();
            return variable && typed(variable, 0x155ebf0) && reinterpret_cast<unsigned char (*)(Address)>(base() + 0x4e5c60)(variable);
        }
        void record_death_change(bool before, bool after)
        {
            if (before == after)
                return;
            // every death counts for the trap mercy, sent or not (a received DeathLink, a bonus round)
            if (after)
                ++mercy_deaths, died_flag_at = GetTickCount64();
            if (!after)
            {
                remote_death = false;
                return;
            }
            if (remote_death.exchange(false))
                return; // a received DeathLink (or Death Trap) killed him: not sent back
            // a bonus round death only ends the bonus round (no life lost), so it isn't sent to anyone
            if (bonus_round_active())
            {
                std::cout << "DeathLink: died in a bonus round, not sent.\n" << std::flush;
                return;
            }
            // time trials: a death (or a restart) there only costs the run
            if (time_trial_active())
            {
                std::cout << "DeathLink: died in a time trial, not sent.\n" << std::flush;
                return;
            }
            // Restart in the pause menu runs the death script too
            if (GetTickCount64() - menu_seen_at.load() < 2500)
            {
                std::cout << "DeathLink: Restart from the pause menu, not sent.\n" << std::flush;
                return;
            }
            ++observed_deaths;
        }
        // death watch (research: DeathLink in bonus rounds). The game variables Crash_DamageManager uses, by name, and
        // every change of a yes/no game variable while in a level go to the log, so a bonus-round death shows which
        // one the game sets instead of PlayerDied.
        struct WatchedVariable
        {
            std::atomic<Address> variable{0};
            char name[48]{};
        };
        WatchedVariable death_watch[32];
        std::atomic_uint death_watch_count{0}, death_watch_unknown{0};
        void death_watch_log(Address variable, bool before, bool after)
        {
            const auto count = std::min<unsigned>(death_watch_count.load(), 32);
            for (unsigned i = 0; i < count; ++i)
                if (death_watch[i].variable.load() == variable)
                {
                    std::cout << "Death watch: " << death_watch[i].name << ": " << before << " -> " << after << "\n" << std::flush;
                    return;
                }
            if (death_watch_unknown.fetch_add(1) >= 400)
                return;
            std::ostringstream out;
            out << "Death watch: game variable 0x" << std::hex << variable << " (type +0x" << (value<Address>(variable) - base()) << std::dec;
            for (unsigned off = 8; off <= 48; off += 8)
                if (const auto text = string(value<Address>(variable + off), 48);
                    text.size() >= 3 && std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 32 && c < 127; }))
                    out << ", +" << off << " \"" << text << "\"";
            out << "): " << before << " -> " << after << "\n";
            std::cout << out.str() << std::flush;
        }
        void player_died_set(Address variable, unsigned char requested)
        {
            const bool in_level = !level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184))).empty();
            const bool tracked = variable && variable == bound_player_died() && in_level;
            const auto getter = reinterpret_cast<unsigned char (*)(Address)>(base() + 0x4e5c60);
            const bool watched = variable && in_level;
            const bool before = watched && getter(variable);
            reinterpret_cast<void (*)(Address, unsigned char)>(death_hook.original)(variable, requested);
            if (watched)
                if (const bool after = getter(variable) != 0; after != before)
                    death_watch_log(variable, before, after);
            if (tracked && variable == bound_player_died())
                record_death_change(before, getter(variable) != 0);
        }
        // Aku Aku Sanity: Crash_DamageManager's NoAkuAkuMasks game variable (the game's own "no Aku Aku masks" switch) is
        // turned on while masks are locked, and off again when they unlock, only if the client turned it on
        std::atomic<Address> no_aku_variable{0};
        // The Aku Aku lock and a mask already out: the game keeps the mask count in Crash_DamageManager's AkuAkuCount
        // number variable. While the lock is on and Aku still shows, it is set to 0 with the game's own number setter
        // (+0x502200: the number variables' "reset" entry, +0x4fb420, jumps there with its default value, just as the
        // yes/no variables' one jumps to their setter +0x502000), a few tries per Crash, each logged.
        void clear_aku_count(Address actor, bool shown)
        {
            static Address tried_for = 0;
            static unsigned tries = 0;
            static std::uint64_t last_try = 0;
            static const bool verified = [] {
                // number "reset": mov edx,[rcx+0x18]; jmp +0x502200
                if (!code_matches(0x4fb420, std::array<unsigned char, 8>{0x8b, 0x51, 0x18, 0xe9, 0xd8, 0x6d, 0x00, 0x00}))
                    return false;
                // the yes/no "reset" next to it must jump to the yes/no setter the client already uses
                std::array<unsigned char, 16> code{};
                if (!read(base() + 0x4fb400, code))
                    return false;
                for (unsigned at = 2; at <= 5; ++at)
                    if (code[at] == 0xe9)
                    {
                        std::int32_t rel = 0;
                        std::memcpy(&rel, &code[at + 1], 4);
                        if (0x4fb400 + at + 5 + rel == 0x502000)
                            return true;
                    }
                return false;
            }();
            const auto variable = aku_variable.load();
            if (actor != tried_for)
                tried_for = actor, tries = 0;
            const auto now = GetTickCount64();
            if (!shown || !verified || !variable || !typed(variable, 0x155e9f8) || tries >= 3 || now - last_try < 2000)
                return;
            ++tries, last_try = now;
            reinterpret_cast<void (*)(Address, int)>(base() + 0x502200)(variable, 0);
            std::cout << "Aku Aku lock: Aku still shows; set AkuAkuCount to 0 (try " << tries << " of 3)\n" << std::flush;
        }
        // the Aku Aku lock's message filter (event_sent) is on while the lock is
        std::atomic_bool aku_messages_blocked{false};
        // set while the client itself hands Aku a mask (/aku_give, the Aku Aku filler): that message goes through
        std::atomic_bool own_aku_message{false};
        // after /aku_give: when to look at Aku Aku's manager again, to log whether a mask came out
        std::atomic<std::uint64_t> aku_give_check_at{0};
        // when the client last handed Crash a mask (the filler); 0 when none is out. With Aku Aku Sanity's lock on, the
        // lock steps aside while that mask is out (masks protect, it isn't cleared); crates stay blocked
        std::atomic<std::uint64_t> filler_mask_since{0};
        bool aku_set_masks(Address component, int masks);
        // while the Aku Aku lock is on, logs when Crash's Aku Aku manager shows a mask
        void watch_aku_manager(bool locked)
        {
            static Address actor_seen = 0, component = 0;
            static std::uint64_t searched = 0;
            static int last_state = -1;
            static bool last_mask = false;
            static unsigned lines = 0;
            const auto actor = player();
            const auto now = GetTickCount64();
            if (actor != actor_seen || (!component && now - searched > 1000))
            {
                actor_seen = actor, component = 0, searched = now, last_state = -1;
                for (const auto c : actor ? list(value<Address>(actor + 24), 128) : std::vector<Address>{})
                    if (typed(c, 0x13bbbd0) && typed(value<Address>(c + 56), 0x1745d78) &&
                        string(value<Address>(value<Address>(c + 56) + 16)) == "Scripts.Graph.Crash_AkuAkuManager")
                    {
                        component = c;
                        break;
                    }
            }
            if (!locked || !component)
                return;
            const int state = value<int>(component + 0x90);
            const bool mask = resolve(value<Address>(component + 0x60)) != 0;
            clear_aku_count(actor, state != 0 || mask);
            // a mask count with no mask on screen (V1.2.38's "now 2" was saved with the game, and V1.2.39's log showed
            // Aku's manager set as if he had one): cleared once per Crash, so finding Aku Aku later gives no free masks
            static Address cleaned_for = 0;
            if (actor != cleaned_for && !mask && ((state >= 1 && state <= 3) || value<unsigned char>(component + 0x42) == 0))
            {
                cleaned_for = actor;
                std::cout << "Aku Aku lock: Aku's manager counts a mask that isn't there (state " << state << ", DoTakeHit "
                          << unsigned(value<unsigned char>(component + 0x42)) << "); clearing it\n" << std::flush;
                aku_set_masks(component, 0);
            }
            if ((state != last_state || mask != last_mask) && lines < 30)
            {
                ++lines;
                std::cout << "Aku Aku lock: Aku's manager is in state " << state << ", " << (mask ? "with a mask out (he shows)" : "no mask out")
                          << "\n" << std::flush;
            }
            last_state = state, last_mask = mask;
        }
        std::pair<int, bool> aku_manager_state(Address actor);
        // a filler mask is out: from the hand-over until Aku's manager shows no mask (after a 2 s grace for it to appear)
        bool filler_mask_out()
        {
            const auto since = filler_mask_since.load();
            if (!since)
                return false;
            if (GetTickCount64() - since < 2000)
                return true;
            // (the manager's state isn't 0 with no mask: logs show 32835 with none out, so only the mask counts)
            if (aku_manager_state(player()).second)
                return true;
            filler_mask_since = 0;
            std::cout << "Aku Aku Mask: the filler mask is gone; Aku Aku Sanity's lock is back\n" << std::flush;
            return false;
        }
        void update_aku_lock(bool locked)
        {
            static Address forced = 0;
            static Address announced = 0;
            // the game saves game variables at checkpoints and puts them back when Crash respawns, so once the client
            // has turned the switch on, a respawn can turn it on again after the lock is lifted: it is turned off then too
            static bool ever = false;
            // with a filler mask out (or being handed over), the lock doesn't clear masks, keep them from protecting or
            // keep Aku's manager from the messages that bring a mask out (a hand-over can take a few of them)
            const bool hold = locked && !filler_mask_out();
            aku_messages_blocked = hold;
            watch_aku_manager(hold);
            const auto variable = no_aku_variable.load();
            if (!variable)
                return;
            if (!typed(variable, 0x155ebf0))
            {
                if (announced != variable)
                    std::cout << "Aku Aku lock: NoAkuAkuMasks isn't a yes/no switch here (type +0x" << std::hex << (value<Address>(variable) - base())
                              << std::dec << "); masks can't be locked\n" << std::flush;
                announced = variable;
                return;
            }
            const auto getter = reinterpret_cast<unsigned char (*)(Address)>(base() + 0x4e5c60);
            const auto setter = reinterpret_cast<void (*)(Address, unsigned char)>(death_hook.original ? death_hook.original : base() + 0x502000);
            const bool on = getter(variable) != 0;
            if (hold && !on)
            {
                setter(variable, 1);
                if (forced != variable)
                    std::cout << "Aku Aku lock: Aku Aku masks are off (the game's NoAkuAkuMasks switch is on)" << (getter(variable) ? "" : ", but the game refused it")
                              << "\n" << std::flush;
                forced = variable, ever = true;
            }
            else if (!hold && on && (forced == variable || ever))
            {
                setter(variable, 0);
                std::cout << "Aku Aku lock: Aku Aku masks are back on" << (forced == variable ? "" : " (a respawn had put the switch back)") << "\n"
                          << std::flush;
                forced = 0;
            }
        }
        // Crash can move: in control, not in a level intro, a cutscene, a death or a respawn. Traps wait for it and
        // their timers pause without it. Riding something (vehicle levels) only needs the camera and no death.
        bool player_can_move(Address actor, bool camera_active)
        {
            if (!camera_active || !typed(actor, 0x1592220))
                return false;
            if (const auto died = bound_player_died(); died && reinterpret_cast<unsigned char (*)(Address)>(base() + 0x4e5c60)(died))
                return false;
            if (!(value<unsigned>(actor + 56) & 0x100) || value<unsigned char>(actor + 341) != 1 || value<unsigned char>(actor + 342) ||
                (value<unsigned>(actor + 336) & 0x30) || !value<unsigned char>(actor + 568))
                return false;
            const auto input = value<Address>(actor + 424);
            if (!typed(input, 0x1589128))
                return true;
            const auto controls = value<Address>(input + 88);
            return typed(controls, 0x13eb4b8) && value<int>(controls + 16) == 0;
        }
        bool death_controls_ready(Address actor, bool camera_active)
        {
            // require an active, controllable player. skip transition/control-lock and invulnerability states.
            if (!camera_active || !typed(actor, 0x1592220) || !(value<unsigned>(actor + 56) & 0x100) ||
                value<unsigned char>(actor + 341) != 1 || value<unsigned char>(actor + 342) ||
                (value<unsigned>(actor + 336) & 0x30) || !value<unsigned char>(actor + 568))
                return false;
            const auto input = value<Address>(actor + 424), invulnerable = value<Address>(actor + 264);
            if (!typed(input, 0x1589128) || !typed(invulnerable, 0x13eb4b8))
                return false;
            const auto controls = value<Address>(input + 88);
            return typed(controls, 0x13eb4b8) && value<int>(controls + 16) == 0 && value<int>(invulnerable + 16) == 0;
        }
        std::atomic_uint active_style{0};
        using Matrix = std::array<float, 16>;
        Matrix multiply(const Matrix &a, const Matrix &b)
        {
            Matrix out{};
            for (unsigned r = 0; r < 4; ++r)
                for (unsigned c = 0; c < 4; ++c)
                    for (unsigned k = 0; k < 4; ++k)
                        out[r * 4 + c] += a[r * 4 + k] * b[k * 4 + c];
            return out;
        }
        bool entity_matrix(Address entity, Matrix &out, std::vector<Address> chain = {})
        {
            if (!entity || chain.size() >= 8 || std::find(chain.begin(), chain.end(), entity) != chain.end())
                return false;
            chain.push_back(entity);
            // placeable: position +32, transform pointer +48, optional parent attachment +16
            const auto transform = value<Address>(entity + 48);
            if (transform)
            {
                if (!read(transform + 32, out))
                    return false;
            }
            else
            {
                out = {};
                out[0] = out[5] = out[10] = out[15] = 1;
                std::array<float, 3> pos{};
                if (!read(entity + 32, pos))
                    return false;
                std::copy(pos.begin(), pos.end(), out.begin() + 12);
            }
            if (!std::all_of(out.begin(), out.end(), [](float f)
                             { return std::isfinite(f); }) ||
                std::abs(out[15] - 1) > 0.001f)
                return false;
            const auto bolt = value<Address>(entity + 16);
            if (bolt)
            {
                const auto parent = resolve(value<Address>(bolt + 16));
                Matrix parent_matrix{};
                if (!entity_matrix(parent, parent_matrix, std::move(chain)))
                    return false;
                out = multiply(out, parent_matrix);
            }
            return true;
        }
        bool visible_placeable(Address at, Address project, Address root)
        {
            for (unsigned depth = 0; at && depth < 8; ++depth)
            {
                unsigned flags = 0;
                float alpha = 0;
                if (!typed(at, 0x161cf98) || value<Address>(at + 40) != project || !read(at + 16, flags) ||
                    (flags & 0x4900) || !read(at + 208, alpha) || !std::isfinite(alpha) || alpha < 0.99f || alpha > 1.01f)
                    return false;
                if (at == root)
                    return true;
                at = value<Address>(at + 72);
            }
            return false;
        }
        bool campaign_menu_visible_at(Address manager)
        {
            // the engine GUI uses igGuiComposition
            if (!typed(manager, 0x1556d90))
                return false;
            for (unsigned c = 0; c < 5; ++c)
            {
                const auto context = value<Address>(manager + 48 + c * 8);
                if (!typed(context, 0x161bb18) || value<unsigned char>(context + 209))
                    continue;
                for (auto project : list(value<Address>(context + 72), 64))
                {
                    if (!typed(project, 0x161d3e8))
                        continue;
                    const auto flags = value<unsigned char>(project + 52);
                    auto root = value<Address>(project + 16);
                    if (!(flags & 0x20) || (flags & 0x10) || value<Address>(project + 144) != context || !typed(root, 0x161cf98))
                        continue;
                    const auto asset = resolve(value<Address>(root + 64));
                    if (typed(asset, 0x161be40))
                    {
                        const auto composed = value<Address>(asset + 32);
                        if (composed == root || value<Address>(composed + 72) != root || value<Address>(root + 40) != project ||
                            (value<unsigned>(root + 16) & 0x4800))
                            continue;
                        root = composed;
                    }
                    if (!visible_placeable(root, project, root))
                        continue;
                    unsigned panels = 0;
                    for (const auto child : list(value<Address>(root + 80), 64))
                    {
                        if (value<Address>(child + 72) != root || !visible_placeable(child, project, root))
                            continue;
                        const auto name = string(value<Address>(child + 48));
                        panels |= name == "[Game_Select_C1]" ? 1 : name == "[Game_Select_C2]" ? 2
                                                               : name == "[Game_Select_C3]"   ? 4
                                                                                              : 0;
                    }
                    if (panels == 7)
                        return true;
                }
            }
            return false;
        }
    }
    bool campaign_menu_visible() { return campaign_menu_visible_at(value<Address>(base() + 0x1a8feb8)); }
    bool pause_menu_visible()
    {
        const auto manager = value<Address>(base() + 0x1a8feb8);
        if (!typed(manager, 0x1556d90))
            return false;
        std::set<std::string> names;
        for (unsigned c = 0; c < 5; ++c)
        {
            const auto context = value<Address>(manager + 48 + c * 8);
            if (!typed(context, 0x161bb18) || value<unsigned char>(context + 209))
                continue;
            for (auto project : list(value<Address>(context + 72), 64))
            {
                if (!typed(project, 0x161d3e8))
                    continue;
                const auto flags = value<unsigned char>(project + 52);
                auto root = value<Address>(project + 16);
                if (!(flags & 0x20) || (flags & 0x10) || value<Address>(project + 144) != context || !typed(root, 0x161cf98))
                    continue;
                const auto asset = resolve(value<Address>(root + 64));
                if (typed(asset, 0x161be40))
                {
                    const auto composed = value<Address>(asset + 32);
                    if (composed != root && value<Address>(composed + 72) == root && value<Address>(root + 40) == project &&
                        !(value<unsigned>(root + 16) & 0x4800))
                        root = composed;
                }
                if (!visible_placeable(root, project, root))
                    continue;
                const auto root_name = string(value<Address>(root + 48));
                if (!root_name.empty())
                    names.insert(root_name);
                for (const auto child : list(value<Address>(root + 80), 64))
                {
                    if (value<Address>(child + 72) != root || !visible_placeable(child, project, root))
                        continue;
                    const auto name = string(value<Address>(child + 48));
                    if (!name.empty())
                        names.insert(name);
                }
            }
        }
        // log what is on screen when it changes (rarely, and only so often), to learn the pause menu's names
        static std::set<std::string> logged;
        static std::uint64_t next_log = 0;
        static unsigned lines = 0;
        const auto now = GetTickCount64();
        if (names != logged && now >= next_log && lines < 60)
        {
            logged = names, next_log = now + 2000, ++lines;
            std::string text;
            for (const auto &name : names)
                text += (text.empty() ? "" : ", ") + name;
            std::cout << "Screen panels: " << (text.empty() ? "(none)" : text) << "\n" << std::flush;
        }
        // the pause menu's panels (from the log): Left_Side and Right_Side, with GameWindow and [TVShake]
        return names.count("Left_Side") && names.count("Right_Side");
    }
    SceneView scene()
    {
        // resolve the live camera through its system and metadata. validate the matrix field before reading +512.
        SceneView result;
        const auto world = value<Address>(base() + 0x1a64690);
        result.zone = string(value<Address>(world + 184));
        auto actor = player();
        static Address map_actor = 0;
        static std::uint64_t next_map_actor = 0;
        if (hub_campaign(result.zone) == 1)
        {
            // C1 moves a separate overworld character. Hero_Crash stays near the map origin.
            if (GetTickCount64() >= next_map_actor)
            {
                map_actor = 0;
                next_map_actor = GetTickCount64() + 250;
                for (const auto entity : event_owners())
                    if (typed(entity, 0x14c8ef8) && string(value<Address>(entity + 128)) == "CrashHub" &&
                        (value<unsigned>(entity + 56) & 0x100))
                    {
                        map_actor = entity;
                        break;
                    }
            }
            actor = event_owner(map_actor) && typed(map_actor, 0x14c8ef8) &&
                            string(value<Address>(map_actor + 128)) == "CrashHub" &&
                            (value<unsigned>(map_actor + 56) & 0x100)
                        ? map_actor : 0;
        }
        else
        {
            map_actor = 0;
            next_map_actor = 0;
        }
        if (actor)
            result.player_active = read(actor + 32, result.player_position) &&
                                   std::all_of(result.player_position.begin(), result.player_position.end(),
                                               [](float f) { return std::isfinite(f); });
        const auto system = value<Address>(base() + 0x1a890e0), meta = value<Address>(base() + 0x1a891d0);
        if (!system || !meta)
            return result;
        static Address verified_meta = 0;
        if (meta != verified_meta)
        {
            if (string(value<Address>(meta + 16)) != "CCameraSystemInstance" || value<std::uint16_t>(meta + 90) != 976)
                return result;
            const auto count = value<unsigned>(meta + 24);
            const auto fields = value<Address>(meta + 40);
            bool found = false;
            if (count > 128 || !fields)
                return result;
            for (unsigned i = 0; i < count; ++i)
            {
                const auto field = value<Address>(fields + i * 8);
                if (string(value<Address>(field + 48)) == "_viewProjectionMatrix")
                    found = value<std::uint16_t>(field + 22) == 64 && value<std::uint16_t>(field + 24) == 512;
            }
            if (!found)
                return result;
            verified_meta = meta;
        }
        const auto cameras = list(value<Address>(system + 32), 4);
        if (cameras.empty())
            return result;
        const auto camera = cameras.front();
        if (value<Address>(camera) != value<Address>(meta + 56) || !value<unsigned char>(camera + 376) || !value<unsigned char>(camera + 377))
            return result;
        if (read(camera + 512, result.view_projection) && std::all_of(result.view_projection.begin(), result.view_projection.end(), [](float f)
                                                                      { return std::isfinite(f); }))
            result.camera_active = true;
        return result;
    }
    namespace
    {
        struct Crate
        {
            Address anchor = 0, entity = 0, model = 0;
            Box identity;
        };
        struct TintedModel
        {
            Address model = 0, bundle = 0;
            unsigned color = 0;
            Address handle = 0;
            float opacity = default_tint_opacity;
        };
        std::mutex crate_mutex;
        std::vector<TintedModel> tinted_models;

        bool field_at(Address meta, const char *name, unsigned offset, unsigned size)
        {
            const auto count = value<unsigned>(meta + 24);
            const auto fields = value<Address>(meta + 40);
            if (!fields || count > 256)
                return false;
            for (unsigned i = 0; i < count; ++i)
            {
                const auto field = value<Address>(fields + i * 8);
                if (string(value<Address>(field + 48)) == name)
                    return value<std::uint16_t>(field + 24) == offset && value<std::uint16_t>(field + 22) == size;
            }
            return false;
        }
        // a script type's field by name and size: its offset in the script's data, or 0
        unsigned field_offset(Address meta, const char *name, unsigned size)
        {
            const auto count = value<unsigned>(meta + 24);
            const auto fields = value<Address>(meta + 40);
            if (!fields || count > 256)
                return 0;
            for (unsigned i = 0; i < count; ++i)
            {
                const auto field = value<Address>(fields + i * 8);
                if (string(value<Address>(field + 48)) == name)
                    return value<std::uint16_t>(field + 22) == size ? value<std::uint16_t>(field + 24) : 0;
            }
            return 0;
        }
        bool request_scripted_death(Address actor)
        {
            if (!event_hook.installed && !code_matches(0x767da0, std::array<unsigned char, 16>{0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x57, 0x48,
                                                                                                0x83, 0xec, 0x40, 0x48, 0x83, 0x79, 0x10, 0}))
                throw std::runtime_error("DeathLink script-message sender differs");
            for (const auto component : list(value<Address>(actor + 24), 128))
            {
                const auto meta = value<Address>(component + 56), data = value<Address>(component + 16);
                if (!typed(component, 0x13bbbd0) || value<Address>(component + 24) != actor ||
                    !(value<unsigned>(component + 32) & 1) || (value<unsigned>(component + 32) & 24) ||
                    !(value<unsigned char>(component + 40) & 1) || !typed(meta, 0x1745d78) ||
                    string(value<Address>(meta + 16)) != "Scripts.Graph.Crash_DamageManager" ||
                    !typed(data, 0x13bbb10) || !death_handle || value<Address>(data + 80) != death_handle.load())
                    continue;
                for (const auto delegator : list(value<Address>(meta + 160), 128))
                {
                    const auto message = value<Address>(delegator + 24), target = value<Address>(delegator + 32);
                    if (!typed(delegator, 0x13bc268) || !typed(message, 0x17b6578) || !typed(target, 0x13bbcb8) ||
                        string(value<Address>(message + 16)) != "Crash_DamageManager_c.GraphEvent_id_pmrewwnd_Definition")
                        continue;
                    if (!field_at(message, "_sender", 32, 8) ||
                        !field_at(message, "GraphEvent_id_snc05c5g_Definition_Parameter", 56, 8) ||
                        !field_at(message, "Field_id_16ghylas", 64, 1))
                        throw std::runtime_error("DeathLink falling-death message layout differs");
                    // CrashFallingDeath runs the normal respawn script. 
                    // KillEntity leaves this reused actor at zero health so we can't use it :(
                    std::array<Address, 11> sender{base() + 0x13bc390, 0, message, target};
                    std::array<Address, 6> context{component};
                    reinterpret_cast<Address (*)(Address, Address)>(base() + 0x767da0)(
                        reinterpret_cast<Address>(sender.data()), reinterpret_cast<Address>(context.data()));
                    return true;
                }
            }
            return false;
        }
        // Event research (for an Aku Aku filler and for setting off the falling Nitro where it lands): the first time each
        // kind of graph event reaches Crash or a Nitro crate it goes to the log with its sender and its fields, and so does
        // every event an Aku Aku crate sends or gets. Only reads; every event still goes through unchanged.
        std::atomic<Address> event_crash{0};
        // (off since V1.2.43: the Aku Aku Mask filler works, and the research lines filled the log)
        std::atomic_bool event_research_done{true};
        // research: every event a trap Nitro gets, with the time since it appeared, to find what sends it "home"
        std::array<std::atomic<Address>, 4> trap_event_entity{};
        std::array<std::atomic<std::uint64_t>, 4> trap_event_since{};
        // its common_Crate_Explode script sent something: it went off (the crate itself stays, so this is how to tell)
        std::array<std::atomic_bool, 4> trap_event_exploded{};
        std::atomic_uint trap_event_lines{0}, trap_event_next{0};
        // which events each watched trap Nitro has had logged (each kind once: spinning near one sends it one per spin)
        std::mutex trap_event_mutex;
        std::array<std::set<std::string>, 4> trap_event_seen;
        std::mutex event_mutex;
        std::set<std::string> events_seen;
        // Aku Aku lock: the game's NoAkuAkuMasks switch stops masks from protecting Crash and crates from giving them,
        // but the game still shows the mask it brings back at a level's start (a mask carried in from the last level)
        // and when Crash respawns. While the lock is on, Crash's Aku Aku manager doesn't get the messages that hand him
        // a mask (seen in the V1.1.65-V1.1.70 logs: at a level's start, at a respawn, and when a mask is picked up);
        // the ones about a hit or a death still go through, so a mask already there goes away when Crash dies.
        std::set<std::string> aku_blocked_seen;
        // the first of each message the lock kept from Aku's manager for this Crash: its send node and message
        Address aku_blocked_actor = 0;
        std::map<std::string, std::pair<Address, Address>> aku_blocked_nodes;
        // the script type of Crash's Aku Aku manager, found by name the first time a message reaches it (also in the
        // first frames of a level), and the other script types on Crash, so each name is read only once
        std::atomic<Address> aku_manager_meta{0};
        std::set<Address> crash_other_metas;
        bool aku_manager(Address component, Address target)
        {
            const auto meta = value<Address>(component + 56);
            if (!meta)
                return false;
            if (meta == aku_manager_meta.load())
                return true;
            if (!typed(target, 0x1592220))
                return false;
            {
                std::lock_guard lock(event_mutex);
                if (crash_other_metas.count(meta))
                    return false;
            }
            if (string(value<Address>(meta + 16), 64) == "Scripts.Graph.Crash_AkuAkuManager")
            {
                aku_manager_meta = meta;
                return true;
            }
            std::lock_guard lock(event_mutex);
            if (crash_other_metas.size() < 512)
                crash_other_metas.insert(meta);
            return false;
        }
        bool aku_give_message(const std::string &name)
        {
            static const char *const gives[]{"tm011evr", "5fw2bzv2", "5bnreye5", "ju9pgu97", "s8h11iit", "fcbn65ha", "ibjgf1k9",
                                             "a9nnfx8i", "6ldq5n9r", "9nj5heau", "nr69k1wc", "cwmbdu2t", "barwfjg5", "5sryuo67"};
            for (const auto id : gives)
                if (name.find(id) != std::string::npos)
                    return true;
            return false;
        }
        unsigned event_lines = 0, aku_event_lines = 0;
        std::string event_object(Address v)
        {
            if (!v)
                return "nothing";
            for (const auto e : {v, resolve(v)})
                if (e && (typed(e, 0x141d490) || typed(e, 0x1592220)))
                    return string(value<Address>(e + 128), 48);
            std::ostringstream out;
            out << "0x" << std::hex << v;
            if (const auto type = value<Address>(v); type > base() && type < base() + 0x2000000)
                out << "(type +0x" << (type - base()) << ")";
            return out.str();
        }
        std::string send_arguments(Address node);
        void log_event(Address event, Address component, const std::string &who, Address context = 0)
        {
            const auto meta = value<Address>(event + 16);
            const auto name = string(value<Address>(meta + 16), 120);
            const auto script = value<Address>(component + 56);
            auto graph = string(value<Address>(script + 16), 120);
            if (graph.rfind("Scripts.Graph.", 0) == 0)
                graph.erase(0, 14);
            const auto sender = event_object(value<Address>(event + 32));
            const bool aku = who.find("Aku") != std::string::npos || sender.find("Aku") != std::string::npos || graph.find("AkuAku") != std::string::npos;
            // a real send's values come from its argument list (what the log called the sender): what each comes from
            const auto values = send_arguments(event);
            std::lock_guard lock(event_mutex);
            // Aku: every message to Aku Aku's manager, but a crate's or mask's own repeats only a few times each
            static std::map<std::string, unsigned> aku_repeats;
            if (aku && graph.find("AkuAkuManager") == std::string::npos && ++aku_repeats[who + "|" + name] > 4)
                return;
            if (aku ? aku_event_lines >= 600 : (event_lines >= 250 || !events_seen.insert(who + "|" + graph + "|" + name).second))
                return;
            ++(aku ? aku_event_lines : event_lines);
            std::ostringstream out;
            out << "Event: " << who << " (" << graph << ") gets " << name << " from " << sender << " [node 0x" << std::hex << event << ", script "
                << (event >= script ? "+0x" : "-0x") << (event >= script ? event - script : script - event) << std::dec << "]" << values;
            // the first few real sends with values: the call's context, to see what else the game passes
            static unsigned contexts = 0;
            if (context && !values.empty() && contexts < 8)
            {
                ++contexts;
                std::array<Address, 6> words{};
                read(context, words);
                out << " context{" << std::hex;
                for (const auto w : words)
                    out << w << " ";
                out << std::dec << "}";
            }
            out << ":";
            const auto count = value<unsigned>(meta + 24);
            const auto fields = value<Address>(meta + 40);
            for (unsigned i = 0; fields && count <= 64 && i < count; ++i)
            {
                const auto field = value<Address>(fields + i * 8);
                const unsigned offset = value<std::uint16_t>(field + 24), bytes = value<std::uint16_t>(field + 22);
                if (offset < 32 || !bytes)
                    continue;
                out << " " << string(value<Address>(field + 48), 64) << "@0x" << std::hex << offset << std::dec << "=";
                if (bytes == 1)
                    out << unsigned(value<unsigned char>(event + offset));
                else if (bytes == 4)
                    out << value<int>(event + offset) << "/" << value<float>(event + offset);
                else if (bytes == 8)
                    out << event_object(value<Address>(event + offset));
                else
                {
                    std::array<float, 4> f{};
                    read(event + offset, f);
                    out << "(" << f[0] << "," << f[1] << "," << f[2] << ")";
                }
            }
            std::cout << out.str() << "\n" << std::flush;
            if (aku_event_lines >= 600)
                event_research_done = true;
        }
        // what a real send's values come from (its argument list, a list of value sources): each source's type, its
        // first two words and the name of the script setting it reads, when it reads one
        std::string send_arguments(Address node)
        {
            const auto args = value<Address>(node + 32);
            if (!args || !typed(args, 0x1745070))
                return {};
            const auto sources = list(args, 16);
            std::ostringstream out;
            out << " values(" << sources.size() << "):";
            for (const auto s : sources)
            {
                const auto type = value<Address>(s);
                out << " ";
                if (type > base() && type < base() + 0x2000000)
                    out << "+0x" << std::hex << (type - base());
                else
                    out << "?";
                out << "{" << std::hex << value<std::uint64_t>(s + 24) << "," << value<std::uint64_t>(s + 32) << "}" << std::dec;
                const auto name = string(value<Address>(value<Address>(value<Address>(s + 24) + 24) + 48), 48);
                if (!name.empty() && std::all_of(name.begin(), name.end(), [](char c) { return c > 32 && c < 127; }))
                    out << "=" << name;
            }
            return out.str();
        }
        // a send node's target (+24): the kind of value source and what it holds (V1.2.61 research: where a trap Nitro's
        // messages go when it explodes, to find the one that adds a box to the level's count)
        std::string send_target(Address node)
        {
            const auto source = value<Address>(node + 24);
            if (!source)
                return " to (none)";
            std::ostringstream out;
            const auto type = value<Address>(source);
            out << " to ";
            if (type > base() && type < base() + 0x2000000)
                out << "+0x" << std::hex << (type - base()) << std::dec;
            else
                out << "?";
            for (const auto at : {source + 24, source + 32})
            {
                const auto v = value<Address>(at);
                out << " " << event_object(v);
                if (const auto name = string(value<Address>(value<Address>(v + 24) + 48), 48);
                    !name.empty() && std::all_of(name.begin(), name.end(), [](char c) { return c > 32 && c < 127; }))
                    out << "=" << name;
            }
            return out.str();
        }
        // when an Aku Aku crate last got or sent a message (one breaking): what Aku's manager does right after is the pickup
        std::atomic<std::uint64_t> aku_crate_event_at{0};
        unsigned near_crate_lines = 0;
        // the send node of the pickup's fcbn65ha (Aku's manager sends it to himself right after an Aku Aku crate breaks)
        // and its type then, so /aku_give can run the very same node later
        std::atomic<Address> aku_pickup_node{0}, aku_pickup_type{0};
        std::set<Address> aku_fcbn_nodes;
        void note_aku_send(Address event, Address component)
        {
            const auto args = value<Address>(event + 32);
            if (!args || !typed(args, 0x1745070))
                return;
            const auto name = string(value<Address>(value<Address>(event + 16) + 16), 120);
            if (name.find("fcbn65ha") == std::string::npos)
                return;
            const bool after_crate = GetTickCount64() - aku_crate_event_at.load() < 3000;
            if (after_crate)
                aku_pickup_node = event, aku_pickup_type = value<Address>(event);
            const auto script = value<Address>(component + 56);
            const auto values = send_arguments(event);
            std::lock_guard lock(event_mutex);
            const bool fresh = aku_fcbn_nodes.insert(event).second;
            if ((fresh || after_crate) && aku_fcbn_nodes.size() < 16)
                std::cout << "Aku give: Aku's manager sent fcbn65ha from node 0x" << std::hex << event << " (script " << (event >= script ? "+0x" : "-0x")
                          << (event >= script ? event - script : script - event) << ", type +0x" << (value<Address>(event) - base()) << std::dec << ")"
                          << (after_crate ? ", right after an Aku Aku crate: kept for /aku_give" : "") << (fresh ? "" : " (seen before)") << values << "\n"
                          << std::flush;
        }
        Address event_sent(Address event, Address context)
        {
            const auto crash = event_crash.load();
            if (crash && event && context)
            {
                const auto component = value<Address>(context);
                const auto target = component ? value<Address>(component + 24) : 0;
                bool blocked = false;
                for (unsigned i = 0; target && i < trap_event_entity.size(); ++i)
                    if (trap_event_entity[i].load() == target)
                    {
                        const auto name = string(value<Address>(value<Address>(event + 16) + 16), 120);
                        // a Nitro's hop: V1.1.68's log showed each hop starts with these two reaching its
                        // common_Crate_Nitro_Spawned, and that a trap Nitro's hops carry it "home" to the spawner it was
                        // borrowed from (9000 away, a few thousand units per hop). A trap Nitro doesn't hop.
                        blocked = name == "common_Crate_c.New_Graph_Event_1_id_uphse9ht" || name == "common_Crate_Nitro_Spawned.New_Graph_Event_1_id_h1ogxqqw";
                        // its spawner isn't told it's gone (V1.2.63): the spawner was borrowed from a crate in the level,
                        // and that crate's box count took the trap Nitro's explosion for its own crate breaking (V1.2.62:
                        // the counter went from 0/18 to 1/18). The trap Nitro still explodes and disappears as usual.
                        const bool spawner_note = name == "common_Spawner_Template_c.GraphEvent_id_van6ltro_Definition";
                        blocked = blocked || spawner_note;
                        // its explosion script spoke: it went off. (Until V1.2.70 every kind of message a trap Nitro got went
                        // to the log, over 20 lines in the frame it exploded; that research is done.)
                        if (string(value<Address>(value<Address>(component + 56) + 16), 120) == "Scripts.Graph.common_Crate_Explode")
                            trap_event_exploded[i] = true;
                        break;
                    }
                if (blocked)
                    return 0;
                if (aku_messages_blocked.load() && !own_aku_message.load() && component && target && aku_manager(component, target))
                {
                    const auto name = string(value<Address>(value<Address>(event + 16) + 16), 120);
                    // a respawn tells Aku how many masks Crash comes back with (V1.1.72: blocking it left a mask that
                    // was already out on screen through every death): it goes through, with none
                    if (name.find("zocmrwe4") != std::string::npos)
                    {
                        const auto meta = value<Address>(event + 16);
                        const auto count = value<unsigned>(meta + 24);
                        const auto fields = value<Address>(meta + 40);
                        for (unsigned i = 0; fields && count <= 64 && i < count; ++i)
                        {
                            const auto field = value<Address>(fields + i * 8);
                            const unsigned offset = value<std::uint16_t>(field + 24), bytes = value<std::uint16_t>(field + 22);
                            if (bytes != 4 || offset < 32 || string(value<Address>(field + 48), 64).find("w7b6hopu") == std::string::npos)
                                continue;
                            const auto masks = value<int>(event + offset);
                            if (masks != 0)
                            {
                                const int none = 0;
                                SIZE_T done = 0;
                                WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(event + offset), &none, sizeof(none), &done);
                                std::lock_guard lock(event_mutex);
                                if (aku_blocked_seen.size() < 40 && aku_blocked_seen.insert(name + std::to_string(masks)).second)
                                    std::cout << "Aku Aku lock: a respawn gave Aku " << masks << " (zocmrwe4 w7b6hopu); made it 0\n" << std::flush;
                            }
                        }
                    }
                    else if (aku_give_message(name))
                    {
                        std::lock_guard lock(event_mutex);
                        // the setup messages Aku gets at a level's start, kept here: a filler mask needs them later
                        if (aku_blocked_actor != target)
                            aku_blocked_actor = target, aku_blocked_nodes.clear();
                        if (const auto id = name.find("_id_"); id != std::string::npos && typed(event, 0x13bc390) && aku_blocked_nodes.size() < 16)
                            aku_blocked_nodes.emplace(name.substr(id + 4), std::make_pair(event, value<Address>(event + 16)));
                        if (aku_blocked_seen.size() < 40 && aku_blocked_seen.insert(name).second)
                        {
                            const auto id = name.find("_id_");
                            std::cout << "Aku Aku lock: kept " << (id == std::string::npos ? name : name.substr(id + 4))
                                      << " from Aku Aku (it hands Crash a mask)\n" << std::flush;
                        }
                        return 0;
                    }
                }
                // the pickup's own message (for /aku_give): what Aku's manager sends himself once a crate's mask is his
                if (!own_aku_message.load() && target == crash && component && aku_manager(component, target))
                    note_aku_send(event, component);
                // V1.1.66 logged the first of each kind; now only what reaches Crash's Aku Aku manager and Aku Aku
                // crates and masks, in order (for /aku_give), and for 3 s after an Aku Aku crate's event, what else
                // Crash and the things around him send (to find what tells Aku's manager a crate's mask is his)
                if (!event_research_done.load())
                {
                    const auto now = GetTickCount64();
                    const bool after_crate = now - aku_crate_event_at.load() < 3000;
                    if (target == crash)
                    {
                        if (string(value<Address>(value<Address>(component + 56) + 16), 64) == "Scripts.Graph.Crash_AkuAkuManager")
                            log_event(event, component, "an Aku Aku", context);
                        else if (after_crate)
                            log_event(event, component, "Crash", context);
                    }
                    else if (target && typed(target, 0x141d490))
                    {
                        const auto name = string(value<Address>(target + 128), 48);
                        if (name.find("Aku") != std::string::npos)
                        {
                            aku_crate_event_at = now;
                            log_event(event, component, "an Aku Aku (" + name + ")", context);
                        }
                        else if (after_crate && near_crate_lines < 60)
                            ++near_crate_lines, log_event(event, component, "near an Aku Aku crate: " + name, context);
                    }
                    else if (target && after_crate && near_crate_lines < 60)
                        ++near_crate_lines, log_event(event, component, "near an Aku Aku crate: " + event_object(target), context);
                }
            }
            return reinterpret_cast<Address (*)(Address, Address)>(event_hook.original)(event, context);
        }
        // research: the yes/no and number game variables' function tables, side by side, marking the yes/no getter
        // and setter the client already uses. The number variables' setter sits in the same place in theirs.
        void log_variable_tables(Address yes_no, Address number)
        {
            static bool logged = false;
            if (logged || !yes_no || !number)
                return;
            logged = true;
            for (const auto variable : {yes_no, number})
            {
                const auto table = value<Address>(variable);
                std::vector<Address> entries(48);
                if (!read(table, entries))
                    continue;
                std::ostringstream out;
                out << "Variable table +0x" << std::hex << (table - base()) << ":";
                for (unsigned i = 0; i < entries.size(); ++i)
                {
                    const auto e = entries[i];
                    if (e < base() || e >= base() + 0x2000000)
                        break;
                    const auto rva = e - base();
                    out << " " << std::dec << i << "=" << std::hex << rva << (rva == 0x502000 ? "(SET)" : rva == 0x4e5c60 || rva == 0x4e4610 ? "(GET)" : "");
                }
                std::cout << out.str() << "\n" << std::flush;
            }
            // the code at the start of each number-variable function, to tell the setter by its shape
            const auto table = value<Address>(number);
            std::vector<Address> entries(48);
            if (!read(table, entries))
                return;
            for (unsigned i = 0; i < entries.size(); ++i)
            {
                const auto e = entries[i];
                if (e < base() || e >= base() + 0x2000000)
                    break;
                std::array<unsigned char, 32> code{};
                read(e, code);
                std::ostringstream out;
                out << "Variable code " << i << " +0x" << std::hex << (e - base()) << " ";
                for (const auto b : code)
                    out << std::setw(2) << std::setfill('0') << unsigned(b);
                std::cout << out.str() << "\n" << std::flush;
            }
        }
        Address unbound_logged = 0;
        unsigned unbound_rescan = 0;
        // rides and forms without a PlayerDied (seen by health): the actor, whether it was alive last frame, and when
        // its health ran out (waiting to see whether PlayerDied follows)
        Address health_actor = 0;
        bool health_alive = false;
        std::uint64_t health_death_at = 0;
        void bind_player_death(Address actor)
        {
            if (actor == last_actor && bound_player_died())
                return;
            // a form without a PlayerDied is looked over again about twice a second (its scripts may load late)
            if (actor == last_actor && actor == unbound_logged && ++unbound_rescan % 30)
                return;
            if (actor != last_actor)
            {
                last_actor = actor;
                remote_death = false;
                death_watch_unknown = 0; // a fresh log allowance for each Crash (new level, bonus round)
            }
            death_handle = 0;
            bonus_round_variable = 0;
            time_trial_variable = 0;
            no_aku_variable = 0;
            const bool on_foot = actor == player();
            std::string graphs;
            // Crash_DamageManager first (DeathLink deaths are sent through it), then any other damage manager
            const auto components = list(value<Address>(actor + 24), 128);
            for (unsigned pass = 0; pass < 2; ++pass)
            for (const auto c : components)
            {
                if (!typed(c, 0x13bbbd0) || value<Address>(c + 24) != actor || !(value<unsigned>(c + 32) & 1) ||
                    (value<unsigned>(c + 32) & 24) || !(value<unsigned char>(c + 40) & 1))
                    continue;
                const auto meta = value<Address>(c + 56), data = value<Address>(c + 16), dm = value<Address>(data + 32);
                const auto graph = typed(meta, 0x1745d78) ? string(value<Address>(meta + 16)) : std::string();
                if (!pass && !on_foot && !graph.empty() && graphs.size() < 1500)
                    graphs += " " + graph;
                if (!pass && graph != "Scripts.Graph.Crash_DamageManager")
                    continue;
                // Crash_DamageManager's data holds the PlayerDied handle at +80, component+56 identifies the graph. The
                // hog, polar bear, jetpack and vehicle forms have damage managers of their own: any graph whose name
                // says DamageManager and that has a PlayerDied handle is used, wherever the handle sits
                const unsigned died_at = graph.find("DamageManager") != std::string::npos && typed(data, 0x13bbb10) && dm &&
                                                 dm == value<Address>(meta + 176)
                                             ? field_offset(dm, "PlayerDied", 8)
                                             : 0;
                if (!died_at)
                    continue;
                death_handle = value<Address>(data + died_at);
                if (!bound_player_died())
                {
                    death_handle = 0;
                    continue;
                }
                if (!code_matches(0x4e5c60, std::array<unsigned char, 5>{0xe9, 0xab, 0xe9, 0xff, 0xff}))
                    throw std::runtime_error("Game PlayerDied getter differs");
                // the death watch names: every game variable this DamageManager holds a handle to
                {
                    const auto count = value<unsigned>(dm + 24);
                    const auto fields = value<Address>(dm + 40);
                    std::ostringstream names;
                    unsigned found = 0;
                    for (unsigned i = 0; fields && count <= 256 && i < count && found < 32; ++i)
                    {
                        const auto field = value<Address>(fields + i * 8);
                        const unsigned offset = value<std::uint16_t>(field + 24), size = value<std::uint16_t>(field + 22);
                        if (size != 8)
                            continue;
                        const auto handle = value<Address>(data + offset);
                        if ((value<unsigned>(handle) & 0xf0000000) != 0x40000000 || string(value<Address>(handle + 24)) != "gamevariables")
                            continue;
                        const auto variable = resolve(handle);
                        if (!variable)
                            continue;
                        auto &slot = death_watch[found++];
                        const auto name = string(value<Address>(handle + 8), 47);
                        if (name == "BonusRoundActive")
                            bonus_round_variable = variable;
                        if (name == "TimeTrialActive")
                            time_trial_variable = variable;
                        if (name == "AkuAkuCount")
                            aku_variable = variable;
                        if (name == "NoAkuAkuMasks")
                            no_aku_variable = variable;
                        if (name == "WumpaCount")
                            wumpa_variable = variable;
                        std::snprintf(slot.name, sizeof(slot.name), "%s", name.c_str());
                        slot.variable = variable;
                        names << " " << name;
                    }
                    if (found != death_watch_count.load() || !on_foot)
                        std::cout << "Death watch: " << graph.substr(graph.rfind('.') + 1) << " ("
                                  << string(value<Address>(actor + 128), 64) << ") uses" << names.str() << "\n" << std::flush;
                    death_watch_count = found;
                }
                death_hook.attach(0x502000, {0x48, 0x89, 0x5c, 0x24, 8, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x40}, reinterpret_cast<Address>(player_died_set));
                event_crash = actor;
                try
                {
                    event_hook.attach(0x767da0, {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0x83, 0x79, 0x10, 0x00},
                                      reinterpret_cast<Address>(event_sent));
                }
                catch (const std::exception &e)
                {
                    static bool told = false;
                    if (!told)
                        std::cout << "Event research: " << e.what() << "\n" << std::flush, told = true;
                }
                log_variable_tables(bound_player_died(), aku_variable.load());
                return;
            }
            if (!on_foot && actor != unbound_logged)
            {
                unbound_logged = actor;
                std::cout << "DeathLink: " << string(value<Address>(actor + 128), 64)
                          << " has no PlayerDied; its deaths are seen when its health runs out. Graphs:" << graphs << "\n" << std::flush;
            }
        }
        const BoxSpec flying_specs[]{
#include "flying_crate_specs.inc"
        };
        bool flying_crate_actor(Address actor)
        {
            for (const auto c : list(value<Address>(actor + 24), 64))
            {
                if (!typed(c, 0x13bbbd0) || value<Address>(c + 24) != actor)
                    continue;
                const auto meta = value<Address>(c + 56);
                if (typed(meta, 0x1745d78) && string(value<Address>(meta + 16)) == "Scripts.Graph.common_Crate_Flying_Level_Count")
                    return true;
            }
            return false;
        }
        std::vector<Address> prefab_instances()
        {
            // same metadata-to-instance table as event_owners(), selecting prefab instances rather than event components
            const auto context = value<Address>(base() + 0x1ac1dd8), meta = value<Address>(base() + 0x1ac1830);
            const auto table = value<Address>(context + 120);
            const auto bytes = value<std::uint64_t>(table + 16) & 0xfffffffffffffULL;
            if (!context || !meta || !table || bytes != (value<std::uint64_t>(table + 32) & 0xfffffffffffffULL) || bytes > 65536 * 8 || bytes % 8)
                return {};
            const auto keys = value<Address>(table + 40), lists = value<Address>(table + 24);
            for (unsigned i = 0; i < bytes / 8; ++i)
                if (value<Address>(keys + i * 8) == meta)
                    return list(value<Address>(lists + i * 8), 16384);
            return {};
        }
        const BoxSpec *flying_spec(Address root, const std::string &level)
        {
            std::array<unsigned char, 12> position{};
            if (!typed(root, 0x1722e80) || !read(root + 32, position))
                return nullptr;
            for (const auto &spec : flying_specs)
                if (level == spec.level && position == spec.position)
                    return &spec;
            return nullptr;
        }
        Address flying_root(Address actor, const std::vector<Address> &prefabs, const std::string &level, const BoxSpec *&identity)
        {
            Address result = 0;
            identity = nullptr;
            for (const auto c : prefabs)
            {
                if (!typed(c, 0x1724358) || value<unsigned char>(c + 56) != 1)
                    continue;
                const auto children = list(value<Address>(c + 40), 128);
                if (std::find(children.begin(), children.end(), actor) == children.end())
                    continue;
                const auto root = value<Address>(c + 24);
                const auto spec = flying_spec(root, level);
                if (!spec)
                    continue;
                if (result && result != root)
                {
                    identity = nullptr;
                    return 0;
                }
                result = root;
                identity = spec;
            }
            return result;
        }
        Address crate_entity(Address anchor)
        {
            // AP identities belong to the invisible spawner, not its shared archetype
            Address spawned = 0;
            bool outlined = false;
            for (const auto c : list(value<Address>(anchor + 24), 64))
            {
                if (!typed(c, 0x13bbbd0) || value<Address>(c + 24) != anchor || !(value<unsigned>(c + 32) & 1) ||
                    (value<unsigned>(c + 32) & 24) || !(value<unsigned char>(c + 40) & 1))
                    continue;
                const auto meta = value<Address>(c + 56);
                if (!typed(meta, 0x1745d78))
                    continue;
                const auto name = string(value<Address>(meta + 16));
                if (name == "Scripts.Graph.common_Crate_Outline")
                {
                    // the template holds the outline. this handle owns the box revealed by its switch
                    outlined = true;
                    const auto fields = value<Address>(meta + 40);
                    const auto count = value<unsigned>(meta + 24);
                    const auto size = value<std::uint16_t>(meta + 90);
                    if (!fields || count > 256 || size < 64 || size > 4096)
                        continue;
                    for (unsigned i = 0; i < count; ++i)
                    {
                        const auto field = value<Address>(fields + i * 8);
                        const auto offset = value<std::uint16_t>(field + 24);
                        if (string(value<Address>(field + 48)) != "replacementSpawned" || value<std::uint16_t>(field + 22) != 8 ||
                            offset < 64 || offset + 8 > size)
                            continue;
                        const auto entity = resolve(value<Address>(c + offset));
                        if (typed(entity, 0x141d490) && (value<unsigned>(entity + 56) & 4))
                            return entity;
                    }
                }
                if (name != "Scripts.Graph.common_Spawner_Template" || !field_at(meta, "SpawnedEntity", 232, 8))
                    continue;
                const auto entity = resolve(value<Address>(c + 232));
                if (typed(entity, 0x141d490) && (value<unsigned>(entity + 56) & 4))
                    spawned = entity;
            }
            return outlined ? 0 : spawned;
        }
        Address crate_model(Address entity)
        {
            // Entity+176 is the model component. +32 verifies its owner and +40 resolves the render model.
            const auto model = value<Address>(entity + 176), ig = value<Address>(model + 40);
            // Bounce and TNT crates use skinned models with the same entity/model fields
            return (typed(model, 0x144bff0) || typed(model, 0x15aee18)) && value<Address>(model + 32) == entity && typed(ig, 0x1676cb8) ? ig : 0;
        }
        bool crate_identity(Address actor, const Box &box, Address position_owner = 0)
        {
            if (!event_owner(actor))
                return false;
            if (position_owner && position_owner != actor)
            {
                const auto spec = flying_spec(position_owner, box.level);
                return spec && box.name == std::string(spec->name) && flying_crate_actor(actor) &&
                       !std::memcmp(spec->position.data(), box.position, sizeof(box.position));
            }
            std::array<float, 3> position{};
            return read(actor + 32, position) && string(value<Address>(actor + 128)) == box.name &&
                   !std::memcmp(position.data(), box.position, sizeof(box.position));
        }
        const Box *selected_box(const Shared &state, const Box &identity)
        {
            const auto found = std::find_if(state.boxes, state.boxes + state.box_count, [&](const auto &box)
                                            { return !std::strcmp(box.level, identity.level) && !std::strcmp(box.name, identity.name) &&
                                                     !std::memcmp(box.position, identity.position, sizeof(box.position)); });
            return found == state.boxes + state.box_count ? nullptr : found;
        }
        std::vector<Crate> crates(const Shared &state, const std::string &zone)
        {
            const auto level = level_key(zone);
            struct Cached
            {
                Address address, position_owner;
                Box identity;
            };
            static std::vector<Cached> cache;
            static std::string cached_zone;
            static std::uint64_t next_scan = 0;
            const auto now = GetTickCount64();
            if (zone != cached_zone || now >= next_scan)
            {
                cache.clear();
                cached_zone = zone;
                next_scan = now + 500;
                std::multimap<std::string, const Box *> selected;
                for (unsigned i = 0; i < state.box_count; ++i)
                    if (level == state.boxes[i].level)
                        selected.emplace(state.boxes[i].name, &state.boxes[i]);
                if (selected.empty())
                    return {};
                const bool flying = level == "L317" || level == "L324" || level == "L330";
                const auto prefabs = flying ? prefab_instances() : std::vector<Address>{};
                std::map<const Box *, std::vector<std::pair<Address, Address>>> matches;
                for (const auto actor : event_owners())
                {
                    auto name = string(value<Address>(actor + 128));
                    Address position_owner = actor;
                    if (flying && flying_crate_actor(actor))
                    {
                        const BoxSpec *spec = nullptr;
                        position_owner = flying_root(actor, prefabs, level, spec);
                        if (!position_owner)
                            continue;
                        name = spec->name;
                    }
                    const auto range = selected.equal_range(name);
                    for (auto found = range.first; found != range.second; ++found)
                    {
                        if (!crate_identity(actor, *found->second, position_owner))
                            continue;
                        const auto pair = std::make_pair(actor, position_owner);
                        auto &owners = matches[found->second];
                        if (std::find(owners.begin(), owners.end(), pair) == owners.end())
                            owners.push_back(pair);
                    }
                }
                for (const auto &[identity, actors] : matches)
                    if (actors.size() == 1)
                        cache.push_back({actors.front().first, actors.front().second, *identity});
            }
            std::vector<Crate> result;
            for (const auto &c : cache)
            {
                const auto selected = selected_box(state, c.identity);
                if (!selected || !crate_identity(c.address, *selected, c.position_owner))
                    continue;
                const auto entity = crate_entity(c.address), model = crate_model(entity);
                if (entity && model)
                    result.push_back({c.address, entity, model, *selected});
            }
            return result;
        }
        bool tint_api()
        {
            // guard tint creation/application and reference helpers before passing engine objects to these RVAs
            const std::pair<Address, std::array<unsigned char, 12>> guards[]{
                {0x882b0, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x8d, 0x15}},
                {0xacd1c0, {0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x83}},
                {0xaf7370, {0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x4c, 0x8b}},
                {0xdda396, {0xc7, 0x41, 0x08, 0x01, 0x00, 0x00, 0x00, 0x4d, 0x85, 0xc0, 0x74, 0x0d}},
                {0xddde3c, {0x81, 0x66, 0x08, 0x00, 0x00, 0xf8, 0xff, 0x48, 0x8b, 0xce, 0xe8, 0xc5}},
                {0xdf0b40, {0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xec, 0x30, 0x48, 0x8b, 0xf2, 0x48}},
                {0xdf0e00, {0x48, 0x85, 0xc9, 0x74, 0x09, 0x48, 0x83, 0xc1, 0x08, 0xe9, 0xa2, 0x49}},
                {0xdf0e10, {0x48, 0x85, 0xc9, 0x0f, 0x85, 0x87, 0xcf, 0xfe, 0xff, 0xc3, 0xcc, 0xcc}}};
            for (const auto &[rva, bytes] : guards)
                if (value<std::array<unsigned char, 12>>(base() + rva) != bytes)
                    return false;
            const auto meta = value<Address>(base() + 0x1a55ff8);
            return value<Address>(meta + 56) == base() + 0x138da80 && string(value<Address>(meta + 16)) == "CFxTintBundle" &&
                   value<std::uint16_t>(meta + 90) == 80 && field_at(meta, "_multiplyColor", 32, 16) &&
                   field_at(meta, "_blendColor", 48, 16) && field_at(meta, "_blendEmissive", 64, 16);
        }
        void retain(Address object) { reinterpret_cast<void (*)(Address)>(base() + 0xdf0e00)(object); }
        void release(Address object) { reinterpret_cast<void (*)(Address)>(base() + 0xdf0e10)(object); }
        void model_handle(Address &handle, Address model) { reinterpret_cast<void (*)(Address *, Address)>(base() + 0xdf0b40)(&handle, model); }
        template <class T>
        bool write(Address at, const T &data)
        {
            SIZE_T done = 0;
            return WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(at), &data, sizeof(data), &done) && done == sizeof(data);
        }
        constexpr std::array<float, 4> blue_tint{1, 1, 1, 1};
        constexpr std::array<float, 4> default_tint{1, 1, 1, 1}, default_blend{1, 1, 1, 0}, default_emissive{1, 0, 0, 0};
        bool marker_tint(const TintedModel &t)
        {
            return typed(t.bundle, 0x138da80) && value<std::array<float, 4>>(t.bundle + 32) == blue_tint &&
                   value<std::array<float, 4>>(t.bundle + 48) == tint_blend(t.color, t.opacity) && value<std::array<float, 4>>(t.bundle + 64) == default_emissive;
        }
        bool has_tint(const TintedModel &t)
        {
            // the model pool invalidates registered handles before recycling even a retained model
            if (!t.model || resolve(t.handle) != t.model || !typed(t.model, 0x1676cb8) || !t.bundle)
                return false;
            const auto bundles = list(value<Address>(t.model + 224), 64);
            return std::find(bundles.begin(), bundles.end(), t.bundle) != bundles.end();
        }
        bool clear_tint(const TintedModel &t)
        {
            // remove only our custom bundle. the engine owns the list and any other effects on this model
            // TNT/pickup effects can adopt this bundle, gotta leave their changed values and ownership alone
            if (!has_tint(t) || !marker_tint(t))
                return false;
            reinterpret_cast<void (*)(Address, Address)>(base() + 0xaf7370)(t.model, t.bundle);
            return !has_tint(t);
        }
        void restore_tints(bool game_thread)
        {
            if (game_thread)
            {
                for (auto &t : tinted_models)
                {
                    clear_tint(t);
                    release(t.bundle);
                    model_handle(t.handle, 0);
                }
            }
            else
                for (const auto &t : tinted_models)
                    if (marker_tint(t))
                    {
                        write(t.bundle + 32, default_tint);
                        write(t.bundle + 48, default_blend);
                    }
            tinted_models.clear();
        }
        void update_tints(const Shared &state, const std::string &zone)
        {
            std::lock_guard lock(crate_mutex);
            if (!tint_api())
            {
                restore_tints(false);
                return;
            }
            if (!state.visible || state.marker_mode != MarkersTint || state.tint_opacity == 0 || level_key(zone).empty())
            {
                restore_tints(true);
                return;
            }
            const auto targets = crates(state, zone);
            for (auto it = tinted_models.begin(); it != tinted_models.end();)
            {
                if (!has_tint(*it) || !marker_tint(*it) || it->opacity != state.tint_opacity || std::none_of(targets.begin(), targets.end(), [&](const auto &c)
                                                                        { return c.model == it->model && c.identity.color == it->color; }))
                {
                    clear_tint(*it);
                    release(it->bundle);
                    model_handle(it->handle, 0);
                    it = tinted_models.erase(it);
                }
                else
                    ++it;
            }
            for (const auto &c : targets)
            {
                if (box_color_index(c.identity.color) >= 5 || tinted_models.size() >= max_boxes)
                    continue;
                const auto bundles = list(value<Address>(c.model + 224), 64);
                if (bundles.size() >= 64 || std::any_of(bundles.begin(), bundles.end(), [](Address b)
                                                        { return typed(b, 0x138da80); }))
                    continue;
                // game effects mutate tint bundles, so every model needs its own instance
                const auto bundle = reinterpret_cast<Address (*)(Address)>(base() + 0x882b0)(0);
                if (!typed(bundle, 0x138da80))
                    return;
                // the factory's count of 1 means no retained owner. keep our own reference across game object removal
                retain(bundle);
                if (value<std::array<float, 4>>(bundle + 32) != default_tint || value<std::array<float, 4>>(bundle + 48) != default_blend ||
                    value<std::array<float, 4>>(bundle + 64) != default_emissive || !write(bundle + 32, blue_tint) || !write(bundle + 48, tint_blend(c.identity.color, state.tint_opacity)))
                {
                    release(bundle);
                    return;
                }
                // engine drawing applies model bundles, draws, then restores each bundle's defaults
                // never put FX uniforms in a material: their GPU state survives/bleeds into scenery draws
                // a direct model retain survives engine object-pool reset and later releases a recycled object
                Address handle = 0;
                model_handle(handle, c.model);
                if (resolve(handle) != c.model)
                {
                    model_handle(handle, 0);
                    release(bundle);
                    continue;
                }
                reinterpret_cast<void (*)(Address, Address)>(base() + 0xacd1c0)(c.model, bundle);
                if (has_tint({c.model, bundle, c.identity.color, handle, state.tint_opacity}))
                    tinted_models.push_back({c.model, bundle, c.identity.color, handle, state.tint_opacity});
                else
                {
                    release(bundle);
                    model_handle(handle, 0);
                }
            }
        }
        bool crate_corners(Address model, LiveBox &box)
        {
            // render model layout: matrix +48, local-space AABB minimum/maximum +112/+128
            Matrix transform{};
            std::array<float, 4> minimum{}, maximum{};
            if (!read(model + 48, transform) || !read(model + 112, minimum) || !read(model + 128, maximum) ||
                !std::all_of(transform.begin(), transform.end(), [](float f)
                             { return std::isfinite(f); }) ||
                std::abs(transform[15] - 1) > 0.001f)
                return false;
            for (unsigned axis = 0; axis < 3; ++axis)
                if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) ||
                    minimum[axis] >= maximum[axis] || maximum[axis] - minimum[axis] > 10000)
                    return false;
            // model bounds are in model space. crates start at their base, not their center.
            for (unsigned i = 0; i < 8; ++i)
            {
                const float x = (i & 1) ? maximum[0] : minimum[0], y = (i & 2) ? maximum[1] : minimum[1], z = (i & 4) ? maximum[2] : minimum[2];
                for (unsigned axis = 0; axis < 3; ++axis)
                    box.corners[i][axis] = x * transform[axis] + y * transform[4 + axis] + z * transform[8 + axis] + transform[12 + axis];
            }
            return true;
        }
        Address c3_button_refresh_method(Address component, Address entity, bool layout = false)
        {
            if (!typed(component, 0x13bbbd0) || value<Address>(component + 24) != entity ||
                !(value<unsigned>(component + 32) & 1) || (value<unsigned>(component + 32) & 24) ||
                !(value<unsigned char>(component + 40) & 1) || !event_owner(entity))
                return 0;
            const auto meta = value<Address>(component + 56);
            if (!typed(meta, 0x1745d78) || string(value<Address>(meta + 16)) != "Scripts.Graph.common_C3_Hub_Portal_Button" ||
                value<std::uint16_t>(meta + 90) != 144 || meta < 42340)
                return 0;
            const auto slab = meta - 42340, method = slab + (layout ? 52556 : 51828), nodes = method + 24, entry = nodes + 48;
            // the two compiled methods refresh visibility or arrange the buttons. validate their one-entry node lists.
            const auto route = value<Address>(entry + 48);
            std::array<Address, 4> bindings{};
            if (!typed(method, 0x17473c8) || value<Address>(method + 16) != nodes || !typed(nodes, 0x1746090) ||
                value<unsigned>(nodes + 16) != 1 || value<unsigned>(nodes + 20) != 1 ||
                (value<std::uint64_t>(nodes + 24) & 0xfffffffffffffULL) != 8 || value<Address>(nodes + 32) != nodes + 40 ||
                value<Address>(nodes + 40) != entry || !typed(entry, 0x1746848) || !typed(route, layout ? 0x17592d8 : 0x1759220) ||
                value<Address>(entry + 24) || value<Address>(entry + 32) || value<Address>(entry + 40) ||
                value<unsigned>(entry + 56) != 4 || !read(value<Address>(entry + 72), bindings) ||
                std::any_of(bindings.begin(), bindings.end(), [](Address p)
                            { return p != 0; }))
                return 0;
            if (layout)
            {
                if (route != slab + 52780)
                    return 0;
            }
            else if (value<Address>(slab + 56556 + 48) != route || value<Address>(slab + 55644 + 48) != route)
                return 0;
            return method;
        }
        Address c3_layout_target(Address component)
        {
            const auto meta = value<Address>(component + 56), data = value<Address>(component + 16), dm = value<Address>(data + 32);
            if (!typed(data, 0x13bbb10) || !dm || dm != value<Address>(meta + 176) ||
                !field_at(dm, "igEntity_id_x9nfoby0_metaField", 96, 8))
                return 0;
            const auto target = resolve(value<Address>(data + 96));
            return typed(target, 0x1722e80) ? target : 0;
        }
        bool c3_static_trigger(Address component, Address entity)
        {
            const auto flags = value<unsigned>(component + 32);
            const auto data = value<Address>(component + 16);
            if (!typed(component, 0x148d9e8) || value<Address>(component + 24) != entity || !event_owner(entity) ||
                !(flags & 1) || (flags & 24) || !(value<unsigned char>(component + 40) & 1) ||
                !typed(data, 0x148d928) || value<unsigned char>(data + 68) != 1 ||
                value<unsigned char>(component + 0x670) != 0 || value<unsigned char>(component + 0x671) != 1)
                return false;
            const auto manager = value<Address>(component + 64);
            const auto index = value<unsigned>(component + 56);
            const auto count = value<unsigned>(manager + 44) & 0x3fffffff;
            const auto records = value<Address>(manager + 32);
            return manager && records && count <= 16384 && index < count &&
                   (value<unsigned char>(records + index * 144 + 64) & 3);
        }
        Address spawner_method(Address component, Address entity, bool allow_spawned, const char **why = nullptr);
        Address c3_platform_spawn_method(Address component, Address entity) { return spawner_method(component, entity, false); }
        Address spawner_method(Address component, Address entity, bool allow_spawned, const char **why)
        {
            const auto fail = [&](const char *reason) -> Address
            {
                if (why)
                    *why = reason;
                return 0;
            };
            if (!typed(component, 0x13bbbd0) || value<Address>(component + 24) != entity ||
                !(value<unsigned>(component + 32) & 1) || (value<unsigned>(component + 32) & 24) ||
                !(value<unsigned char>(component + 40) & 1) || !event_owner(entity) ||
                !(value<unsigned>(entity + 56) & 0x100))
                return fail("component or owner state");
            const auto meta = value<Address>(component + 56), data = value<Address>(component + 16);
            if (!typed(meta, 0x1745d78) || string(value<Address>(meta + 16)) != "Scripts.Graph.common_Spawner_Template" ||
                value<std::uint16_t>(meta + 90) != 344 || meta < 45808 || !typed(data, 0x13bbb10) ||
                value<Address>(data + 32) != value<Address>(meta + 176) || !value<Address>(data + 32) ||
                !field_at(meta, "SpawnedEntity", 232, 8) || !field_at(meta, "isSpawningEnabled", 69, 1) ||
                !field_at(meta, "used", 67, 1) || !field_at(meta, "pendingUsed", 66, 1) ||
                value<unsigned char>(component + 69) != 1 || value<unsigned char>(component + 67) ||
                value<unsigned char>(component + 66) || (!allow_spawned && resolve(value<Address>(component + 232))))
                return fail("not a ready spawner");
            const auto method = meta - 45808 + 67320, nodes = method + 24, entry = nodes + 48;
            // spawner method is slab-relative. only call its verified entry when no platform entity has spawned yet!
            std::array<Address, 4> bindings{};
            if (!typed(method, 0x17473c8) || value<Address>(method + 16) != nodes || !typed(nodes, 0x1746090) ||
                value<unsigned>(nodes + 16) != 1 || value<unsigned>(nodes + 20) != 1 ||
                (value<std::uint64_t>(nodes + 24) & 0xfffffffffffffULL) != 8 || value<Address>(nodes + 32) != nodes + 40 ||
                value<Address>(nodes + 40) != entry || !typed(entry, 0x1746848) ||
                value<Address>(entry + 24) || value<Address>(entry + 32) || value<Address>(entry + 40) ||
                value<Address>(entry + 48) != meta - 45808 + 67544 || !typed(value<Address>(entry + 48), 0x13bc390) ||
                value<unsigned>(entry + 56) != 4 || !read(value<Address>(entry + 72), bindings) ||
                std::any_of(bindings.begin(), bindings.end(), [](Address p)
                            { return p != 0; }))
                return fail("spawn routine differs");
            return method;
        }
        // /nitro_probe research: a read-only report of the level's Nitro and TNT crates and their spawners.
        // It only reads (every read is a checked ReadProcessMemory) and never calls game code.
        std::string spawner_report(Address component)
        {
            const auto meta = value<Address>(component + 56);
            if (!typed(meta, 0x1745d78) || string(value<Address>(meta + 16)) != "Scripts.Graph.common_Spawner_Template")
                return {};
            const auto size = value<std::uint16_t>(meta + 90);
            const bool fields = field_at(meta, "SpawnedEntity", 232, 8) && field_at(meta, "isSpawningEnabled", 69, 1) &&
                                field_at(meta, "used", 67, 1) && field_at(meta, "pendingUsed", 66, 1);
            // the spawn routine layout the Crash 3 secret platform call verifies
            bool routine = false;
            if (size == 344 && meta >= 45808)
            {
                const auto method = meta - 45808 + 67320, nodes = method + 24, entry = nodes + 48;
                routine = typed(method, 0x17473c8) && value<Address>(method + 16) == nodes && typed(nodes, 0x1746090) &&
                          value<Address>(nodes + 40) == entry && typed(entry, 0x1746848) &&
                          value<Address>(entry + 48) == meta - 45808 + 67544 && typed(value<Address>(entry + 48), 0x13bc390);
            }
            std::ostringstream out;
            out << "spawner size " << size << (fields ? ", fields ok" : ", fields differ");
            if (fields)
                out << ", enabled " << unsigned(value<unsigned char>(component + 69)) << ", used "
                    << unsigned(value<unsigned char>(component + 67)) << ", pending "
                    << unsigned(value<unsigned char>(component + 66)) << ", crate "
                    << (resolve(value<Address>(component + 232)) ? "present" : "none");
            out << ", spawn routine " << (routine ? "KNOWN" : "unknown");
            return out.str();
        }
        void nitro_template_probe();
        void nitro_probe()
        {
            const auto world = value<Address>(base() + 0x1a64690);
            const auto key = level_key(string(value<Address>(world + 184)));
            const auto actor = player();
            std::array<float, 3> crash{};
            const bool located = actor && read(actor + 32, crash);
            std::cout << std::fixed << std::setprecision(1) << "Nitro probe: level " << (key.empty() ? "(hub or menu)" : key)
                      << ", Crash " << (located ? "at " + std::to_string(crash[0]) + ", " + std::to_string(crash[1]) + ", " + std::to_string(crash[2]) : "not found")
                      << "\n";
            unsigned nitros = 0, tnts = 0, shown = 0, routines = 0;
            bool listed_components = false;
            for (const auto owner : event_owners())
            {
                const auto name = string(value<Address>(owner + 128));
                const bool nitro = name.find("Nitro") != std::string::npos, tnt = name.find("TNT") != std::string::npos;
                if (!nitro && !tnt)
                    continue;
                ++(nitro ? nitros : tnts);
                std::string spawners;
                std::vector<std::string> graphs;
                for (const auto c : list(value<Address>(owner + 24), 64))
                {
                    if (!typed(c, 0x13bbbd0) || value<Address>(c + 24) != owner)
                        continue;
                    const auto meta = value<Address>(c + 56);
                    if (typed(meta, 0x1745d78))
                        graphs.push_back(string(value<Address>(meta + 16)));
                    const auto report = spawner_report(c);
                    if (report.empty())
                        continue;
                    routines += report.find("KNOWN") != std::string::npos;
                    spawners += (spawners.empty() ? "" : " | ") + report;
                }
                if (shown >= 12)
                    continue;
                ++shown;
                std::array<float, 3> at{};
                const bool placed = read(owner + 32, at);
                float distance = -1;
                if (placed && located)
                    distance = std::sqrt((at[0] - crash[0]) * (at[0] - crash[0]) + (at[1] - crash[1]) * (at[1] - crash[1]) +
                                         (at[2] - crash[2]) * (at[2] - crash[2]));
                std::cout << "Nitro probe: " << name << (placed ? " at " + std::to_string(at[0]) + ", " + std::to_string(at[1]) + ", " + std::to_string(at[2]) : "")
                          << (distance >= 0 ? " (" + std::to_string(static_cast<int>(distance)) + " from Crash)" : "") << " -> "
                          << (spawners.empty() ? "no spawner component" : spawners) << "\n";
                if (nitro && !listed_components)
                {
                    // one sample of what else is attached to a Nitro
                    listed_components = true;
                    std::string list_text;
                    for (const auto &g : graphs)
                        list_text += (list_text.empty() ? "" : ", ") + g;
                    std::cout << "Nitro probe: " << name << " scripts: " << (list_text.empty() ? "(none)" : list_text) << "\n";
                }
            }
            std::cout << "Nitro probe done: " << nitros << " Nitro and " << tnts << " TNT crates, " << routines
                      << " spawners with the known spawn routine. Nothing was changed.\n"
                      << std::flush;
            nitro_template_probe();
        }
        // /nitro_test research (experimental): move the nearest live Nitro crate next to Crash by rewriting
        // its entity position, its render-model translation and every copy /nitro_scan found. No game code is
        // called. Restarting the level restores the crate. A follow-up after one second shows what the game kept.
        std::string triple(const std::array<float, 3> &v)
        {
            return std::to_string(v[0]) + ", " + std::to_string(v[1]) + ", " + std::to_string(v[2]);
        }
        bool same_place(const float *a, const std::array<float, 3> &b, float tolerance)
        {
            return std::abs(a[0] - b[0]) < tolerance && std::abs(a[1] - b[1]) < tolerance && std::abs(a[2] - b[2]) < tolerance;
        }
        struct NitroPick
        {
            Address owner = 0, entity = 0, model = 0;
            float distance = 0;
            std::string name;
        };
        NitroPick nearest_nitro(const std::array<float, 3> &crash)
        {
            NitroPick pick;
            for (const auto owner : event_owners())
            {
                const auto name = string(value<Address>(owner + 128));
                if (name.rfind("Crate_Nitro", 0) != 0) // skips Crate_Switch_Nitro, the detonator
                    continue;
                const auto crate = crate_entity(owner);
                std::array<float, 3> at{};
                if (!crate || !read(crate + 32, at))
                    continue;
                const float d = std::sqrt((at[0] - crash[0]) * (at[0] - crash[0]) + (at[1] - crash[1]) * (at[1] - crash[1]) +
                                          (at[2] - crash[2]) * (at[2] - crash[2]));
                if (!pick.entity || d < pick.distance)
                    pick = {owner, crate, crate_model(crate), d, name};
            }
            return pick;
        }
        Address module_end()
        {
            static const Address end = []
            {
                const auto nt = base() + value<std::int32_t>(base() + 0x3c);
                return base() + value<std::uint32_t>(nt + 24 + 56); // OptionalHeader.SizeOfImage
            }();
            return end;
        }
        std::string hex(unsigned offset)
        {
            std::ostringstream out;
            out << "+0x" << std::hex << offset;
            return out.str();
        }
        // /nitro_scan research (read-only): from the crate's owner, entity and models, follow pointers up to three
        // levels deep and list every place that holds the crate's position as three floats.
        struct ScanHit
        {
            Address at;
            std::string path;
            std::array<float, 3> goal{}; // where /nitro_test put this copy
        };
        struct NitroScan
        {
            Address entity = 0;
            std::vector<ScanHit> hits;
        } nitro_scan_state;
        // the last crate /nitro_test moved and the spot it came from: /nitro_scan then also looks for copies that
        // still hold that old spot (where its hop returns it)
        struct NitroHome
        {
            Address entity = 0;
            std::array<float, 3> old{};
        } nitro_home;
        // research probes follow unknown pointers. only read pages that are committed, readable and not guard pages:
        // touching another thread's stack guard page can make the game crash later
        bool readable(Address at, std::size_t size)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (at < 0x10000 || !VirtualQuery(reinterpret_cast<void *>(at), &info, sizeof(info)) || info.State != MEM_COMMIT ||
                (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                                                                  PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
                return false;
            const auto end = reinterpret_cast<Address>(info.BaseAddress) + info.RegionSize;
            return at + size <= end || readable(end, at + size - end);
        }
        // copies of a position: x and y exact, height within a hop. a crate stacked on this one is a full crate
        // height (about 78) above, so it never matches
        bool crate_copy(const float *v, const std::array<float, 3> &t)
        {
            return std::abs(v[0] - t[0]) < 0.05f && std::abs(v[1] - t[1]) < 0.05f && std::abs(v[2] - t[2]) < 25;
        }
        // ---- V1.2.46 research for a Nitro in any level (read-only: every read is checked, no game code is called) ----
        // What a spawner holds that says what it makes, what the crate it made points to, which kinds of things in
        // the level have spawners, and (in the background) which Nitro names are anywhere in the game's memory. Run in
        // a level with Nitros and in one without (Crash 1 has none): the difference shows whether the Nitro's
        // description is loaded only with levels that use it, and what a borrowed spawner would need to make one.
        std::string printable_text(Address at, unsigned max = 64)
        {
            if (at < 0x10000 || at > 0x7ffffffeffffULL || !readable(at, 4))
                return {};
            const auto text = string(at, max);
            return text.size() >= 3 && std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 32 && c < 127; }) ? text : std::string{};
        }
        // an object's type (as an offset in the game's .exe) and up to `most` names it points to in its first `span` bytes
        std::string object_brief(Address p, unsigned span = 0x80, unsigned most = 3)
        {
            if (p < 0x10000 || p > 0x7ffffffeffffULL || !readable(p, 16))
                return {};
            std::ostringstream out;
            const auto vt = value<Address>(p);
            if (vt >= base() && vt < module_end())
                out << "type " << hex(static_cast<unsigned>(vt - base()));
            else if (const auto text = printable_text(p); !text.empty())
                return "\"" + text + "\"";
            else
                return {};
            unsigned shown = 0;
            for (unsigned off = 8; off < span && shown < most; off += 8)
                if (const auto text = printable_text(value<Address>(p + off)); !text.empty())
                    out << " " << hex(off) << "=\"" << text << "\"", ++shown;
            return out.str();
        }
        // a pointer-sized value: what it points to, directly or as a handle
        std::string pointer_brief(Address v)
        {
            if (!v)
                return "0";
            std::ostringstream out;
            out << "0x" << std::hex << v << std::dec;
            if (const auto direct = object_brief(v); !direct.empty())
                out << " [" << direct << "]";
            if (const auto target = resolve(v); target && target != v)
                if (const auto via = object_brief(target, 0x100, 4); !via.empty())
                    out << " -> [" << via << "]";
            return out.str();
        }
        // every field a type's description lists, with its value in `object`
        void dump_fields(Address object, Address meta, const std::string &who, unsigned most = 80)
        {
            const unsigned count = value<unsigned>(meta + 24), size = value<std::uint16_t>(meta + 90);
            const auto fields = value<Address>(meta + 40);
            std::cout << who << ": " << (string(value<Address>(meta + 16), 120).empty() ? "(unnamed type)" : string(value<Address>(meta + 16), 120))
                      << ", " << size << " bytes, " << count << " fields\n";
            if (!fields || count > 512)
                return;
            for (unsigned i = 0; i < count && i < most; ++i)
            {
                const auto field = value<Address>(fields + i * 8);
                const unsigned offset = value<std::uint16_t>(field + 24), bytes = value<std::uint16_t>(field + 22);
                const auto kind = value<Address>(field);
                std::ostringstream line;
                line << who << ":   " << string(value<Address>(field + 48), 64) << " @" << hex(offset) << " (" << bytes << " bytes, field type "
                     << (kind >= base() && kind < module_end() ? hex(static_cast<unsigned>(kind - base())) : std::string("?")) << ") = ";
                if (!bytes || (size && offset + bytes > size + 0x40u))
                    line << "-";
                else if (bytes == 1)
                    line << unsigned(value<unsigned char>(object + offset));
                else if (bytes == 2)
                    line << value<std::uint16_t>(object + offset);
                else if (bytes == 4)
                    line << value<int>(object + offset) << " / " << value<float>(object + offset);
                else if (bytes == 8)
                    line << pointer_brief(value<Address>(object + offset));
                else
                {
                    std::array<float, 4> f{};
                    read(object + offset, f);
                    line << "(" << f[0] << ", " << f[1] << ", " << f[2] << (bytes >= 16 ? ", " + std::to_string(f[3]) : std::string()) << ")";
                }
                std::cout << line.str() << "\n";
            }
        }
        // the objects an object points to in its first `span` bytes, with their types and names
        void dump_pointees(Address object, const std::string &who, unsigned span = 0x200)
        {
            std::cout << who << ": " << object_brief(object, 0x100, 6) << "\n";
            for (unsigned off = 8; off < span; off += 8)
            {
                const auto p = value<Address>(object + off);
                const auto brief = object_brief(p, 0x100, 4);
                if (!brief.empty())
                    std::cout << who << ":   " << hex(off) << " -> " << brief << "\n";
            }
        }
        std::string kind_of(const std::string &name)
        {
            auto end = name.size();
            while (end && (std::isdigit(static_cast<unsigned char>(name[end - 1])) || name[end - 1] == '_'))
                --end;
            return name.substr(0, end);
        }
        std::atomic_bool nitro_memory_scan_running{false};
        // every block of the game's data memory (not the .exe or the mod's own code), read through ReadProcessMemory a
        // block at a time (so memory freed meanwhile only ends that block's read). Blocks overlap by 256 bytes.
        template <class F> std::uint64_t for_each_memory_block(F &&f)
        {
            std::uint64_t scanned = 0;
            std::vector<char> block(4 << 20);
            for (Address at = 0x10000; at < 0x7ffffffe0000ULL;)
            {
                MEMORY_BASIC_INFORMATION info{};
                if (!VirtualQuery(reinterpret_cast<void *>(at), &info, sizeof(info)))
                    break;
                const auto region = reinterpret_cast<Address>(info.BaseAddress), end = region + info.RegionSize;
                if (info.State == MEM_COMMIT && info.Type != MEM_IMAGE && !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS | PAGE_NOCACHE | PAGE_WRITECOMBINE)) &&
                    (info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
                    for (Address chunk = region; chunk < end; chunk += block.size() - 256)
                    {
                        const auto want = static_cast<std::size_t>(std::min<Address>(block.size(), end - chunk));
                        SIZE_T got = 0;
                        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(chunk), block.data(), want, &got) || !got)
                            break;
                        scanned += got;
                        f(chunk, std::string_view(block.data(), got));
                        if (want < block.size())
                            break;
                    }
                at = end > at ? end : at + 0x1000;
            }
            return scanned;
        }
        // every distinct name holding one of `needles`, with how often it is there. The client's own lists (its
        // location tables hold every crate's name in quotes) are left out.
        std::map<std::string, unsigned> memory_names(const std::vector<std::string> &needles, std::uint64_t &scanned)
        {
            std::map<std::string, unsigned> found;
            scanned = for_each_memory_block([&](Address, std::string_view view)
                                            {
                for (const auto &needle : needles)
                    for (auto hit = view.find(needle); hit != view.npos; hit = view.find(needle, hit + needle.size()))
                    {
                        auto from = hit, to = hit + needle.size();
                        const auto plain = [](char c) { return c > 32 && c < 127; };
                        while (from > 0 && hit - from < 160 && plain(view[from - 1]))
                            --from;
                        while (to < view.size() && to - hit < 160 && plain(view[to]))
                            ++to;
                        const auto text = view.substr(from, to - from);
                        if (text.find_first_of("\"{}") != text.npos)
                            continue;
                        if (found.size() < 4000 || found.count(std::string(text)))
                            ++found[std::string(text)];
                    } });
            return found;
        }
        // where `text` sits in memory as a whole string (a zero before and after it)
        std::vector<Address> memory_strings(const std::string &text, unsigned most = 64)
        {
            std::vector<Address> result;
            for_each_memory_block([&](Address base_at, std::string_view view)
                                  {
                for (auto hit = view.find(text); hit != view.npos && result.size() < most; hit = view.find(text, hit + 1))
                    if (hit > 0 && view[hit - 1] == 0 && hit + text.size() < view.size() && view[hit + text.size()] == 0 &&
                        std::find(result.begin(), result.end(), base_at + hit) == result.end())
                        result.push_back(base_at + hit); });
            return result;
        }
        // every 8-byte-aligned place holding one of `values`
        std::vector<std::pair<Address, Address>> memory_pointers(const std::set<Address> &values, unsigned most = 256)
        {
            std::vector<std::pair<Address, Address>> result; // where, which value
            if (values.empty())
                return result;
            const Address lo = *values.begin(), hi = *values.rbegin();
            for_each_memory_block([&](Address base_at, std::string_view view)
                                  {
                const auto first = (8 - base_at % 8) % 8;
                for (std::size_t k = first; k + 8 <= view.size() && result.size() < most; k += 8)
                {
                    Address v;
                    std::memcpy(&v, view.data() + k, 8);
                    if (v < lo || v > hi)
                        continue;
                    if (values.count(v) && std::find_if(result.begin(), result.end(), [&](const auto &r) { return r.first == base_at + k; }) == result.end())
                        result.push_back({base_at + k, v});
                } });
            return result;
        }
        // V1.2.50: the Nitro's description and template, found in memory. A Nitro spawner's EntityToSpawn is a handle to
        // a template entity (+0x141d490) whose description (+0x40, type +0x141d3f0) is named
        // "Levels\Crash1\crates\Crash_Crate_Nitro" (+0x48). Every level's archive holds the Nitro's files, so the
        // description (and maybe a template) can be loaded where no Nitro spawner is.
        struct NitroTemplateFind
        {
            std::string level;
            Address entity = 0, data = 0;
            std::uint64_t when = 0;
            bool done = false;
        };
        std::mutex nitro_find_mutex;
        NitroTemplateFind nitro_find;
        // the Nitro's template: an entity (+0x141d490) described as "...Crash_Crate_Nitro", not a crate live in the level
        bool nitro_template_valid(Address entity)
        {
            const auto data = value<Address>(entity + 0x40);
            return entity && typed(entity, 0x141d490) && typed(data, 0x141d3f0) && !event_owner(entity) &&
                   string(value<Address>(data + 0x48)).find("Crash_Crate_Nitro") != std::string::npos;
        }
        std::string find_nitro_template(const std::string &level)
        {
            std::ostringstream out;
            const auto names = memory_strings("Levels\\Crash1\\crates\\Crash_Crate_Nitro");
            std::set<Address> name_at(names.begin(), names.end()), datas;
            for (const auto &[where, v] : memory_pointers(name_at))
                if (typed(where - 0x48, 0x141d3f0))
                    datas.insert(where - 0x48);
            out << "Nitro find (" << (level.empty() ? "no level" : "level " + level) << "): " << names.size() << " copies of its name, "
                << datas.size() << " Nitro descriptions\n";
            std::set<Address> users;
            for (const auto &[where, v] : memory_pointers(datas))
            {
                const auto e = where - 0x40, type = value<Address>(e);
                if (type >= base() && type < module_end())
                {
                    users.insert(e);
                    out << "Nitro find:   used by 0x" << std::hex << e << std::dec << " type " << hex(static_cast<unsigned>(type - base())) << " \""
                        << string(value<Address>(e + 0x80), 64) << "\"" << (event_owner(e) ? " (live in the level)" : "") << "\n";
                }
            }
            NitroTemplateFind pick;
            pick.level = level, pick.done = true, pick.when = GetTickCount64();
            // the template (spawned Nitros are named ..._Spawned_Entity and live in the level)
            for (const auto e : users)
                if (!pick.entity && nitro_template_valid(e))
                    pick.entity = e, pick.data = value<Address>(e + 0x40);
            out << "Nitro find: " << (pick.entity ? "the Nitro's template is loaded here" : "no Nitro template loaded here") << "\n";
            std::lock_guard lock(nitro_find_mutex);
            nitro_find = pick;
            return out.str();
        }
        // the Nitro's template only, in the background (about 2 seconds of reading memory), once per level
        void nitro_template_search(const std::string &level)
        {
            if (nitro_memory_scan_running.exchange(true))
                return;
            std::thread([level]
                        {
                const auto start = GetTickCount64();
                const auto report = find_nitro_template(level);
                std::cout << report << "Nitro find: done in " << (GetTickCount64() - start) << " ms\n" << std::flush;
                nitro_memory_scan_running = false; })
                .detach();
        }
        void nitro_memory_scan(const std::string &level)
        {
            if (nitro_memory_scan_running.exchange(true))
                return;
            std::thread([level]
                        {
                const auto start = GetTickCount64();
                std::ostringstream out;
                const auto report = [&](const char *label, const std::map<std::string, unsigned> &found, std::uint64_t scanned, unsigned most)
                {
                    out << label << ": " << found.size() << " different names in " << scanned / (1024 * 1024) << " MB of game memory ("
                        << (level.empty() ? "no level" : "level " + level) << ")\n";
                    unsigned shown = 0;
                    for (const auto &[text, count] : found)
                        if (shown++ < most)
                            out << label << ":   " << text << (count > 1 ? " x" + std::to_string(count) : std::string()) << "\n";
                    if (found.size() > most)
                        out << label << ":   ... and " << found.size() - most << " more\n";
                };
                std::uint64_t scanned = 0;
                const auto nitro = memory_names({"Nitro", "nitro", "NITRO"}, scanned);
                report("Nitro memory", nitro, scanned, 200);
                // Crash's own textures: which copies of his fur, clothes and hair are loaded (Fumbling in the Dark showed
                // some of his colors and not others)
                const auto crash = memory_names({"CrashFur", "CrashCloth", "CrashHair", "crashfur", "crashcloth", "crashhair"}, scanned);
                report("Crash textures in memory", crash, scanned, 120);
                out << find_nitro_template(level);
                out << "Nitro memory: done in " << (GetTickCount64() - start) << " ms\n";
                std::cout << out.str() << std::flush;
                nitro_memory_scan_running = false; })
                .detach();
        }
        void nitro_template_probe()
        {
            const auto key = level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184)));
            // the kinds of things in the level (names without their numbers), and which kinds have a spawner
            std::map<std::string, unsigned> kinds;
            std::map<std::string, std::pair<unsigned, Address>> spawners; // kind -> how many, one spawner component
            std::map<std::string, Address> owners;                        // kind -> one owner with a spawner
            for (const auto owner : event_owners())
            {
                const auto kind = kind_of(string(value<Address>(owner + 128)));
                ++kinds[kind];
                for (const auto c : list(value<Address>(owner + 24), 64))
                    if (typed(c, 0x13bbbd0) && value<Address>(c + 24) == owner && typed(value<Address>(c + 56), 0x1745d78) &&
                        string(value<Address>(value<Address>(c + 56) + 16)) == "Scripts.Graph.common_Spawner_Template")
                    {
                        auto &s = spawners[kind];
                        if (!s.first++)
                            s.second = c, owners[kind] = owner;
                    }
            }
            std::vector<std::pair<unsigned, std::string>> sorted;
            for (const auto &[kind, count] : kinds)
                sorted.push_back({count, kind});
            std::sort(sorted.rbegin(), sorted.rend());
            std::ostringstream census;
            census << "Nitro research: level " << (key.empty() ? "(none)" : key) << ", " << sorted.size() << " kinds of things:";
            for (std::size_t i = 0; i < sorted.size() && i < 80; ++i)
                census << (i % 8 ? ", " : "\nNitro research:   ") << sorted[i].second << " x" << sorted[i].first
                       << (spawners.count(sorted[i].second) ? " (spawner)" : "");
            std::cout << census.str() << "\n";
            // up to 8 kinds of spawner in full: Nitro first, then TNT, then the most common
            std::vector<std::string> pick;
            for (const auto &[kind, s] : spawners)
                if (kind.find("Nitro") != std::string::npos)
                    pick.push_back(kind);
            for (const auto &[kind, s] : spawners)
                if (kind.find("TNT") != std::string::npos && pick.size() < 8)
                    pick.push_back(kind);
            for (const auto &[count, kind] : sorted)
                if (spawners.count(kind) && pick.size() < 8 && std::find(pick.begin(), pick.end(), kind) == pick.end())
                    pick.push_back(kind);
            std::cout << "Nitro research: " << spawners.size() << " kinds have a spawner; in full: ";
            for (std::size_t i = 0; i < pick.size(); ++i)
                std::cout << (i ? ", " : "") << pick[i];
            std::cout << "\n";
            for (const auto &kind : pick)
            {
                const auto c = spawners[kind].second, owner = owners[kind];
                const auto who = "Spawner " + kind;
                dump_fields(c, value<Address>(c + 56), who);
                if (const auto data = value<Address>(c + 16); data)
                {
                    dump_fields(data, value<Address>(data + 32), who + " data");
                    // the template it makes its entity from
                    if (field_offset(value<Address>(data + 32), "EntityToSpawn", 8) == 0x48)
                        if (const auto t = resolve(value<Address>(data + 0x48)))
                            dump_pointees(t, who + " template", 0x100);
                }
                dump_pointees(owner, who + " owner");
                if (const auto made = resolve(value<Address>(c + 232)))
                {
                    std::string scripts;
                    for (const auto m : list(value<Address>(made + 24), 64))
                        if (typed(m, 0x13bbbd0) && typed(value<Address>(m + 56), 0x1745d78))
                            scripts += (scripts.empty() ? "" : ", ") + string(value<Address>(value<Address>(m + 56) + 16), 120);
                    std::cout << who << " made: " << string(value<Address>(made + 128)) << ", scripts: " << (scripts.empty() ? "(none)" : scripts) << "\n";
                    dump_pointees(made, who + " made");
                }
                else
                    std::cout << who << " made: nothing out right now\n";
            }
            std::cout << "Nitro research: done here; the memory search runs in the background (a few seconds)\n" << std::flush;
            nitro_memory_scan(key);
        }
        // follow pointers from the crate's spawner, entity, models and every script on them (each script read in
        // full) up to four levels deep, and list every place holding one of the targets
        // The search is bounded by every object it tries (also ones it can't read: a heap full of stale pointers
        // once made it run for millions of steps and freeze the game) and by the objects it queues.
        // Targets near the origin are refused: a crate created this frame still has an empty (0,0,0) display
        // position, and "matching" every zero in memory is what froze the game in V1.1.57 and V1.1.58.
        bool heap_page(Address at, std::size_t size);
        std::vector<ScanHit> position_scan(const NitroPick &pick, const std::vector<std::array<float, 3>> &all_targets,
                                           std::vector<unsigned> &which, unsigned &visited, unsigned max_objects = 60000,
                                           unsigned max_depth = 4, unsigned max_hits = 400, bool heap_only = false)
        {
            std::vector<std::array<float, 3>> targets;
            for (const auto &t : all_targets)
                if (std::abs(t[0]) + std::abs(t[1]) > 1 && std::isfinite(t[0]) && std::isfinite(t[1]) && std::isfinite(t[2]))
                    targets.push_back(t);
            struct Node
            {
                Address at;
                std::string path;
                unsigned depth, span;
            };
            unsigned tried = 0, queued = 0;
            std::deque<Node> queue;
            std::set<Address> seen, hit_at;
            std::vector<Node> roots{{pick.owner, "owner", 0, 2048}, {pick.entity, "entity", 0, 2048},
                                    {value<Address>(pick.entity + 176), "model component", 0, 2048}, {pick.model, "model", 0, 2048}};
            for (const auto &[holder, label] : {std::pair<Address, const char *>{pick.owner, "owner"}, {pick.entity, "entity"}})
                for (const auto c : list(value<Address>(holder + 24), 64))
                {
                    const auto meta = value<Address>(c + 56);
                    const bool graph = typed(meta, 0x1745d78);
                    auto name = graph ? string(value<Address>(meta + 16)) : std::string("component");
                    if (name.rfind("Scripts.Graph.", 0) == 0)
                        name.erase(0, 14);
                    const unsigned size = graph ? value<std::uint16_t>(meta + 90) : 0;
                    roots.push_back({c, std::string(label) + " script " + name, 0, std::min(8192u, std::max(2048u, size))});
                }
            for (const auto &root : roots)
                if (root.at && seen.insert(root.at).second)
                    queue.push_back(root);
            std::vector<ScanHit> hits;
            which.clear();
            visited = 0;
            const auto image_start = base(), image_end = module_end();
            std::vector<unsigned char> bytes(8192);
            while (!queue.empty() && visited < max_objects && tried < max_objects * 2)
            {
                const auto node = std::move(queue.front());
                queue.pop_front();
                ++tried;
                SIZE_T got = 0;
                unsigned size = 0;
                for (const unsigned attempt : {node.span, 512u, 64u})
                    if (attempt <= node.span && (heap_only ? heap_page(node.at, attempt) : readable(node.at, attempt)) &&
                        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(node.at), bytes.data(), attempt, &got) && got == attempt)
                    {
                        size = attempt;
                        break;
                    }
                if (!size)
                    continue;
                ++visited;
                for (unsigned o = 0; o + 12 <= size; o += 4)
                {
                    float v[3];
                    std::memcpy(v, bytes.data() + o, sizeof(v));
                    for (unsigned t = 0; t < targets.size(); ++t)
                        if (crate_copy(v, targets[t]))
                        {
                            if (hit_at.insert(node.at + o).second)
                            {
                                float w = 0;
                                if (o + 16 <= size)
                                    std::memcpy(&w, bytes.data() + o + 12, sizeof(w));
                                hits.push_back({node.at + o, node.path + " " + hex(o) + (w == 1.0f ? " (w 1)" : "")});
                                which.push_back(t);
                                if (hits.size() >= max_hits)
                                    return hits;
                            }
                            break;
                        }
                }
                if (node.depth >= max_depth)
                    continue;
                for (unsigned o = 0; o + 8 <= size; o += 8)
                {
                    Address p = 0;
                    std::memcpy(&p, bytes.data() + o, sizeof(p));
                    if (queued >= max_objects * 2 || visited + queue.size() >= max_objects || p < 0x10000 || p > 0x7ffffffeffffULL ||
                        (p & 7) || (p >= image_start && p < image_end) || !seen.insert(p).second)
                        continue;
                    ++queued;
                    queue.push_back({p, node.path + " " + hex(o) + " ->", node.depth + 1, 512});
                }
            }
            return hits;
        }
        void nitro_scan()
        {
            const auto actor = player();
            std::array<float, 3> crash{};
            if (!actor || !read(actor + 32, crash))
            {
                std::cout << "Nitro scan: Crash not found. Run it inside a level.\n" << std::flush;
                return;
            }
            const auto pick = nearest_nitro(crash);
            if (!pick.entity)
            {
                std::cout << "Nitro scan: no live Nitro crate in this level.\n" << std::flush;
                return;
            }
            // targets: current position, render translation if different, and the pre-test spot
            std::vector<std::array<float, 3>> targets(1);
            read(pick.entity + 32, targets[0]);
            Matrix matrix{};
            if (pick.model && read(pick.model + 48, matrix) && !same_place(&matrix[12], targets[0], 0.05f))
                targets.push_back({matrix[12], matrix[13], matrix[14]});
            const bool tested = nitro_home.entity == pick.entity && !same_place(nitro_home.old.data(), targets[0], 0.05f);
            const unsigned old_target = tested ? static_cast<unsigned>(targets.size()) : ~0u;
            if (tested)
                targets.push_back(nitro_home.old);
            std::vector<unsigned> which;
            unsigned visited = 0;
            auto found = position_scan(pick, targets, which, visited);
            std::vector<ScanHit> hits, old_hits;
            for (unsigned i = 0; i < found.size(); ++i)
                (which[i] == old_target ? old_hits : hits).push_back(found[i]);
            std::cout << "Nitro scan: " << pick.name << " (" << static_cast<int>(pick.distance) << " from Crash) at " << triple(targets[0])
                      << ": searched " << visited << " objects, " << hits.size() << " copies of its position"
                      << (tested ? ", " + std::to_string(old_hits.size()) + " still holding its old spot " + triple(nitro_home.old) : std::string())
                      << "\n";
            for (unsigned i = 0; i < old_hits.size() && i < 30; ++i)
                std::cout << "Nitro scan:   " << old_hits[i].path << " [old spot]\n";
            for (unsigned i = 0; i < hits.size() && i < 40; ++i)
                std::cout << "Nitro scan:   " << hits[i].path << "\n";
            std::cout << "Nitro scan done. Nothing was changed. /nitro_test now also moves these copies.\n" << std::flush;
            nitro_scan_state = {pick.entity, std::move(hits)};
        }
        // /nitro_test holds the crate next to Crash for this long so it can be touched, putting back any copy the
        // game moves and logging when and where each copy was first moved
        constexpr std::uint64_t nitro_hold_ms = 15000;
        struct NitroTest
        {
            Address entity = 0, actor = 0;
            std::string level;
            std::uint64_t started = 0;
            bool reported = false, searched = false;
            NitroPick pick;
            std::array<float, 3> target{};
            std::vector<ScanHit> written;
            std::vector<bool> moved;
        } nitro_test_state;
        bool live_crate(Address entity)
        {
            return typed(entity, 0x141d490) && (value<unsigned>(entity + 56) & 4);
        }
        void nitro_test()
        {
            const auto actor = player();
            std::array<float, 3> crash{};
            if (!actor || !read(actor + 32, crash))
            {
                std::cout << "Nitro test: Crash not found. Run it inside a level.\n" << std::flush;
                return;
            }
            const auto pick = nearest_nitro(crash);
            if (!pick.entity)
            {
                std::cout << "Nitro test: no live Nitro crate in this level.\n" << std::flush;
                return;
            }
            std::array<float, 3> before{};
            read(pick.entity + 32, before);
            std::vector<ScanHit> copies{{pick.entity + 32, "entity +0x20"}};
            Matrix matrix{};
            if (pick.model && read(pick.model + 48, matrix))
                copies.push_back({pick.model + 48 + 48, "model +0x60"});
            if (nitro_scan_state.entity == pick.entity)
                for (const auto &hit : nitro_scan_state.hits)
                    if (hit.path.rfind("owner", 0) != 0 && // the spawner: moving it moves every respawn
                        std::none_of(copies.begin(), copies.end(), [&](const ScanHit &c) { return c.at == hit.at; }))
                        copies.push_back(hit);
            // 150 units from Crash along the first axis, at Crash's height
            const std::array<float, 3> target{crash[0] + 150, crash[1], crash[2]};
            std::vector<ScanHit> written;
            unsigned skipped = 0;
            for (const auto &copy : copies)
            {
                // only overwrite a copy that still holds the crate's position. the crate hops, so a copy may sit
                // up to a hop higher or lower than the others (the physics copy did)
                std::array<float, 3> now{};
                const bool holds = read(copy.at, now) &&
                                   (crate_copy(now.data(), before) || (matrix[15] == 1.0f && crate_copy(now.data(), {matrix[12], matrix[13], matrix[14]})));
                // each copy moves by the same distance and keeps its own offset: the physics body sits at the
                // crate's centre, 76 units up, and sinking it into the floor makes the game reset the crate
                const std::array<float, 3> goal{target[0] + now[0] - before[0], target[1] + now[1] - before[1], target[2] + now[2] - before[2]};
                if (holds && write(copy.at, goal))
                {
                    written.push_back(copy);
                    written.back().goal = goal;
                }
                else
                    ++skipped;
            }
            nitro_home = {pick.entity, before};
            const auto level = level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184)));
            nitro_test_state = {pick.entity, actor, level, GetTickCount64(), false, false, pick, target, written, std::vector<bool>(written.size())};
            std::cout << "Nitro test: " << pick.name << " (" << static_cast<int>(pick.distance) << " from Crash) moved from " << triple(before)
                      << " to " << triple(target) << ": " << written.size() << " copies written"
                      << (skipped ? ", " + std::to_string(skipped) + " skipped (no longer matched)" : "")
                      << (nitro_scan_state.entity == pick.entity ? "" : ". Tip: run /nitro_scan first to move every copy")
                      << ". Held there for 15 seconds: touch it!\n" << std::flush;
        }
        void nitro_test_followup()
        {
            auto &t = nitro_test_state;
            if (!t.started)
                return;
            const auto now = GetTickCount64(), elapsed = now - t.started;
            if (!live_crate(t.entity))
            {
                std::cout << "Nitro test: the Nitro is gone after " << elapsed << " ms (broken or exploded)\n" << std::flush;
                t = {};
                return;
            }
            // a respawn or level change can free the copies: stop writing at once
            if (player() != t.actor || level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184))) != t.level)
            {
                std::cout << "Nitro test: stopped holding after " << elapsed << " ms (Crash respawned or the level changed)\n" << std::flush;
                t = {};
                return;
            }
            // the first time the game pulls the crate back, look for whatever still holds the spot it went back to
            // (its home) and move those too, so its next hop starts next to Crash
            if (!t.searched)
                for (unsigned i = 0; i < t.written.size(); ++i)
                {
                    std::array<float, 3> at{};
                    if (!read(t.written[i].at, at) || same_place(at.data(), t.written[i].goal, 1.0f))
                        continue;
                    t.searched = true;
                    // where the crate itself went back to: this copy's value minus its own offset
                    const std::array<float, 3> spot{at[0] - (t.written[i].goal[0] - t.target[0]), at[1] - (t.written[i].goal[1] - t.target[1]),
                                                    at[2] - (t.written[i].goal[2] - t.target[2])};
                    std::vector<unsigned> which;
                    unsigned visited = 0;
                    const auto found = position_scan(t.pick, {spot}, which, visited);
                    unsigned added = 0;
                    for (const auto &hit : found)
                    {
                        if (hit.path.rfind("owner", 0) == 0 ||
                            std::any_of(t.written.begin(), t.written.end(), [&](const ScanHit &w) { return w.at == hit.at; }))
                            continue;
                        std::array<float, 3> now{};
                        if (!read(hit.at, now))
                            continue;
                        const std::array<float, 3> goal{t.target[0] + now[0] - spot[0], t.target[1] + now[1] - spot[1], t.target[2] + now[2] - spot[2]};
                        if (!write(hit.at, goal))
                            continue;
                        t.written.push_back(hit);
                        t.written.back().goal = goal;
                        t.moved.push_back(false);
                        ++added;
                        std::cout << "Nitro test:   new copy " << hit.path << "\n";
                    }
                    std::cout << "Nitro test: the game pulled it back to " << triple(spot) << " after " << elapsed << " ms; searched "
                              << visited << " objects and moved " << added << " more copies that still held that spot\n" << std::flush;
                    break;
                }
            // hold: put back any copy the game moved, logging the first move of each
            for (unsigned i = 0; i < t.written.size(); ++i)
            {
                std::array<float, 3> at{};
                if (!read(t.written[i].at, at) || same_place(at.data(), t.written[i].goal, 1.0f))
                    continue;
                // the game reused this memory for something else: stop touching it
                if (std::abs(at[0] - nitro_home.old[0]) > 1 || std::abs(at[1] - nitro_home.old[1]) > 1)
                {
                    std::cout << "Nitro test: " << t.written[i].path << " now holds something else; no longer holding it\n" << std::flush;
                    t.written.erase(t.written.begin() + i);
                    t.moved.erase(t.moved.begin() + i);
                    --i;
                    continue;
                }
                if (!t.moved[i])
                {
                    t.moved[i] = true;
                    std::cout << "Nitro test: at " << elapsed << " ms the game moved " << t.written[i].path << " to " << triple(at)
                              << (std::abs(at[0] - nitro_home.old[0]) < 1 && std::abs(at[1] - nitro_home.old[1]) < 1 ? " (its old spot)" : "")
                              << "; putting it back\n" << std::flush;
                }
                write(t.written[i].at, t.written[i].goal);
            }
            if (!t.reported && elapsed >= 1000)
            {
                t.reported = true;
                const auto moved = static_cast<unsigned>(std::count(t.moved.begin(), t.moved.end(), true));
                std::cout << "Nitro test after 1 second: " << t.written.size() - moved << " of " << t.written.size()
                          << " copies untouched by the game\n" << std::flush;
            }
            if (elapsed >= nitro_hold_ms)
            {
                const auto moved = static_cast<unsigned>(std::count(t.moved.begin(), t.moved.end(), true));
                std::cout << "Nitro test: stopped holding after 15 seconds; the game tried to move " << moved << " of "
                          << t.written.size() << " copies. Run /nitro_scan to look for copies that still hold its old spot.\n"
                          << std::flush;
                t = {};
            }
        }
        // Nitro Trap: the game spawns a real Nitro ahead of Crash. A Nitro spawner's routine only runs
        // while its crate is out, so the crate of the ready spawner closest to the spot is set aside, the spawner is pointed at the
        // spot, the game's own spawn routine runs (the one the Crash 3 secret platform uses), and then the spawner
        // gets its position and its own crate back, so that crate stays normal and dying never doubles it.
        // The spot is ahead of Crash in the direction he is going, about 200 units (1.3 crates) away: close enough to
        // be a surprise, far enough to jump it or stop (230 until V1.2.69). In the middle of a jump or a fall it appears
        // straight ahead at his height and floats there (V1.2.69). On the ground, floor he stood on earlier in this
        // level (an earlier try, or ground he came back over) comes first; otherwise it goes straight ahead along the
        // line he is running, at his ground height and following the slope he is on. Never on a crate.
        static constexpr float nitro_ahead = 200, nitro_near = 165, nitro_far = 260;
        // where the player is for the Nitro Trap: Crash or Coco, or the hog, polar bear, jet board or jet ski they ride
        // (crash_position only finds them on foot)
        bool trap_actor_position(std::array<float, 3> &at)
        {
            const auto actor = controlled_actor();
            return actor && read(actor + 32, at);
        }
        struct PathSample
        {
            std::array<float, 3> at;
            std::uint64_t time;
        };
        std::deque<PathSample> crash_path;
        std::vector<std::array<float, 3>> level_floor; // flat ground Crash stood on in this level, every try
        std::string crash_path_level;
        void record_crash_path(const std::string &level)
        {
            const auto now = GetTickCount64();
            if (level != crash_path_level)
                crash_path.clear(), level_floor.clear(), crash_path_level = level;
            if (!crash_path.empty() && now - crash_path.back().time < 100)
                return;
            std::array<float, 3> at{};
            if (!trap_actor_position(at))
                return;
            // a respawn or teleport starts a new path (the floor seen so far stays known)
            if (!crash_path.empty() && std::hypot(at[0] - crash_path.back().at[0], at[1] - crash_path.back().at[1]) > 1500)
                crash_path.clear();
            crash_path.push_back({at, now});
            while (crash_path.size() > 80)
                crash_path.pop_front();
            // flat ground: height steady for 0.2 s either side (not a jump, a fall or a steep slope)
            if (crash_path.size() < 5)
                return;
            const auto i = crash_path.size() - 3;
            const auto &floor = crash_path[i].at;
            float low = floor[2], high = floor[2];
            for (std::size_t j = i - 2; j <= i + 2; ++j)
                low = std::min(low, crash_path[j].at[2]), high = std::max(high, crash_path[j].at[2]);
            if (high - low < 15 && level_floor.size() < 4000 &&
                std::none_of(level_floor.begin(), level_floor.end(), [&](const std::array<float, 3> &f)
                             { return std::abs(f[0] - floor[0]) < 60 && std::abs(f[1] - floor[1]) < 60 && std::abs(f[2] - floor[2]) < 40; }))
                level_floor.push_back(floor);
        }
        // the way Crash is going: his movement over half a second, from the latest half second in the last 3 seconds
        // in which he covered at least 60 units
        bool crash_heading(float &hx, float &hy, std::size_t &end, std::uint64_t window_ms = 3000)
        {
            const auto n = crash_path.size();
            if (n < 6)
                return false;
            const auto latest = crash_path.back().time;
            for (std::size_t i = n - 1; i >= 5 && latest - crash_path[i].time <= window_ms; --i)
            {
                const auto &a = crash_path[i - 5].at, &b = crash_path[i].at;
                const float dx = b[0] - a[0], dy = b[1] - a[1], d = std::hypot(dx, dy);
                if (d >= 60)
                {
                    hx = dx / d, hy = dy / d, end = i;
                    return true;
                }
            }
            return false;
        }
        // the way Crash faces (V1.2.74): one axis of his transform, flat. Which axis (and which way along it) is his front
        // is learned while he runs: the one that keeps pointing where he goes, in more than one direction. Then a jump
        // on the spot (even right after a respawn, with no running since) still has an ahead. Learned once per kind of
        // character or ride, for the whole game; until he has run around a bit, there is none.
        struct FacingLearn
        {
            std::array<int, 6> score{};
            std::array<std::array<float, 2>, 6> first{};
            std::array<bool, 6> has_first{}, varied{};
            int axis = -1;
        };
        std::map<Address, FacingLearn> facing_learned; // by the actor's type
        // the six candidates: the transform's x, y and z axes, each either way, flat and of length 1 (or 0 if upright)
        bool actor_axes(Address &type, std::array<std::array<float, 2>, 6> &axes)
        {
            const auto actor = controlled_actor();
            Matrix m{};
            if (!actor || !value<Address>(actor + 48) || !entity_matrix(actor, m))
                return false;
            type = value<Address>(actor);
            for (unsigned i = 0; i < 3; ++i)
            {
                const float x = m[i * 4], y = m[i * 4 + 1], length = std::hypot(x, y);
                const bool flat = std::isfinite(length) && length > 0.3f;
                axes[i * 2] = flat ? std::array<float, 2>{x / length, y / length} : std::array<float, 2>{0, 0};
                axes[i * 2 + 1] = flat ? std::array<float, 2>{-x / length, -y / length} : std::array<float, 2>{0, 0};
            }
            return true;
        }
        void learn_facing()
        {
            float hx = 0, hy = 0;
            std::size_t end = 0;
            // only while he is going somewhere right now
            if (!crash_heading(hx, hy, end, 600) || end + 1 != crash_path.size())
                return;
            static std::uint64_t last = 0;
            if (crash_path.back().time == last)
                return;
            last = crash_path.back().time;
            Address type = 0;
            std::array<std::array<float, 2>, 6> axes{};
            if (!actor_axes(type, axes))
                return;
            auto &l = facing_learned[type];
            for (unsigned i = 0; i < 6; ++i)
            {
                const float dot = axes[i][0] * hx + axes[i][1] * hy;
                if (dot > 0.9f)
                {
                    l.score[i] = std::min(l.score[i] + 1, 1000);
                    if (!l.has_first[i])
                        l.first[i] = {hx, hy}, l.has_first[i] = true;
                    // the same axis pointed his way while he went a quite different way: it turns with him
                    else if (l.first[i][0] * hx + l.first[i][1] * hy < 0.5f)
                        l.varied[i] = true;
                }
                else if (dot < 0.6f)
                    l.score[i] = std::max(0, l.score[i] - 3);
            }
            int best = -1;
            for (unsigned i = 0; i < 6; ++i)
                if (l.varied[i] && l.score[i] >= 15 && (best < 0 || l.score[i] > l.score[best]))
                    best = static_cast<int>(i);
            if (best != l.axis)
            {
                l.axis = best;
                if (best >= 0)
                    std::cout << "Nitro Trap: learned which way Crash faces (axis " << best << ")\n" << std::flush;
            }
        }
        bool crash_facing(float &hx, float &hy)
        {
            Address type = 0;
            std::array<std::array<float, 2>, 6> axes{};
            if (!actor_axes(type, axes))
                return false;
            const auto found = facing_learned.find(type);
            if (found == facing_learned.end() || found->second.axis < 0)
                return false;
            const auto &axis = axes[found->second.axis];
            if (axis[0] == 0 && axis[1] == 0)
                return false;
            hx = axis[0], hy = axis[1];
            return true;
        }
        // V1.2.71: what the Nitro Trap needs from the level (the Nitro's template, spawners it may borrow, where the crates
        // are) is gathered on a thread of its own, at most every 4 seconds and only while a Nitro Trap waits or runs.
        // Going over every object in the level takes tens of thousands of checked reads; V1.2.66-70 did it on the game's
        // thread twice a second, all the time, and the game hitched every half second (V1.2.69's clips). The game thread
        // checks whatever it picks from this list before using it.
        struct FreeSpawner
        {
            Address component = 0, owner = 0;
            std::string name;
            std::array<float, 3> at{};
        };
        struct NitroLevelScan
        {
            std::string level;
            std::uint64_t when = 0;
            Address nitro_template = 0; // from one of the level's Nitro crate spawners, if it has any
            std::vector<FreeSpawner> spawners;
            std::vector<std::array<float, 3>> crates;
        };
        std::mutex nitro_scan_mutex;
        NitroLevelScan nitro_level_info;
        std::atomic_bool nitro_scan_running{false};
        bool nitro_scan_ready(const std::string &level)
        {
            std::lock_guard lock(nitro_scan_mutex);
            return !level.empty() && nitro_level_info.level == level;
        }
        bool ahead_spot(std::array<float, 3> &spot, bool *floating = nullptr)
        {
            if (floating)
                *floating = false;
            const auto n = crash_path.size();
            if (n < 6)
                return false;
            const auto &now = crash_path.back().at;
            // in the air: his height changed in the last 0.2 s, he is more than 30 above his lowest point of the last
            // half second, and that half second wasn't a smooth slope
            bool in_air = false;
            {
                const auto &back = crash_path[n - 6].at;
                float low = now[2], high = now[2], lowest = now[2];
                bool line = true;
                for (std::size_t j = n - 6; j < n; ++j)
                {
                    const float z = crash_path[j].at[2];
                    lowest = std::min(lowest, z);
                    if (j >= n - 3)
                        low = std::min(low, z), high = std::max(high, z);
                    line = line && std::abs(z - (back[2] + (now[2] - back[2]) * (j - (n - 6)) / 5.0f)) < 8;
                }
                in_air = high - low > 8 && now[2] - lowest > 30 && !line;
            }
            // the way he is going; in the air also the way he faces (learned while he runs, V1.2.74), or else the way he
            // last ran, up to 10 seconds back (V1.2.69's clip: a jump on the spot after standing through the countdown
            // had no way to go, so it fell instead; V1.2.72's log: right after a respawn there was no running at all)
            float hx = 0, hy = 0;
            std::size_t end = 0;
            if (!crash_heading(hx, hy, end) && !(in_air && (crash_facing(hx, hy) || crash_heading(hx, hy, end, 10000))))
                return false;
            // the crates, from the last look at the level (a few seconds old at most; see nitro_level_scan)
            std::vector<std::array<float, 3>> crates;
            {
                std::lock_guard lock(nitro_scan_mutex);
                if (nitro_level_info.level == crash_path_level)
                    crates = nitro_level_info.crates;
            }
            const auto clear = [&](const std::array<float, 3> &p)
            {
                return std::none_of(crates.begin(), crates.end(), [&](const std::array<float, 3> &c)
                                    { return std::hypot(c[0] - p[0], c[1] - p[1]) < 110 && std::abs(c[2] - p[2]) < 160; });
            };
            // open space (V1.2.69): straight ahead can be inside a wall where his way bends, and the client can't see
            // walls. Crates are always somewhere he can get to, so a spot straight ahead must be on the way to a crate
            // further ahead (within 70 of the line from him to it), or next to floor he stood on before
            const auto open = [&](const std::array<float, 3> &from, const std::array<float, 3> &p)
            {
                const float pd = std::hypot(p[0] - from[0], p[1] - from[1]);
                for (const auto &c : crates)
                {
                    const float dx = c[0] - from[0], dy = c[1] - from[1], cd = std::hypot(dx, dy);
                    if (cd < pd + 40 || cd > 900 || std::abs(c[2] - p[2]) > 300)
                        continue;
                    const float t = ((p[0] - from[0]) * dx + (p[1] - from[1]) * dy) / (cd * cd);
                    if (std::hypot(from[0] + dx * t - p[0], from[1] + dy * t - p[1]) < 70)
                        return true;
                }
                return std::any_of(level_floor.begin(), level_floor.end(), [&](const std::array<float, 3> &f)
                                   { return std::hypot(f[0] - p[0], f[1] - p[1]) < 80 && std::abs(f[2] - p[2]) < 200; });
            };
            // in the air it appears straight ahead at his height, and floats there
            {
                if (in_air)
                {
                    std::array<float, 3> at = now;
                    trap_actor_position(at); // where he is this moment (the path is up to 0.1 s old)
                    for (const float d : {nitro_ahead, nitro_ahead + 25, nitro_ahead + 50})
                    {
                        const std::array<float, 3> p{at[0] + hx * d, at[1] + hy * d, at[2]};
                        if (clear(p) && open(at, p))
                        {
                            if (floating)
                                *floating = true;
                            return spot = p, true;
                        }
                    }
                    return false;
                }
            }
            // floor he stood on before, within about 35 degrees of straight ahead
            float best = 1e9f;
            for (const auto &f : level_floor)
            {
                const float dx = f[0] - now[0], dy = f[1] - now[1], d = std::hypot(dx, dy);
                if (d < nitro_near || d > nitro_far || std::abs(f[2] - now[2]) > 120)
                    continue;
                const float along = (dx * hx + dy * hy) / d;
                const float score = std::abs(d - nitro_ahead) + (1 - along) * 800;
                if (along >= 0.82f && score < best && clear(f))
                    best = score, spot = f;
            }
            if (best < 1e9f)
                return true;
            // straight ahead: only while he is on the ground (the last 0.2 s at one height) and not turning
            float low = now[2], high = now[2];
            for (std::size_t j = n - 3; j < n; ++j)
                low = std::min(low, crash_path[j].at[2]), high = std::max(high, crash_path[j].at[2]);
            if (high - low > 8)
                return false;
            if (end >= 10)
            {
                const auto &a = crash_path[end - 10].at, &b = crash_path[end - 5].at;
                const float dx = b[0] - a[0], dy = b[1] - a[1], d = std::hypot(dx, dy);
                if (d >= 60 && (dx * hx + dy * hy) / d < 0.9f)
                    return false;
            }
            // the slope of the ground he ran over in the last half second, if it was smooth (no jump in it)
            float slope = 0;
            const auto &back = crash_path[n - 6].at;
            const float run = std::hypot(now[0] - back[0], now[1] - back[1]);
            if (run > 40)
            {
                bool smooth = true;
                for (std::size_t j = n - 6; j < n; ++j)
                    smooth = smooth && std::abs(crash_path[j].at[2] - (back[2] + (now[2] - back[2]) * (j - (n - 6)) / 5.0f)) < 8;
                if (smooth)
                    slope = std::clamp((now[2] - back[2]) / run, -0.35f, 0.35f);
            }
            for (const float d : {nitro_ahead, nitro_ahead + 25, nitro_ahead - 25, nitro_ahead + 50})
            {
                const std::array<float, 3> p{now[0] + hx * d, now[1] + hy * d, now[2] + slope * d};
                if (clear(p) && open(now, p))
                    return spot = p, true;
            }
            return false;
        }
        struct NitroSpawner
        {
            Address component = 0, owner = 0, method = 0;
            float distance = -1;
            std::string name;
        };
        // the ready Nitro spawner closest to where the new Nitro goes. The new crate goes with that spawner's part of the
        // level, which stays loaded while Crash is around; one borrowed from the far end of the level could vanish early.
        // With `away`, the closest one at least that far from the spot, if there is one: a crate made by a borrowed
        // spawner sooner or later goes "home" to it, and from a spawner right next to the spot it lands on that
        // spawner's own Nitro in plain sight (V1.1.66: every falling Nitro near the start of Snow Go did).
        NitroSpawner nitro_spawner_near(const std::array<float, 3> &to, float away = 0)
        {
            NitroSpawner best, distant;
            for (const auto owner : event_owners())
            {
                const auto name = string(value<Address>(owner + 128));
                if (name.rfind("Crate_Nitro", 0) != 0)
                    continue;
                std::array<float, 3> at{};
                if (!read(owner + 32, at))
                    continue;
                for (const auto c : list(value<Address>(owner + 24), 64))
                {
                    const auto m = spawner_method(c, owner, true);
                    const float d = std::hypot(at[0] - to[0], at[1] - to[1]);
                    if (m && (!best.component || d < best.distance))
                        best = {c, owner, m, d, name};
                    if (m && d >= away && (!distant.component || d < distant.distance))
                        distant = {c, owner, m, d, name};
                }
            }
            return away > 0 && distant.component ? distant : best;
        }
        struct NitroSpawn
        {
            Address component = 0, owner = 0, old_handle = 0;
            std::vector<std::pair<Address, std::array<float, 3>>> restore; // spawner position copies
            std::vector<std::pair<Address, Address>> words;                 // what it makes (any spawner), to put back
            std::uint64_t started = 0;
            std::string level, name;
            bool floats = false; // it appears in midair, ahead of Crash mid-jump, and stays up there
        } nitro_spawn_state;
        // the Nitro a trap spawned most recently (the falling Nitro waits for it)
        struct TrapNitro
        {
            Address entity = 0;
            std::array<float, 3> at{};
            std::uint64_t appeared = 0;
            std::string level;
        } trap_nitro;
        // trap Nitros are watched (only read) until they are gone, so the log says what became of them. Left alone, a
        // crate made by a borrowed spawner goes "home" to that spawner (onto the level's own Nitro there) at its first hop.
        struct TrapNitroWatch
        {
            Address entity = 0, components = 0;
            std::string level;
            std::array<float, 3> spot{}, home{};
            bool has_home = false, moved = false;
            bool floats = false, sank = false; // appeared in midair; the game started to let it drop
            std::uint64_t since = 0;
        };
        std::vector<TrapNitroWatch> trap_nitros;
        void watch_trap_nitro(Address entity, const std::array<float, 3> &at, const std::string &level, const std::array<float, 3> *home,
                              bool floats = false);
        void finish_nitro_spawn(const char *how)
        {
            auto &s = nitro_spawn_state;
            for (const auto &[at, original] : s.words)
                write(at, original);
            for (const auto &[at, original] : s.restore)
                write(at, original);
            write(s.component + 232, s.old_handle); // the spawner keeps its own crate
            std::cout << "Nitro spawn: " << how << "; " << s.name << "'s spawner is back to normal\n" << std::flush;
            s = {};
        }
        void nitro_spawn_followup();
        // V1.2.50 test (/nitro_drop any): the Nitro's template handle in this level, from one of its Nitro spawners or
        // else from the memory search (/nitro_probe), or 0
        Address level_nitro_template(const std::string &level)
        {
            {
                std::lock_guard lock(nitro_scan_mutex);
                if (nitro_level_info.level == level && nitro_level_info.nitro_template && nitro_template_valid(nitro_level_info.nitro_template))
                    return nitro_level_info.nitro_template;
            }
            std::lock_guard lock(nitro_find_mutex);
            if (nitro_find.level != level)
                return 0;
            if (nitro_template_valid(nitro_find.entity))
                return nitro_find.entity;
            // gone since (or never found): look again, at most every 10 seconds
            if (nitro_find.done && GetTickCount64() - nitro_find.when > 10000)
                nitro_find.done = false;
            return 0;
        }
        bool nitro_template_searched(const std::string &level)
        {
            std::lock_guard lock(nitro_find_mutex);
            return nitro_find.done && nitro_find.level == level;
        }
        // the scripts on an owner, by name ("common_Crate_LevelCount, ...")
        std::string owner_scripts(Address owner)
        {
            std::string scripts;
            for (const auto c : list(value<Address>(owner + 24), 64))
                if (typed(c, 0x13bbbd0) && typed(value<Address>(c + 56), 0x1745d78))
                {
                    auto name = string(value<Address>(value<Address>(c + 56) + 16), 120);
                    if (name.rfind("Scripts.Graph.", 0) == 0)
                        name.erase(0, 14);
                    scripts += (scripts.empty() ? "" : ", ") + name;
                }
            return scripts;
        }
        // a ready spawner to make the trap's Nitro with, closest to the spot (at least `away` from it when there is one).
        // V1.2.65: never one whose thing counts toward the level's boxes. A crate's box count (common_Crate_LevelCount, on
        // the spawner's owner) counts whatever its spawner makes: borrowing an Aku Aku crate's spawner counted the trap's
        // Nitro as that crate (V1.2.64 log: "Crate_AkuAku005 in L123 counted"), and in Crash 2 and 3 borrowing a Nitro
        // crate's spawner counted it as that Nitro. So: no owner with a box count, no enemies, checkpoints, time trial,
        // gem, relic or boss things, nothing on a path, and nothing that is itself a Nitro or TNT.
        // the scan itself (any thread: it only reads). A spawner it may borrow: none whose thing counts toward the boxes,
        // see free_spawner_near
        NitroLevelScan scan_level_for_nitro(const std::string &level)
        {
            NitroLevelScan scan;
            scan.level = level;
            for (const auto owner : event_owners())
            {
                const auto name = string(value<Address>(owner + 128));
                std::array<float, 3> at{};
                if (name.empty() || !read(owner + 32, at))
                    continue;
                if (name.rfind("Crate_", 0) == 0)
                    scan.crates.push_back(at);
                // the Nitro's template, from a Nitro crate's spawner
                if (!scan.nitro_template && kind_of(name) == "Crate_Nitro")
                    for (const auto c : list(value<Address>(owner + 24), 64))
                    {
                        if (!typed(c, 0x13bbbd0) || string(value<Address>(value<Address>(c + 56) + 16)) != "Scripts.Graph.common_Spawner_Template")
                            continue;
                        const auto data = value<Address>(c + 16);
                        if (field_offset(value<Address>(data + 32), "EntityToSpawn", 8) != 0x48)
                            continue;
                        if (const auto e = resolve(value<Address>(data + 0x48)); nitro_template_valid(e))
                        {
                            scan.nitro_template = e;
                            break;
                        }
                    }
                bool skip = false;
                for (const char *word : {"Nitro", "TNT", "Checkpoint", "Switch", "Outline", "LevelEnd", "TimeTrial", "Enemy", "Gem", "Relic",
                                         "Portal", "Boss", "Bonus", "Spline", "Path", "Orbit", "Swing", "Rotating", "Aku"})
                    skip = skip || name.find(word) != std::string::npos;
                if (skip)
                    continue;
                const auto scripts = owner_scripts(owner);
                if (scripts.find("LevelCount") != std::string::npos || scripts.find("Count") != std::string::npos ||
                    scripts.find("Checkpoint") != std::string::npos)
                    continue;
                for (const auto c : list(value<Address>(owner + 24), 64))
                    if (spawner_method(c, owner, true))
                        scan.spawners.push_back({c, owner, name, at});
            }
            scan.when = GetTickCount64();
            return scan;
        }
        // a fresh look at the level, on its own thread, if the last one is for another level or 4 seconds old
        void nitro_level_scan(const std::string &level)
        {
            if (level.empty())
                return;
            {
                std::lock_guard lock(nitro_scan_mutex);
                if (nitro_level_info.level == level && GetTickCount64() - nitro_level_info.when < 4000)
                    return;
            }
            if (nitro_scan_running.exchange(true))
                return;
            std::thread([level]
                        {
                auto scan = scan_level_for_nitro(level);
                const bool none = !scan.nitro_template;
                {
                    std::lock_guard lock(nitro_scan_mutex);
                    nitro_level_info = std::move(scan);
                }
                nitro_scan_running = false;
                // a level without Nitro crates: the Nitro's template is looked for in all of the game's memory (once)
                if (none && !nitro_template_searched(level))
                    nitro_template_search(level); })
                .detach();
        }
        // a ready spawner to make the trap's Nitro with, closest to the spot (at least `away` from it when there is one),
        // from the level scan, checked again here before it is used. V1.2.65: never one whose thing counts toward the
        // level's boxes. A crate's box count (common_Crate_LevelCount, on the spawner's owner) counts whatever its spawner
        // makes: borrowing an Aku Aku crate's spawner counted the trap's Nitro as that crate (V1.2.64 log: "Crate_AkuAku005
        // in L123 counted"), and in Crash 2 and 3 borrowing a Nitro crate's spawner counted it as that Nitro. So: no owner
        // with a box count, no enemies, checkpoints, time trial, gem, relic or boss things, nothing on a path, and nothing
        // that is itself a Nitro or TNT.
        NitroSpawner free_spawner_near(const std::array<float, 3> &to, float away = 0)
        {
            std::vector<FreeSpawner> spawners;
            {
                std::lock_guard lock(nitro_scan_mutex);
                if (nitro_level_info.level == crash_path_level)
                    spawners = nitro_level_info.spawners;
            }
            const auto distance = [&](const FreeSpawner &f) { return std::hypot(f.at[0] - to[0], f.at[1] - to[1]); };
            std::sort(spawners.begin(), spawners.end(), [&](const FreeSpawner &a, const FreeSpawner &b) { return distance(a) < distance(b); });
            NitroSpawner best, distant;
            unsigned checked = 0;
            for (const auto &f : spawners)
            {
                if (best.component && (away <= 0 || distant.component))
                    break;
                // past the closest one, only one at least `away` from the spot is still wanted
                if (best.component && distance(f) < away)
                    continue;
                if (++checked > 16)
                    break;
                // still the same thing, still a ready spawner, where it is now
                std::array<float, 3> at{};
                const auto m = string(value<Address>(f.owner + 128)) == f.name && read(f.owner + 32, at) ? spawner_method(f.component, f.owner, true) : 0;
                if (!m)
                    continue;
                const float d = std::hypot(at[0] - to[0], at[1] - to[1]);
                if (!best.component || d < best.distance)
                    best = {f.component, f.owner, m, d, f.name};
                if (d >= away && (!distant.component || d < distant.distance))
                    distant = {f.component, f.owner, m, d, f.name};
            }
            return away > 0 && distant.component ? distant : best;
        }
        // starts a spawn (ahead of Crash, or at `forced`); false with a reason when it cannot right now. With `any`, an
        // ordinary crate's spawner makes the Nitro from the Nitro's template (research: a Nitro in levels without one)
        bool nitro_spawn(std::string &why, const std::array<float, 3> *forced = nullptr, float away = 0, bool any = false)
        {
            if (nitro_spawn_state.component)
                return why = "one is still spawning", false;
            if (nitro_trap_exempt())
                return why = "gimmick level", false;
            std::array<float, 3> crash{}, spot{};
            if (!trap_actor_position(crash))
                return why = "Crash not found", false;
            bool floats = false;
            if (forced)
                spot = *forced;
            else if (!ahead_spot(spot, &floats))
                return why = "no spot ahead yet: Crash needs to be running or jumping", false;
            const auto level = level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184)));
            // in the scuba levels it floats in the water wherever it appears
            floats = floats || (!forced && nitro_ahead_only_level(level));
            // always made from the Nitro's template by a spawner that counts nothing (V1.2.65; before, a level's own Nitro
            // spawner was borrowed where there was one, and its box count counted the trap's Nitro)
            any = true;
            Address nitro_template = 0;
            if (any)
            {
                // (the level scan runs on its own thread; the falling Nitro waits for it, see update_nitro_drop)
                if (!nitro_scan_ready(level))
                {
                    nitro_level_scan(level);
                    return why = "still looking at the level", false;
                }
                nitro_template = level_nitro_template(level);
                if (!nitro_template && !nitro_template_searched(level))
                    nitro_template_search(level);
                if (!nitro_template)
                    return why = nitro_template_searched(level) ? "no Nitro template loaded in this level" : "still looking for the Nitro in memory", false;
            }
            const auto spawner = free_spawner_near(spot, away);
            if (!spawner.component)
                return why = "no spawner here that counts nothing to borrow", false;
            std::vector<std::pair<Address, Address>> words;
            static unsigned described = 0;
            if (described++ < 6)
                std::cout << "Nitro spawn: borrowing " << spawner.name << "'s spawner (its owner has: " << owner_scripts(spawner.owner) << ")\n" << std::flush;
            if (any)
            {
                // the borrowed spawner makes the Nitro: its own handle to what it makes (its data's EntityToSpawn, a real
                // handle with the game's own layout) points at the Nitro's template for the spawn call, and back right
                // after (V1.2.59; V1.2.58 used a handle found in memory, and one was on a thread's stack and went stale)
                const auto data = value<Address>(spawner.component + 16);
                if (field_offset(value<Address>(data + 32), "EntityToSpawn", 8) != 0x48)
                    return why = "the borrowed spawner's settings differ", false;
                // its own template can be any kind of game object (a crate's, a platform's: +0x14c8ef8 since V1.2.66
                // borrows platforms); the spawner copies whatever its handle points at
                const auto handle = value<Address>(data + 0x48), own = resolve(handle);
                if (!handle || !own || !(typed(own, 0x141d490) || typed(own, 0x14c8ef8) || typed(own, 0x14c7fb0) || typed(own, 0x1592220)))
                {
                    const auto type = value<Address>(own);
                    std::ostringstream text;
                    text << "the borrowed spawner's template is something else (type ";
                    if (type >= base() && type < module_end())
                        text << "+0x" << std::hex << (type - base());
                    else
                        text << "unknown";
                    text << ")";
                    return why = text.str(), false;
                }
                words.push_back({handle + 40, own});
                write(handle + 40, nitro_template);
            }
            auto &s = nitro_spawn_state;
            s = {spawner.component, spawner.owner, value<Address>(spawner.component + 232), {}, std::move(words), GetTickCount64(), level, spawner.name, floats};
            std::array<float, 3> home{};
            read(spawner.owner + 32, home);
            for (const auto at : {spawner.owner + 32, value<Address>(spawner.owner + 16) + 0xc0})
            {
                std::array<float, 3> now{};
                if (at > 0x10000 && read(at, now) && crate_copy(now.data(), home) && write(at, spot))
                    s.restore.push_back({at, now});
            }
            write(spawner.component + 232, Address{0}); // its own crate is set aside, not destroyed
            reinterpret_cast<void (*)(Address, Address, unsigned, Address)>(base() + 0xc988c0)(spawner.method, spawner.component, 0, 0);
            // the borrowed handle goes back at once: other spawners of that kind use it too
            for (const auto &w : s.words)
                write(w.first, w.second);
            s.words.clear();
            std::cout << "Nitro spawn: " << spawner.name << "'s spawner (" << static_cast<int>(spawner.distance)
                      << " from the spot" << (any ? ", making a Nitro from its template" : "") << ") spawns at " << triple(spot) << ", " << static_cast<int>(std::hypot(spot[0] - crash[0], spot[1] - crash[1]))
                      << " from Crash" << (floats ? ", in midair (Crash is jumping)" : "") << "\n" << std::flush;
            // the crate usually appears inside that call: put the spawner back at once, before its own crate can hop
            // home to the borrowed spot
            nitro_spawn_followup();
            return true;
        }
        void nitro_spawn_followup()
        {
            auto &s = nitro_spawn_state;
            if (!s.component)
                return;
            const auto elapsed = GetTickCount64() - s.started;
            if (level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184))) != s.level)
            {
                s = {}; // the level unloaded: nothing to restore
                return;
            }
            const auto crate = resolve(value<Address>(s.component + 232));
            if (crate)
            {
                std::array<float, 3> at{};
                read(crate + 32, at);
                std::string scripts;
                for (const auto m : list(value<Address>(crate + 24), 64))
                    if (typed(m, 0x13bbbd0) && typed(value<Address>(m + 56), 0x1745d78))
                        scripts += (scripts.empty() ? "" : ", ") + string(value<Address>(value<Address>(m + 56) + 16), 120);
                std::cout << "Nitro spawn: a new " << string(value<Address>(crate + 0x80)) << " (" << string(value<Address>(value<Address>(crate + 0x40) + 0x48))
                          << "; " << (scripts.empty() ? "no scripts" : scripts) << ") appeared at " << triple(at) << " after " << elapsed << " ms\n";
                // (V1.2.61-70 also wrote every setting of its scripts here, the first two times: about 250 lines in one
                // frame, which froze the game for most of a second when the Nitro appeared, V1.2.69's clip)
                trap_nitro = {crate, at, GetTickCount64(), s.level};
                watch_trap_nitro(crate, at, s.level, s.restore.empty() ? nullptr : &s.restore.front().second, s.floats);
                finish_nitro_spawn("done");
            }
            else if (elapsed > 2000)
                finish_nitro_spawn("nothing spawned within 2 seconds");
        }
        // Nitro Trap, falling: a countdown ("NITRO INCOMING 3, 2, 1" on screen, a tick each second like a TNT crate),
        // then a Nitro appears high above Crash and drops onto the spot where he stands in 0.30 seconds, speeding up like
        // a dropped crate, so moving away dodges it. The game creates the crate up there (a crate the game creates itself
        // works fully) and it is moved every frame with the game's own "set position", the one scripts use. That marks
        // it moved, so the game carries the move over to the crate's physics body and everything it is drawn from.
        // (Writing its position copies directly didn't: every frame the physics body put them back, V1.1.65's watch.)
        static constexpr std::uint64_t drop_countdown_ms = nitro_countdown_ms, drop_step_ms = nitro_countdown_step_ms, drop_fall_ms = 310;
        // ahead of Crash needs him moving on the ground; after the countdown he gets this long to start, then it falls
        static constexpr std::uint64_t ahead_wait_ms = 1500;
        // the falling Nitro borrows a spawner at least this far away when there is one (see nitro_spawner_near)
        static constexpr float drop_spawner_away = 1500;
        static constexpr float drop_height = 420;
        struct NitroDropTrap
        {
            enum Stage { Idle, Countdown, Ahead, Spawning, Settling, Falling } stage = Idle;
            bool ahead = false; // the coin flip: a Nitro appears ahead of Crash instead of falling onto him
            bool any = false;   // /nitro_drop any: an ordinary crate's spawner makes it (research)
            bool waited = false; // a scuba level's Nitro is waiting for a spot ahead (said once)
            bool boom = false;   // /nitro_drop boom (test): set off where it lands
            std::uint64_t start = 0, spawned = 0, fall_start = 0;
            int ticks = 0, settle_frames = 0;
            float drop = 0; // how far it falls
            std::string level;
            std::array<float, 3> spot{}, high{};
            Address entity = 0;
        } nitro_drop;
        // a short tick, made once: 22050 Hz mono 16-bit, a fading square-ish tone
        const std::vector<unsigned char> &tick_sound()
        {
            static const std::vector<unsigned char> wav = []
            {
                constexpr unsigned rate = 22050, samples = rate * 12 / 100;
                std::vector<unsigned char> out(44 + samples * 2);
                const auto put32 = [&](std::size_t at, std::uint32_t v) { std::memcpy(&out[at], &v, 4); };
                const auto put16 = [&](std::size_t at, std::uint16_t v) { std::memcpy(&out[at], &v, 2); };
                std::memcpy(&out[0], "RIFF", 4), put32(4, static_cast<std::uint32_t>(out.size() - 8)), std::memcpy(&out[8], "WAVE", 4);
                std::memcpy(&out[12], "fmt ", 4), put32(16, 16), put16(20, 1), put16(22, 1), put32(24, rate), put32(28, rate * 2);
                put16(32, 2), put16(34, 16), std::memcpy(&out[36], "data", 4), put32(40, samples * 2);
                for (unsigned i = 0; i < samples; ++i)
                {
                    const float t = static_cast<float>(i) / rate, fade = 1.0f - static_cast<float>(i) / samples;
                    const float wave = std::sin(6.2831853f * 1480 * t) + 0.35f * std::sin(6.2831853f * 2960 * t);
                    const auto v = static_cast<std::int16_t>(std::clamp(wave * fade * fade * 9000.0f, -32000.0f, 32000.0f));
                    put16(44 + i * 2, static_cast<std::uint16_t>(v));
                }
                return out;
            }();
            return wav;
        }
        void play_tick()
        {
            PlaySoundA(reinterpret_cast<LPCSTR>(tick_sound().data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
        }
        // the Nitro Trap countdown's sound for number `index` (0 for 3, 1 for 2, 2 for 1), in step with the numbers: the
        // game's own TNT countdown once a TNT has counted down in this level (a sound as long as the whole countdown
        // plays once, at 3; a short one at every number), otherwise the client's own tick
        void countdown_tick(int index)
        {
            static std::string announced;
            // the TNT countdown read from the game's own sound banks (found at startup, /sound_find)
            if (play_countdown_sound(index, nitro_countdown_step_ms))
            {
                if (const auto name = countdown_sound_name(); announced != name)
                {
                    announced = name;
                    std::cout << "Nitro Trap: the countdown plays the game's TNT sound " << name << " from its sound banks\n" << std::flush;
                }
                return;
            }
            const auto name = tnt_tick_sound();
            if (!name.empty())
            {
                const auto length = game_sound_length_ms(name);
                const bool whole = length >= 2 * nitro_countdown_step_ms;
                if (whole && index > 0)
                    return; // still playing from 3
                if (play_game_sound(name))
                {
                    if (announced != name)
                    {
                        announced = name;
                        std::cout << "Nitro Trap: the countdown uses the game's TNT sound \"" << name << "\" (" << length << " ms, "
                                  << (whole ? "played once at 3" : "at every number") << ")\n" << std::flush;
                    }
                    return;
                }
            }
            play_tick();
        }
        // a heap page we may read and write: committed, private, read/write, not a guard, uncached or write-combined
        // page (those can belong to the graphics driver)
        bool heap_page(Address at, std::size_t size)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (at < 0x10000 || !VirtualQuery(reinterpret_cast<void *>(at), &info, sizeof(info)) || info.State != MEM_COMMIT ||
                info.Type != MEM_PRIVATE || !(info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) ||
                (info.Protect & (PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE)))
                return false;
            return at + size <= reinterpret_cast<Address>(info.BaseAddress) + info.RegionSize;
        }
        // a crate's physics body: the crate's physics component (+0x1423ff0, the one that copies the body's place onto
        // the crate every frame, V1.1.65's watch) keeps it at +0x50. The game's own calls in that component move it:
        // "set body position" (+0x15e3b0, called at +0x6f2727) and "set body velocities" (+0x572770, at +0x6f278c).
        // V1.1.68: moving only the crate left its body (what is drawn and what kills) up where it appeared.
        Address crate_physics(Address entity)
        {
            if (typed(entity + 0x1a8, 0x1423ff0))
                return entity + 0x1a8;
            for (const auto c : list(value<Address>(entity + 24), 64))
                if (typed(c, 0x1423ff0))
                    return c;
            return 0;
        }
        inline constexpr float body_meters = 0.0254f; // game units (inches) to physics body units (meters)
        // where the crate's physics body is, in game units (read-only: the getter the game's own sync uses)
        bool crate_body_position(Address entity, std::array<float, 3> &out)
        {
            static const bool verified =
                code_matches(0x6f2796, std::array<unsigned char, 9>{0x48, 0x83, 0xc1, 0x50, 0xe8, 0x81, 0xcd, 0xa3, 0xff});
            const auto physics = live_crate(entity) ? crate_physics(entity) : 0;
            if (!verified || !physics)
                return false;
            const auto matrix = reinterpret_cast<const float *(*)(Address)>(base() + 0x12f520)(physics + 0x50);
            if (!matrix)
                return false;
            for (unsigned i = 0; i < 3; ++i)
                out[i] = matrix[12 + i] / body_meters;
            return true;
        }
        bool move_crate_body(Address entity, const std::array<float, 3> &at)
        {
            static const bool verified =
                code_matches(0x6f2720, std::array<unsigned char, 12>{0x48, 0x8b, 0xd0, 0x48, 0x8d, 0x4e, 0x50, 0xe8, 0x84, 0xbc, 0xa6, 0xff}) &&
                code_matches(0x6f2780, std::array<unsigned char, 17>{0x4c, 0x8d, 0x43, 0x6c, 0x48, 0x8d, 0x4e, 0x50, 0x48, 0x8d, 0x50, 0x60, 0xe8, 0xdf,
                                                                     0xff, 0xe7, 0xff});
            const auto physics = live_crate(entity) ? crate_physics(entity) : 0;
            if (!verified || !physics)
                return false;
            // the physics body works in meters, the game in inches: the game's own sync multiplies the body's position
            // by 39.37 when it copies it to the crate (V1.1.69 put the game's numbers in as they were, and the Nitro
            // flew to 39.37 times its spot, out of sight)
            alignas(16) float place[4]{at[0] * body_meters, at[1] * body_meters, at[2] * body_meters, 0};
            alignas(16) float still[4]{};
            reinterpret_cast<void (*)(Address, const float *)>(base() + 0x15e3b0)(physics + 0x50, place);
            reinterpret_cast<void (*)(Address, const float *, const float *)>(base() + 0x572770)(physics + 0x50, still, still);
            return true;
        }
        // the game's own "set position" for an entity (+0xc05a60, found by V1.1.65's watch): it stores the position,
        // marks the entity moved and tells its parts, so the physics body and the model follow
        void set_entity_position(Address entity, const std::array<float, 3> &at)
        {
            static const bool verified = code_matches(0xc05a60, std::array<unsigned char, 16>{0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x81,
                                                                                               0xec, 0xb0, 0x00, 0x00, 0x00, 0x48, 0x8b, 0xd9});
            if (verified && live_crate(entity))
                reinterpret_cast<void (*)(Address, const float *)>(base() + 0xc05a60)(entity, at.data());
            // and its physics body, which is what is drawn and what touches Crash
            static bool told = false;
            if (!move_crate_body(entity, at) && !told)
                told = true, std::cout << "Nitro drop: can't move the crate's physics body (" << (crate_physics(entity) ? "the game's code differs" : "no physics part")
                                       << ")\n" << std::flush;
        }
        void stop_nitro_drop(Shared &state, const char *why)
        {
            auto &d = nitro_drop;
            // cut short after it appeared: it lands at once
            if (d.stage == NitroDropTrap::Settling || d.stage == NitroDropTrap::Falling)
                set_entity_position(d.entity, d.spot);
            if (d.stage != NitroDropTrap::Idle && why)
                std::cout << "Nitro drop: " << why << "\n" << std::flush;
            d = {};
            state.nitro_countdown_end = 0;
        }
        // where the Nitro lands: where Crash is when the countdown ends, at the lowest height he was at in the last
        // second (the ground, even mid-jump), nudged a hair so no copy of Crash's own position ever matches the crate's
        std::array<float, 3> drop_spot()
        {
            std::array<float, 3> at{};
            trap_actor_position(at);
            const auto now = GetTickCount64();
            at[0] += 0.5f, at[1] += 0.5f;
            for (const auto &p : crash_path)
                if (now - p.time <= 1000 && std::hypot(p.at[0] - at[0], p.at[1] - at[1]) < 400)
                    at[2] = std::min(at[2], p.at[2]);
            return at;
        }
        std::string seconds(std::uint64_t ms)
        {
            return std::to_string(ms / 1000) + "." + std::to_string(ms / 100 % 10) + " s";
        }
        std::string whole(float v)
        {
            return std::isfinite(v) && std::abs(v) < 1e9f ? std::to_string(static_cast<long long>(v)) : std::string("?");
        }
        void watch_trap_nitro(Address entity, const std::array<float, 3> &at, const std::string &level, const std::array<float, 3> *home, bool floats)
        {
            if (trap_nitros.size() >= 6)
                trap_nitros.erase(trap_nitros.begin());
            TrapNitroWatch w;
            w.entity = entity, w.components = value<Address>(entity + 24), w.level = level, w.spot = at, w.since = GetTickCount64();
            const auto slot = trap_event_next.fetch_add(1) % trap_event_entity.size();
            {
                std::lock_guard lock(trap_event_mutex);
                trap_event_seen[slot].clear();
            }
            trap_event_since[slot] = w.since, trap_event_exploded[slot] = false, trap_event_entity[slot] = entity;
            if (home)
                w.home = *home, w.has_home = true;
            w.floats = floats;
            trap_nitros.push_back(std::move(w));
        }
        // what became of each trap Nitro goes to the log; one that appeared in midair is held up if the game lets it drop
        void update_trap_nitros(const std::string &level)
        {
            const auto now = GetTickCount64();
            for (std::size_t i = 0; i < trap_nitros.size();)
            {
                auto &w = trap_nitros[i];
                // the falling Nitro's spot is where it lands
                if (w.entity == nitro_drop.entity && nitro_drop.stage != NitroDropTrap::Idle)
                    w.spot = nitro_drop.spot, w.since = now;
                std::array<float, 3> e{};
                const bool alive = level == w.level && live_crate(w.entity) && value<Address>(w.entity + 24) == w.components && read(w.entity + 32, e);
                if (!alive)
                {
                    if (level == w.level)
                        std::cout << "Nitro Trap: its Nitro is gone after " << seconds(now - w.since) << " (set off, broken or unloaded)\n" << std::flush;
                    trap_nitros.erase(trap_nitros.begin() + static_cast<std::ptrdiff_t>(i));
                    continue;
                }
                // a floating Nitro stays up: if the game lets it drop (by its weight, or coming down from a hop), it is put
                // back where it appeared, still (hopping up from there is left alone)
                if (w.floats && e[2] < w.spot[2] - 3)
                {
                    if (!w.sank)
                        w.sank = true, std::cout << "Nitro Trap: its floating Nitro started to drop after " << seconds(now - w.since) << ": held up\n" << std::flush;
                    set_entity_position(w.entity, w.spot);
                    e = w.spot;
                }
                if (!w.moved && std::hypot(e[0] - w.spot[0], e[1] - w.spot[1]) > 20 &&
                    !(w.entity == nitro_drop.entity && nitro_drop.stage != NitroDropTrap::Idle))
                {
                    w.moved = true;
                    std::cout << "Nitro Trap: after " << seconds(now - w.since) << " the game moved its Nitro to " << whole(e[0]) << ", " << whole(e[1])
                              << ", " << whole(e[2])
                              << (w.has_home && std::hypot(e[0] - w.home[0], e[1] - w.home[1]) < 20 ? " (back to the spawner it was borrowed from)" : "")
                              << "\n" << std::flush;
                }
                ++i;
            }
        }
        // whether a watched trap Nitro went off (its explosion script spoke)
        bool trap_nitro_went_off(Address entity)
        {
            for (unsigned i = 0; i < trap_event_entity.size(); ++i)
                if (trap_event_entity[i].load() == entity)
                    return trap_event_exploded[i].load();
            return false;
        }
        std::vector<Address> aku_send_nodes(Address meta, Address message);
        // one of a crate's scripts by name ("Scripts.Graph.common_Crate"), or 0
        Address crate_script(Address crate, const std::string &script)
        {
            for (const auto c : list(value<Address>(crate + 24), 64))
            {
                const auto meta = value<Address>(c + 56);
                if (typed(c, 0x13bbbd0) && typed(meta, 0x1745d78) && value<Address>(c + 24) == crate && string(value<Address>(meta + 16), 120) == script)
                    return c;
            }
            return 0;
        }
        // a message one of the crate's scripts takes, by its id: the message and where it goes, or {0, 0}
        std::pair<Address, Address> crate_message(Address crate, const std::string &id)
        {
            for (const auto c : list(value<Address>(crate + 24), 64))
            {
                const auto meta = value<Address>(c + 56);
                if (!typed(c, 0x13bbbd0) || !typed(meta, 0x1745d78) || value<Address>(c + 24) != crate)
                    continue;
                for (const auto delegator : list(value<Address>(meta + 160), 128))
                {
                    const auto m = value<Address>(delegator + 24), t = value<Address>(delegator + 32);
                    if (typed(delegator, 0x13bc268) && typed(m, 0x17b6578) && typed(t, 0x13bbcb8) &&
                        string(value<Address>(m + 16), 120).find("_id_" + id) != std::string::npos)
                        return {m, t};
                }
            }
            return {0, 0};
        }
        // sets off a trap Nitro the way the game does when Crash runs into one. V1.2.68's logs, every falling Nitro that
        // hit Crash: its common_Crate_Nitro_Spawned script sends the crate tuqrv6vb, 06rjdce1, gy91r6w7 and then
        // bovldql9, which its common_Crate_Explode script takes, and the whole explosion follows from that in the same
        // frame. (V1.2.70 sent xyvfghry, the first message of that frame: the crate took it and nothing followed.)
        // V1.2.72 ran the game's own send nodes and the game crashed the moment the Nitro landed: a node run that way
        // most likely goes on into the nodes after it in its script, without the state the script has while it runs.
        // Now every message goes from a node of our own that ends there (as a DeathLink death is sent to Aku), with the
        // game's node's values when that node is found next to the script. Each step goes to the log before it is
        // sent, so if one still takes the game down, the log says which. Game thread only.
        std::map<std::pair<Address, Address>, Address> nitro_send_nodes; // (script description, message) -> its send node
        bool set_off_trap_nitro(Address crate, std::string &how)
        {
            if (!event_hook.installed && !code_matches(0x767da0, std::array<unsigned char, 16>{0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x57, 0x48,
                                                                                                0x83, 0xec, 0x40, 0x48, 0x83, 0x79, 0x10, 0}))
                return how = "the game's message sender differs from the verified build", false;
            if (!live_crate(crate))
                return how = "the Nitro is gone", false;
            const auto sender = crate_script(crate, "Scripts.Graph.common_Crate_Nitro_Spawned");
            if (!sender)
                return how = "its common_Crate_Nitro_Spawned script wasn't found", false;
            const auto meta = value<Address>(sender + 56);
            std::ostringstream out;
            bool exploded_sent = false;
            // the message, and how many values the game's send of it carries (tuqrv6vb and bovldql9: five)
            for (const auto &step : std::vector<std::pair<std::string, unsigned>>{{"tuqrv6vb", 5}, {"06rjdce1", 0}, {"gy91r6w7", 0}, {"bovldql9", 5}})
            {
                const std::string &id = step.first;
                const unsigned values = step.second;
                const auto taken = crate_message(crate, id);
                const Address message = taken.first, target = taken.second;
                if (!message)
                {
                    out << " " << id << ": not taken";
                    continue;
                }
                // the game's node of this message, only for its values
                Address args = 0;
                if (values)
                {
                    const auto fits = [&](Address node)
                    {
                        const auto list_at = value<Address>(node + 32);
                        return typed(node, 0x13bc390) && value<Address>(node + 16) == message && value<Address>(node + 24) && typed(list_at, 0x1745070) &&
                               list(list_at, 16).size() == values;
                    };
                    auto &node = nitro_send_nodes[{meta, message}];
                    if (!node || !fits(node))
                    {
                        node = 0;
                        for (const auto found : aku_send_nodes(meta, message))
                            if (fits(found))
                            {
                                node = found;
                                break;
                            }
                    }
                    if (node)
                        args = value<Address>(node + 32);
                    else if (id != "bovldql9")
                    {
                        out << " " << id << ": its values weren't found, skipped";
                        continue;
                    }
                }
                std::cout << "Nitro boom: sending " << id << (args ? " with the game's values" : " without values") << " ...\n" << std::flush;
                std::array<Address, 11> own{base() + 0x13bc390, 0, message, target, args};
                std::array<Address, 6> context{sender};
                reinterpret_cast<Address (*)(Address, Address)>(base() + 0x767da0)(reinterpret_cast<Address>(own.data()), reinterpret_cast<Address>(context.data()));
                std::cout << "Nitro boom: " << id << " sent\n" << std::flush;
                out << " " << id << (args ? ": sent with the game's values" : ": sent");
                exploded_sent = exploded_sent || id == "bovldql9";
            }
            how = out.str();
            return exploded_sent;
        }
        // the falling Nitro that landed without hitting Crash: set off at once, then a look whether it went off
        struct NitroSetOff
        {
            Address entity = 0;
            std::uint64_t at = 0;
            std::string level;
        } nitro_set_off;
        void update_nitro_set_off(const std::string &level)
        {
            auto &o = nitro_set_off;
            if (!o.entity || GetTickCount64() - o.at < 300)
                return;
            if (level == o.level)
                std::cout << (trap_nitro_went_off(o.entity) ? "Nitro drop: it went off where it landed\n"
                                                             : "Nitro drop: it didn't go off where it landed\n")
                          << std::flush;
            o = {};
        }
        void update_nitro_drop(Shared &state, const std::string &level)
        {
            auto &d = nitro_drop;
            const auto now = GetTickCount64();
            if (state.nitro_drop_request != state.nitro_drop_ack)
            {
                state.nitro_drop_ack = state.nitro_drop_request;
                if (d.stage != NitroDropTrap::Idle)
                    std::cout << "Nitro drop: one is already falling\n" << std::flush;
                else if (level.empty() || nitro_trap_exempt())
                    std::cout << "Nitro drop: not in a level with Nitros\n" << std::flush;
                else
                {
                    d = {};
                    d.stage = NitroDropTrap::Countdown, d.start = now, d.level = level;
                    // one Nitro Trap, two endings, half and half (/nitro_drop fall or ahead picks one)
                    static std::mt19937 coin{std::random_device{}()};
                    d.ahead = state.nitro_drop_kind == 2 || (state.nitro_drop_kind == 0 && std::bernoulli_distribution(0.5)(coin));
                    if (nitro_ahead_only_level(level) && !d.ahead)
                        d.ahead = true, std::cout << "Nitro Trap: in a scuba level it only appears ahead of Crash\n" << std::flush;
                    d.any = state.nitro_drop_kind == 3;
                    d.boom = state.nitro_drop_kind == 4;
                    // a fresh look at the level during the countdown (and, in a level without Nitro crates, a search
                    // for the Nitro's template in memory after it)
                    nitro_level_scan(level);
                    state.nitro_countdown_end = now + drop_countdown_ms;
                    std::cout << "Nitro Trap: countdown, then a Nitro " << (d.ahead ? "appears ahead of Crash" : "falls onto Crash")
                              << (d.any ? " (test: made by an ordinary crate's spawner)" : "") << "\n" << std::flush;
                }
            }
            static std::uint64_t last_tick = 0;
            const auto dt = last_tick && now > last_tick ? std::min<std::uint64_t>(now - last_tick, 600000) : 0;
            last_tick = now;
            if (d.stage == NitroDropTrap::Idle)
                return;
            // the pause menu (or any stretch the game didn't run) stops the countdown and the fall where they are
            if (state.menu_paused || dt > 250)
            {
                d.start += dt, d.spawned += dt, d.fall_start += dt;
                if (state.nitro_countdown_end)
                    state.nitro_countdown_end += dt;
                return;
            }
            if (level != d.level)
            {
                d.stage = NitroDropTrap::Idle; // its memory went with the level: nothing to move
                stop_nitro_drop(state, "the level changed");
                return;
            }
            const auto elapsed = now - d.start;
            if (d.stage == NitroDropTrap::Countdown)
            {
                // Crash died or lost control (a death, a cutscene): the drop is called off
                if (!state.player_ready)
                {
                    stop_nitro_drop(state, "called off: Crash can't move");
                    return;
                }
                // a tick as each number appears, like a TNT crate
                if (d.ticks < 3 && elapsed >= static_cast<std::uint64_t>(d.ticks) * drop_step_ms)
                    countdown_tick(d.ticks++);
                if (elapsed < drop_countdown_ms)
                    return;
                if (d.ahead)
                {
                    d.stage = NitroDropTrap::Ahead, d.spawned = now;
                    std::cout << "Nitro Trap: ahead of Crash\n" << std::flush;
                }
            }
            if (d.stage == NitroDropTrap::Ahead)
            {
                if (!state.player_ready)
                {
                    stop_nitro_drop(state, "called off: Crash can't move");
                    return;
                }
                // a Nitro ahead of him, where the ahead kind always puts it: on the ground he is running on
                std::string why;
                if (nitro_spawn(why))
                {
                    d = {}; // the ahead Nitro stays where it appeared, nothing more to do
                    state.nitro_countdown_end = std::min<std::uint64_t>(state.nitro_countdown_end, now); // the countdown text goes away
                    return;
                }
                if (now - d.spawned < ahead_wait_ms)
                    return;
                // in a scuba level it never falls: it waits for a spot ahead (swimming along) as long as it takes
                if (nitro_ahead_only_level(level))
                {
                    if (!d.waited)
                        d.waited = true, std::cout << "Nitro Trap: nothing ahead yet (" << why << "); in a scuba level it waits for a spot ahead\n" << std::flush;
                    return;
                }
                std::cout << "Nitro Trap: nothing ahead after " << ahead_wait_ms / 1000.0 << " s (" << why << "): it falls onto him instead\n" << std::flush;
                d.ahead = false, d.stage = NitroDropTrap::Countdown;
            }
            if (d.stage == NitroDropTrap::Countdown)
            {
                // the countdown is over: the Nitro appears high above the spot (the game creates it there, whole)
                d.spot = drop_spot();
                const std::array<float, 3> high{d.spot[0], d.spot[1], d.spot[2] + drop_height};
                std::string why;
                if (!nitro_spawn(why, &high, drop_spawner_away, d.any))
                {
                    // the Nitro's template is still being looked for: wait for it a little (the countdown text is gone)
                    if (why.rfind("still looking", 0) == 0 && elapsed < drop_countdown_ms + 8000)
                    {
                        state.nitro_countdown_end = std::min<std::uint64_t>(state.nitro_countdown_end, now);
                        return;
                    }
                    stop_nitro_drop(state, ("no Nitro: " + why).c_str());
                    return;
                }
                d.stage = NitroDropTrap::Spawning, d.spawned = now;
            }
            if (d.stage == NitroDropTrap::Spawning)
            {
                // the crate usually appears inside the spawn call; trap_nitro is the one that just appeared
                if (!trap_nitro.entity || trap_nitro.appeared < d.spawned)
                {
                    if (now - d.spawned > 1500)
                        stop_nitro_drop(state, "the Nitro did not appear");
                    return;
                }
                d.entity = trap_nitro.entity, d.high = trap_nitro.at;
                d.stage = NitroDropTrap::Settling, d.settle_frames = 0;
            }
            if (!live_crate(d.entity))
            {
                d.stage = NitroDropTrap::Idle; // nothing left to move
                stop_nitro_drop(state, "the Nitro went off before landing");
                return;
            }
            if (d.stage == NitroDropTrap::Settling)
            {
                // a few frames up there while the game sets the new crate up, held where it appeared
                set_entity_position(d.entity, d.high);
                if (++d.settle_frames < 3 || now - d.spawned < 60)
                    return;
                d.drop = std::max(0.0f, d.high[2] - d.spot[2]);
                std::cout << "Nitro drop: falling " << static_cast<int>(d.drop) << " onto " << triple(d.spot) << "\n" << std::flush;
                d.stage = NitroDropTrap::Falling, d.fall_start = now;
                return;
            }
            // falling: speeding up like a dropped crate, landing 0.30 seconds later
            const float q = std::min(1.0f, static_cast<float>(now - d.fall_start) / drop_fall_ms);
            set_entity_position(d.entity, std::array<float, 3>{d.high[0], d.high[1], d.high[2] - d.drop * q * q});
            if (q < 1)
                return;
            std::array<float, 3> e{};
            read(d.entity + 32, e);
            std::array<float, 3> body{};
            const bool has_body = crate_body_position(d.entity, body);
            std::cout << "Nitro drop: landed (the game has it at " << whole(e[0]) << ", " << whole(e[1]) << ", " << whole(e[2])
                      << (has_body ? ", its body at " + whole(body[0]) + ", " + whole(body[1]) + ", " + whole(body[2]) : std::string(", its body not found"))
                      << ")\n" << std::flush;
            // /nitro_drop boom (test): it blows up where it lands, also when Crash got out of the way (it already went off
            // if it hit him). Only on that test since V1.2.74: V1.2.72's set-off crashed the game the moment it landed
            if (d.boom && !trap_nitro_went_off(d.entity))
            {
                std::string how;
                if (set_off_trap_nitro(d.entity, how))
                {
                    std::cout << "Nitro drop: set it off where it landed (" << how << " )\n" << std::flush;
                    nitro_set_off = {d.entity, now, d.level};
                }
                else
                    std::cout << "Nitro drop: couldn't set it off (" << how << ")\n" << std::flush;
            }
            d = {};
            state.nitro_countdown_end = 0;
        }
        // research: every setting of a script, one line per six (bytes as numbers, 12/16 byte ones as floats)
        void log_graph_fields(Address c, const std::string &who)
        {
            const auto meta = value<Address>(c + 56);
            const unsigned size = value<std::uint16_t>(meta + 90), count = value<unsigned>(meta + 24);
            const auto fields = value<Address>(meta + 40);
            std::ostringstream out;
            out << who << ": " << size << " bytes, " << count << " settings:";
            unsigned shown = 0;
            for (unsigned i = 0; fields && count <= 512 && i < count; ++i)
            {
                const auto field = value<Address>(fields + i * 8);
                const unsigned offset = value<std::uint16_t>(field + 24), bytes = value<std::uint16_t>(field + 22);
                if (!bytes || offset < 0x40 || offset + bytes > size)
                    continue;
                out << (shown++ % 6 ? " " : "\n" + who + ":  ") << string(value<Address>(field + 48), 48) << "@0x" << std::hex << offset << std::dec << "=";
                if (bytes == 1)
                    out << unsigned(value<unsigned char>(c + offset));
                else if (bytes == 4)
                    out << value<int>(c + offset) << "/" << value<float>(c + offset);
                else if (bytes == 8)
                    out << "0x" << std::hex << value<std::uint64_t>(c + offset) << std::dec;
                else
                {
                    std::array<float, 4> f{};
                    read(c + offset, f);
                    out << "(" << f[0] << "," << f[1] << "," << f[2] << ")";
                }
            }
            std::cout << out.str() << "\n" << std::flush;
        }
        // Aku Aku's manager: its state (0 when no mask is out) and whether a mask is out
        std::pair<int, bool> aku_manager_state(Address actor)
        {
            for (const auto c : actor ? list(value<Address>(actor + 24), 128) : std::vector<Address>{})
                if (typed(c, 0x13bbbd0) && typed(value<Address>(c + 56), 0x1745d78) &&
                    string(value<Address>(value<Address>(c + 56) + 16)) == "Scripts.Graph.Crash_AkuAkuManager")
                    return {value<int>(c + 0x90), resolve(value<Address>(c + 0x60)) != 0};
            return {-1, false};
        }
        // Crash's running Aku Aku manager script, or 0
        Address aku_manager_component(Address actor)
        {
            for (const auto component : actor ? list(value<Address>(actor + 24), 128) : std::vector<Address>{})
            {
                const auto meta = value<Address>(component + 56);
                if (typed(component, 0x13bbbd0) && value<Address>(component + 24) == actor && (value<unsigned>(component + 32) & 1) &&
                    typed(meta, 0x1745d78) && string(value<Address>(meta + 16)) == "Scripts.Graph.Crash_AkuAkuManager")
                    return component;
            }
            return 0;
        }
        // a message Aku's manager takes, by its id (New_Graph_Event_1_id_<id> or GraphEvent_id_<id>_Definition): the
        // message and where it goes, or {0, 0}
        std::pair<Address, Address> aku_message(Address component, const std::string &id)
        {
            for (const auto delegator : list(value<Address>(value<Address>(component + 56) + 160), 128))
            {
                const auto message = value<Address>(delegator + 24), target = value<Address>(delegator + 32);
                if (!typed(delegator, 0x13bc268) || !typed(message, 0x17b6578) || !typed(target, 0x13bbcb8))
                    continue;
                const auto name = string(value<Address>(message + 16), 120);
                if (name == "Crash_AkuAkuManager_c.New_Graph_Event_1_id_" + id || name == "Crash_AkuAkuManager_c.GraphEvent_id_" + id + "_Definition")
                    return {message, target};
            }
            return {0, 0};
        }
        // research: every message Aku's manager takes, once
        void log_aku_messages(Address component)
        {
            static bool logged = false;
            if (logged)
                return;
            logged = true;
            std::ostringstream out;
            out << "Aku give: Aku's manager takes";
            for (const auto delegator : list(value<Address>(value<Address>(component + 56) + 160), 128))
            {
                const auto name = string(value<Address>(value<Address>(delegator + 24) + 16), 120);
                const auto id = name.find("_id_");
                out << " " << (id == std::string::npos ? name : name.substr(id + 4));
            }
            std::cout << out.str() << "\n" << std::flush;
        }
        // runs a send node in Aku's manager the way his own script does (the Aku Aku lock lets it through)
        void aku_send(Address component, Address node)
        {
            std::array<Address, 6> context{component};
            own_aku_message = true;
            reinterpret_cast<Address (*)(Address, Address)>(base() + 0x767da0)(node, reinterpret_cast<Address>(context.data()));
            own_aku_message = false;
        }
        // sends Aku's manager a message with no values, the way a DeathLink death is sent. False if he doesn't take it
        bool aku_send_message(Address component, const std::string &id)
        {
            const auto [message, target] = aku_message(component, id);
            if (!message || !field_at(message, "_sender", 32, 8))
            {
                std::cout << "Aku give: Aku's manager doesn't take " << id << " here\n" << std::flush;
                return false;
            }
            std::array<Address, 11> sender{base() + 0x13bc390, 0, message, target};
            aku_send(component, reinterpret_cast<Address>(sender.data()));
            return true;
        }
        // the game's senders of one message, found around Aku's script description and the message's own (a script's
        // nodes sit together in memory), lowest address first
        std::vector<Address> aku_send_nodes(Address meta, Address message)
        {
            std::set<Address> pages;
            for (const auto around : {meta, message})
            {
                const auto page = around & ~Address{0xfff};
                for (Address p = page > 0x100000 ? page - 0x80000 : 0x10000; p <= page + 0x80000; p += 0x1000)
                    pages.insert(p);
            }
            std::vector<Address> found, words(512);
            for (const auto page : pages)
            {
                if (!readable(page, 0x1000) || !read(page, words))
                    continue;
                for (unsigned i = 0; i < words.size(); ++i)
                {
                    if (words[i] != message)
                        continue;
                    const auto node = page + i * 8 - 16, type = value<Address>(node), args = value<Address>(node + 32);
                    if (type > base() && type < base() + 0x2000000 && value<Address>(node + 24) && (!args || typed(args, 0x1745070)))
                        found.push_back(node);
                }
            }
            return found;
        }
        void dump_crash_scripts(Address actor, const std::string &why);
        // /aku_give tries one way at a time and looks a second later whether a mask came out (game thread only)
        struct AkuGiveStep
        {
            std::string label;
            // a send node of the game's, run as is (still of this type and for this message), or messages with no values
            Address node = 0, type = 0, message = 0;
            std::vector<std::string> ids;
            // and then Crash takes the mask: the game's mask count and Aku's manager's own state set to 1 (when Crash
            // had no mask before)
            bool count = false;
            // first: 2 = his settings as they are with the lock off and no mask (what the lock leaves otherwise keeps the
            // mask from coming out)
            int prepare = 0;
        };
        // V1.2.39's log, lock on, before /aku_give: these of Aku's manager's settings differ from the lock off with no
        // mask (V1.2.34's log). Set back to the lock-off values
        void aku_lock_off_settings(Address component)
        {
            struct Setting
            {
                const char *name;
                unsigned offset, size;
                int value;
            };
            static const Setting settings[]{{"tempIsIdle", 0x45, 1, 0}, {"Bool_id_yvav8yod", 0x120, 1, 1}, {"Bool_id_4m5sd4dd", 0x138, 1, 0}, {"Int_id_fbrl3zgn", 0x1b8, 4, 3}};
            const auto meta = value<Address>(component + 56);
            std::ostringstream out;
            out << "Aku give: Aku's manager's lock-off settings:";
            for (const auto &setting : settings)
            {
                if (!field_at(meta, setting.name, setting.offset, setting.size))
                {
                    out << " " << setting.name << " (not there)";
                    continue;
                }
                const int before = setting.size == 1 ? value<unsigned char>(component + setting.offset) : value<int>(component + setting.offset);
                if (setting.size == 1)
                    write(component + setting.offset, static_cast<unsigned char>(setting.value));
                else
                    write(component + setting.offset, setting.value);
                out << " " << setting.name << " " << before << "->" << setting.value;
            }
            std::cout << out.str() << "\n" << std::flush;
        }
        // AkuAkuCount (Crash_DamageManager's mask count, what decides whether a hit costs a mask or a life) set with the
        // game's own number setter (+0x502200, where the number variables' "reset" entry +0x4fb420 jumps). False if it
        // can't be done safely
        bool aku_give_count(int count)
        {
            const bool verified = code_matches(0x4fb420, std::array<unsigned char, 8>{0x8b, 0x51, 0x18, 0xe9, 0xd8, 0x6d, 0x00, 0x00});
            const auto variable = aku_variable.load();
            if (!verified || !variable || !typed(variable, 0x155e9f8))
            {
                std::cout << "Aku give: AkuAkuCount can't be set here (" << (!verified ? "setter differs" : !variable ? "not found" : "wrong type") << ")\n"
                          << std::flush;
                return false;
            }
            reinterpret_cast<void (*)(Address, int)>(base() + 0x502200)(variable, count);
            std::cout << "Aku give: set AkuAkuCount to " << count << " with the game's own setter\n" << std::flush;
            return true;
        }
        // Crash has this many masks: the game's count (what a hit checks) and Aku's manager's own state. V1.2.37's log:
        // with a crate's mask out his currentState is 1 and DoTakeHit off; with no mask, 0 and on. The filler mask left
        // them at 0 and on, so the hit that took it didn't take its model away
        bool aku_set_masks(Address component, int masks)
        {
            const auto meta = value<Address>(component + 56);
            if (!field_at(meta, "currentState", 0x90, 4) || !field_at(meta, "DoTakeHit", 0x42, 1))
            {
                std::cout << "Aku give: Aku's manager isn't laid out as expected; his state wasn't set\n" << std::flush;
                return aku_give_count(masks);
            }
            const bool counted = aku_give_count(masks);
            write(component + 0x90, masks);
            write(component + 0x42, static_cast<unsigned char>(masks ? 0 : 1));
            std::cout << "Aku give: Aku's manager now has " << masks << (masks == 1 ? " mask" : " masks") << " (currentState " << masks << ", DoTakeHit "
                      << (masks ? "off" : "on") << ")\n" << std::flush;
            return counted;
        }
        std::vector<AkuGiveStep> aku_steps;
        unsigned aku_step = 0;
        void aku_run_step(Address actor)
        {
            const auto &step = aku_steps[aku_step];
            const auto component = aku_manager_component(actor);
            filler_mask_since = GetTickCount64();
            bool sent = false;
            if (component && step.prepare == 2)
                aku_lock_off_settings(component);
            if (component && step.node && value<Address>(step.node) == step.type && value<Address>(step.node + 16) == step.message)
                aku_send(component, step.node), sent = true;
            for (const auto &id : step.ids)
                sent = (component && aku_send_message(component, id)) || sent;
            const auto [state, mask] = aku_manager_state(actor);
            // Crash takes the mask only once its model is out (V1.2.38 with the lock on: no model came out, but his
            // state said 1 mask, and the next /aku_give made it 2)
            if (step.count && component && mask)
                aku_set_masks(component, 1);
            std::cout << "Aku give: tried " << step.label << (sent ? "" : " (nothing could be sent)") << "; right after, Aku's manager is in state " << state
                      << (mask ? ", WITH A MASK OUT" : ", no mask out") << "\n" << std::flush;
            aku_give_check_at = GetTickCount64() + 1000;
        }
        void aku_give_followup()
        {
            const auto at = aku_give_check_at.load();
            if (!at || GetTickCount64() < at)
                return;
            aku_give_check_at = 0;
            if (aku_step >= aku_steps.size())
                return;
            const auto actor = player();
            const auto [state, mask] = aku_manager_state(actor);
            const auto label = aku_steps[aku_step].label;
            std::cout << "Aku give: a second after " << label << ", Aku's manager is in state " << state << (mask ? ", WITH A MASK OUT" : ", no mask out")
                      << "\n" << std::flush;
            if (mask)
            {
                std::cout << "Aku give: this worked: " << label << "\n" << std::flush;
                // the model came out after the step, not right away: Crash takes the mask now
                if (aku_steps[aku_step].count && state == 0)
                    if (const auto component = aku_manager_component(actor))
                        aku_set_masks(component, 1);
                aku_steps.clear();
                return;
            }
            if (!actor || ++aku_step >= aku_steps.size())
            {
                std::cout << "Aku give: " << (actor ? "no mask came out" : "Crash is gone; stopped") << "\n" << std::flush;
                if (actor)
                    dump_crash_scripts(actor, "no mask came out");
                aku_steps.clear();
                return;
            }
            aku_run_step(actor);
        }
        void aku_give()
        {
            std::array<float, 3> crash{};
            const auto actor = player();
            const auto component = aku_manager_component(actor);
            if (!actor || !crash_position(crash) || !component)
            {
                std::cout << "Aku give: Crash or his Aku Aku manager wasn't found. Try it inside a level.\n" << std::flush;
                return;
            }
            if (!event_hook.installed && !code_matches(0x767da0, std::array<unsigned char, 16>{0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x57, 0x48,
                                                                                                0x83, 0xec, 0x40, 0x48, 0x83, 0x79, 0x10, 0}))
            {
                std::cout << "Aku give: the game's message sender differs from the verified build\n" << std::flush;
                return;
            }
            if (!aku_steps.empty())
            {
                std::cout << "Aku give: still trying the last one\n" << std::flush;
                return;
            }
            {
                const auto [state, mask] = aku_manager_state(actor);
                std::cout << "Aku give: before, Aku's manager is in state " << state << (mask ? ", with a mask out" : ", no mask out") << "\n" << std::flush;
            }
            filler_mask_since = GetTickCount64();
            // Aku Aku Sanity's switch keeps masks from being given: off now, before the hand-over (the lock stays aside
            // while the mask is out). The lock's filter steps aside first: V1.2.38's log, with the lock on, showed Aku's
            // manager answers the switch with a message (s8h11iit) that the filter kept from him, and then a9nnfx8i
            // didn't bring the mask out
            aku_messages_blocked = false;
            bool unlocked = false;
            if (const auto variable = no_aku_variable.load(); variable && typed(variable, 0x155ebf0))
            {
                const auto getter = reinterpret_cast<unsigned char (*)(Address)>(base() + 0x4e5c60);
                const auto setter = reinterpret_cast<void (*)(Address, unsigned char)>(death_hook.original ? death_hook.original : base() + 0x502000);
                if (getter(variable))
                {
                    own_aku_message = true;
                    setter(variable, 0);
                    own_aku_message = false;
                    unlocked = true;
                    std::cout << "Aku give: turned Aku Aku Sanity's no-masks switch off for this mask\n" << std::flush;
                }
            }
            // ... and the setup messages the lock kept from him at this level's start (sent by his own script then)
            if (unlocked)
            {
                std::vector<std::pair<std::string, Address>> missed;
                {
                    std::lock_guard lock(event_mutex);
                    if (aku_blocked_actor == actor)
                        for (const auto id : {"ju9pgu97", "tm011evr"})
                            if (const auto it = aku_blocked_nodes.find(id);
                                it != aku_blocked_nodes.end() && typed(it->second.first, 0x13bc390) && value<Address>(it->second.first + 16) == it->second.second)
                                missed.emplace_back(id, it->second.first);
                }
                for (const auto &[id, node] : missed)
                    aku_send(component, node);
                std::cout << "Aku give: sent Aku the setup the lock kept from him:" << (missed.empty() ? " none caught" : "");
                for (const auto &m : missed)
                    std::cout << " " << m.first;
                std::cout << "\n" << std::flush;
            }
            log_aku_messages(component);
            // (his state is the mask count, 0 to 3; once, during a load, it read 32835)
            const auto [state_before, model_out] = aku_manager_state(actor);
            const int masks_before = model_out ? std::clamp(state_before, 1, 3) : 0;
            const bool had_mask = model_out;
            // a mask already out: one more, as a second crate's would (two masks take two hits). Its model is already
            // there, so nothing new is brought out
            if (had_mask)
            {
                const int masks = std::min(masks_before, 1) + 1;
                std::cout << "Aku give: Crash already has a mask; now " << masks << "\n" << std::flush;
                aku_set_masks(component, masks);
                return;
            }
            // no mask on screen: Aku's manager and the game's count start from no mask. V1.2.39's log: with the lock on he
            // was set as if he had one (DoTakeHit off), maybe left from V1.2.38's "now 2", saved with the game
            aku_set_masks(component, 0);
            // A real pickup (V1.2.35's log) is a chain of messages Aku's manager sends himself: fcbn65ha with "yes"
            // (from one node of his script), a9nnfx8i (his handling of it sends 6ldq5n9r, which brings the mask out),
            // then 9becl2sc, tjpwzf3m, sppqmskb, 202aweq4, 9nj5heau and nr69k1wc, and the game's mask count changes.
            // V1.2.35 sent only a9nnfx8i and 6ldq5n9r: the mask showed but a hit still killed Crash. Now the whole
            // chain, in order, then the mask count.
            // V1.2.40's log, lock on: the mask came out once tempIsIdle and Bool_id_yvav8yod were set back to their
            // lock-off values first (s8h11iit didn't help), so that comes first now
            AkuGiveStep chain;
            chain.label = "the hand-over (his lock-off settings, then the pickup's chain)";
            chain.prepare = 2;
            // (sppqmskb isn't sent: tjpwzf3m's own handling sends it, as in a real pickup)
            chain.ids = {"a9nnfx8i", "9becl2sc", "tjpwzf3m", "202aweq4", "9nj5heau", "nr69k1wc"};
            chain.count = !had_mask;
            const auto meta = value<Address>(component + 56);
            const auto message = aku_message(component, "fcbn65ha").first;
            // fcbn65ha's "yes" node: the one a pickup runs (two values: a +0x1754fe0 source, then a constant yes)
            const auto says_yes = [&](Address node)
            {
                const auto sources = list(value<Address>(node + 32), 16);
                return typed(node, 0x13bc390) && value<Address>(node + 16) == message && typed(value<Address>(node + 32), 0x1745070) && sources.size() == 2 &&
                       typed(sources[0], 0x1754fe0) && typed(sources[1], 0x1748280) && (value<std::uint64_t>(sources[1] + 24) & 0xff) == 1;
            };
            if (message)
            {
                // (found once per script: the search reads the memory around it)
                static Address found_for = 0, found = 0;
                if (const auto pickup = aku_pickup_node.load(); pickup && says_yes(pickup))
                    chain.node = pickup;
                else if (found_for == meta && found && says_yes(found))
                    chain.node = found;
                else
                    for (const auto node : aku_send_nodes(meta, message))
                        if (says_yes(node))
                        {
                            chain.node = found = node, found_for = meta;
                            break;
                        }
            }
            if (chain.node)
            {
                chain.type = value<Address>(chain.node), chain.message = message;
                std::cout << "Aku give: fcbn65ha's yes node is at script " << (chain.node >= meta ? "+0x" : "-0x") << std::hex
                          << (chain.node >= meta ? chain.node - meta : meta - chain.node) << std::dec << send_arguments(chain.node) << "\n" << std::flush;
            }
            else
                std::cout << "Aku give: fcbn65ha's yes node wasn't found; the rest of the chain goes anyway\n" << std::flush;
            aku_steps.push_back(chain);
            // once more if the model didn't come out the first time
            auto again = chain;
            again.label = "the hand-over again";
            aku_steps.push_back(again);
            aku_step = 0;
            aku_run_step(actor);
        }
        // research: Aku's manager's settings and Crash_DamageManager's (V1.2.37: a crate's mask and a filler mask side by
        // side showed what the filler's was missing). Now only when a give brings no mask out
        void dump_crash_scripts(Address actor, const std::string &why)
        {
            static unsigned dumps = 0, damage_dumps = 0;
            if (dumps >= 24)
                return;
            ++dumps;
            std::cout << "Aku research: " << why << "\n" << std::flush;
            for (const auto c : list(value<Address>(actor + 24), 128))
            {
                if (!typed(c, 0x13bbbd0) || !typed(value<Address>(c + 56), 0x1745d78))
                    continue;
                const auto name = string(value<Address>(value<Address>(c + 56) + 16));
                if (name == "Scripts.Graph.Crash_AkuAkuManager")
                    log_graph_fields(c, "Aku manager");
                else if (name == "Scripts.Graph.Crash_DamageManager" && damage_dumps < 7)
                    ++damage_dumps, log_graph_fields(c, "Damage manager");
            }
        }
        // requests from /nitro_spawn and the Nitro Trap; a request waits up to 5 seconds for a spot
        void update_nitro_trap(Shared &state, const std::string &level)
        {
            // the game's sounds from the last level may be gone with it
            static std::string sound_level;
            if (level != sound_level)
                sound_level = level, forget_game_sounds();
            record_crash_path(level);
            learn_facing();
            // the level scan, every few seconds on its own thread, only while a Nitro Trap waits or runs
            if (!level.empty() && (state.nitro_wanted || nitro_drop.stage != NitroDropTrap::Idle || state.nitro_spawn_request != state.nitro_spawn_ack) &&
                !nitro_trap_exempt())
                nitro_level_scan(level);
            nitro_spawn_followup();
            update_nitro_drop(state, level);
            update_nitro_set_off(level);
            update_trap_nitros(level);
            const auto now = GetTickCount64();
            static std::uint64_t next_ready = 0, give_up = 0;
            if (now >= next_ready)
            {
                next_ready = now + 500;
                std::array<float, 3> crash{}, spot{};
                const bool can = !level.empty() && !nitro_trap_exempt() && state.player_ready && !nitro_spawn_state.component &&
                                 nitro_drop.stage == NitroDropTrap::Idle && trap_actor_position(crash);
                // the Nitro's template (in a level without Nitro crates, looked for in memory once while a Nitro Trap
                // waits) and a spawner that counts nothing to make it with, from the level scan (cheap here: no
                // going over the level's objects on the game's thread)
                const bool known = can && level_nitro_template(level);
                if (can && !known && state.nitro_wanted && nitro_scan_ready(level) && !nitro_template_searched(level))
                    nitro_template_search(level);
                bool borrowed = false;
                if (known)
                {
                    std::lock_guard lock(nitro_scan_mutex);
                    borrowed = nitro_level_info.level == level && !nitro_level_info.spawners.empty();
                }
                state.nitro_ready = borrowed && ahead_spot(spot);
                state.nitro_drop_ready = borrowed;
            }
            if (state.nitro_spawn_request == state.nitro_spawn_ack)
            {
                give_up = 0;
                return;
            }
            if (!give_up)
                give_up = now + 5000;
            std::string why;
            if (nitro_spawn(why))
                state.nitro_spawn_ack = state.nitro_spawn_request, give_up = 0;
            else if (now > give_up)
            {
                state.nitro_spawn_ack = state.nitro_spawn_request, give_up = 0;
                std::cout << "Nitro spawn: gave up (" << why << ")\n" << std::flush;
            }
        }
        // /ice_probe research (read-only): every script setting on Crash. Two runs, one standing on normal ground and
        // one standing on ice, show which settings the game changes on ice; movement-sounding names are always
        // listed. Raw words of Crash's movement controller (+424) that changed are listed too.
        struct IceValue
        {
            std::uint64_t bits = 0;
            unsigned size = 0;
        };
        std::map<std::string, IceValue> ice_probe_last;
        std::set<Address> ice_materials, ice_material_types;
        std::vector<unsigned> ice_raw_last;
        std::string ice_format(const IceValue &v)
        {
            std::ostringstream out;
            if (v.size == 1)
                out << unsigned(v.bits & 0xff);
            else if (v.size == 4)
            {
                float f = 0;
                const auto word = static_cast<std::uint32_t>(v.bits);
                std::memcpy(&f, &word, 4);
                out << static_cast<std::int32_t>(word);
                if (std::isfinite(f) && std::abs(f) > 1e-6f && std::abs(f) < 1e7f)
                    out << " (" << f << ")";
            }
            else
                out << "0x" << std::hex << v.bits;
            return out.str();
        }
        std::string describe_object(Address p)
        {
            std::ostringstream out;
            if (!readable(p, 72))
                return "(not readable)";
            const auto vt = value<Address>(p);
            if (!vt)
                return "(unreadable)";
            if (vt >= base() && vt < module_end())
                out << "type " << hex(static_cast<unsigned>(vt - base()));
            else
                out << "first word 0x" << std::hex << vt << std::dec;
            for (unsigned off = 8; off <= 64; off += 8)
            {
                const auto at = value<Address>(p + off);
                if (!readable(at, 64))
                    continue;
                const auto text = string(at, 64);
                if (text.size() >= 3 && std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 32 && c < 127; }))
                    out << ", " << hex(off) << " \"" << text << "\"";
            }
            return out.str();
        }
        void ice_probe()
        {
            const auto actor = player();
            if (!actor)
            {
                std::cout << "Ice probe: Crash not found. Run it inside a level.\n" << std::flush;
                return;
            }
            static const char *keys[]{"ice", "slip", "fric", "surface", "slide", "skid", "moment", "accel", "decel",
                                      "ground", "material", "speed", "veloc", "traction", "grip", "snow", "terrain"};
            std::map<std::string, IceValue> now;
            ice_materials.clear();
            std::string graphs, named;
            unsigned named_count = 0;
            for (const auto c : list(value<Address>(actor + 24), 128))
            {
                if (!typed(c, 0x13bbbd0) || value<Address>(c + 24) != actor)
                    continue;
                const auto meta = value<Address>(c + 56);
                if (!typed(meta, 0x1745d78))
                    continue;
                auto graph = string(value<Address>(meta + 16));
                if (graph.rfind("Scripts.Graph.", 0) == 0)
                    graph.erase(0, 14);
                graphs += (graphs.empty() ? "" : ", ") + graph;
                const auto count = value<unsigned>(meta + 24), size = unsigned(value<std::uint16_t>(meta + 90));
                const auto fields = value<Address>(meta + 40);
                if (!fields || count > 512)
                    continue;
                if (graph == "Crash_IceSlide")
                {
                    // the game's own ice logic: every setting, and what its material references are
                    std::cout << "Ice probe: Crash_IceSlide has " << count << " settings\n";
                    for (unsigned i = 0; i < count; ++i)
                    {
                        const auto field = value<Address>(fields + i * 8);
                        const auto name = string(value<Address>(field + 48));
                        const unsigned offset = value<std::uint16_t>(field + 24), bytes = value<std::uint16_t>(field + 22);
                        IceValue v{0, std::min(bytes, 8u)};
                        if (bytes && offset + v.size <= size)
                            read(c + offset, v.bits);
                        std::cout << "Ice probe:   IceSlide " << hex(offset) << " " << name << " (" << bytes << " bytes) = " << ice_format(v);
                        if (bytes == 8 && v.bits > 0x10000 && v.bits < 0x7ffffffeffffULL && readable(v.bits, 8))
                        {
                            std::cout << " -> " << describe_object(v.bits);
                            if (name.find("Material") != std::string::npos)
                                ice_materials.insert(v.bits), ice_material_types.insert(value<Address>(v.bits));
                        }
                        std::cout << "\n";
                    }
                }
                for (unsigned i = 0; i < count; ++i)
                {
                    const auto field = value<Address>(fields + i * 8);
                    const auto name = string(value<Address>(field + 48));
                    const unsigned offset = value<std::uint16_t>(field + 24), bytes = value<std::uint16_t>(field + 22);
                    if (name.empty() || !bytes || bytes > 8 || offset + bytes > size)
                        continue;
                    IceValue v{0, bytes};
                    SIZE_T got = 0;
                    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(c + offset), &v.bits, bytes, &got) || got != bytes)
                        continue;
                    const auto key = graph + "." + name;
                    now[key] = v;
                    auto lower = name;
                    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                    if (named_count < 80 && std::any_of(std::begin(keys), std::end(keys), [&](const char *k) { return lower.find(k) != std::string::npos; }))
                    {
                        named += "Ice probe:   " + key + " = " + ice_format(v) + "\n";
                        ++named_count;
                    }
                }
            }
            std::cout << "Ice probe: level " << level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184))) << ", "
                      << now.size() << " settings on Crash. Scripts: " << (graphs.empty() ? "(none)" : graphs) << "\n"
                      << named;
            // what material is Crash standing on? look for objects of the same type as Crash_IceSlide's materials
            // in Crash and his movement controller, one pointer deep
            if (!ice_material_types.empty())
            {
                unsigned shown = 0;
                for (const auto &[root, label] : {std::pair<Address, const char *>{actor, "crash"}, {value<Address>(actor + 424), "controller"}})
                {
                    if (!root)
                        continue;
                    std::vector<Address> words(256);
                    if (!read(root, words))
                        continue;
                    for (unsigned i = 0; i < words.size() && shown < 30; ++i)
                    {
                        const auto p = words[i];
                        if (p < 0x10000 || p > 0x7ffffffeffffULL || (p & 7) || !readable(p, 512))
                            continue;
                        const auto describe = [&](Address q, const std::string &path)
                        {
                            if (!readable(q, 8) || !ice_material_types.count(value<Address>(q)))
                                return;
                            ++shown;
                            std::cout << "Ice probe: material at " << path << " = 0x" << std::hex << q << std::dec
                                      << (ice_materials.count(q) ? " (one of IceSlide's)" : "") << ": " << describe_object(q) << "\n";
                        };
                        describe(p, std::string(label) + " " + hex(i * 8));
                        std::vector<Address> inner(64);
                        if (read(p, inner))
                            for (unsigned j = 0; j < inner.size() && shown < 30; ++j)
                                if (inner[j] > 0x10000 && inner[j] < 0x7ffffffeffffULL && !(inner[j] & 7))
                                    describe(inner[j], std::string(label) + " " + hex(i * 8) + " -> " + hex(j * 8));
                    }
                }
                if (!shown)
                    std::cout << "Ice probe: no material of that type found on Crash or his controller\n";
            }
            // raw movement-controller words, compared with the last probe
            std::vector<unsigned> raw(512);
            const auto controller = value<Address>(actor + 424);
            SIZE_T got = 0;
            const bool raw_ok = typed(controller, 0x1589128) &&
                                ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(controller), raw.data(), raw.size() * 4, &got) && got == raw.size() * 4;
            if (!ice_probe_last.empty())
            {
                unsigned changed = 0;
                for (const auto &[key, v] : now)
                {
                    const auto old = ice_probe_last.find(key);
                    if (old == ice_probe_last.end() || old->second.bits == v.bits || changed >= 80)
                        continue;
                    std::cout << "Ice probe: changed " << key << ": " << ice_format(old->second) << " -> " << ice_format(v) << "\n";
                    ++changed;
                }
                if (raw_ok && ice_raw_last.size() == raw.size())
                    for (unsigned i = 2; i < raw.size() && changed < 140; ++i) // skip the vtable
                        if (raw[i] != ice_raw_last[i])
                        {
                            std::cout << "Ice probe: changed controller " << hex(i * 4) << ": " << ice_format({ice_raw_last[i], 4})
                                      << " -> " << ice_format({raw[i], 4}) << "\n";
                            ++changed;
                        }
                std::cout << "Ice probe: " << changed << " changes since the last probe\n";
            }
            else
                std::cout << "Ice probe: saved. Now stand on ice and run /ice_probe again to see what changes.\n";
            ice_probe_last = std::move(now);
            ice_raw_last = raw_ok ? raw : std::vector<unsigned>{};
            std::cout << std::flush;
        }
        // /ice_watch: every 0.2 s compare Crash_IceSlide's settings and log its flags and materials when they change.
        // It also sorts every other value on Crash (his script settings, raw words of Crash and of his movement
        // controller, and of the objects the controller points to) by whether the sample was taken on ice
        // (IceSlide's two ice flags on) or on normal ground (both off). /ice_watch off lists the values that differ
        // between the two: the switches the game flips for ice.
        std::map<std::string, IceValue> ice_watch_last;
        unsigned ice_watch_lines = 0;
        bool was_described_reset = false;
        struct IceSides
        {
            std::vector<std::uint64_t> seen[2]; // [0] normal ground, [1] ice
            bool varies[2] = {};
            unsigned size = 4;
        };
        std::map<std::string, IceSides> ice_sides; // script settings by name
        // raw words by number: root (1 Crash, 2 controller, 3 an object the controller points to), the controller
        // offset of that pointer, and the word's offset
        std::unordered_map<std::uint64_t, IceSides> ice_raw_sides;
        unsigned ice_side_samples[2] = {};
        static std::uint64_t ice_raw_key(unsigned root, unsigned pointer, unsigned offset)
        {
            return (std::uint64_t(root) << 48) | (std::uint64_t(pointer) << 24) | offset;
        }
        std::string ice_raw_label(std::uint64_t key)
        {
            const auto root = unsigned(key >> 48), pointer = unsigned((key >> 24) & 0xffffff), offset = unsigned(key & 0xffffff);
            switch (root)
            {
            case 1:
                return "crash " + hex(offset);
            case 2:
                return "controller " + hex(offset);
            case 3:
                return "controller " + hex(pointer) + " -> " + hex(offset);
            case 5:
                return "floor material " + hex(offset);
            case 6:
                return "floor material " + hex(pointer) + " -> " + hex(offset);
            default:
                return "crash " + hex(pointer) + " -> " + hex(offset);
            }
        }
        void ice_side_note(IceSides &s, bool ice, std::uint64_t bits, unsigned size)
        {
            s.size = size;
            auto &seen = s.seen[ice];
            if (std::find(seen.begin(), seen.end(), bits) != seen.end())
                return;
            if (seen.size() >= 3)
                s.varies[ice] = true;
            else
                seen.push_back(bits);
        }
        std::string ice_side_values(const IceSides &s, bool ice)
        {
            std::string out;
            for (const auto bits : s.seen[ice])
                out += (out.empty() ? "" : " / ") + ice_format({bits, s.size});
            return out + (s.varies[ice] ? " / ... (keeps changing)" : "");
        }
        void ice_side_report()
        {
            if (ice_side_samples[0] < 3 || ice_side_samples[1] < 3)
            {
                std::cout << "Ice watch: not enough samples to compare (" << ice_side_samples[0] << " on normal ground, "
                          << ice_side_samples[1] << " on ice). Stand and walk on both for a few seconds.\n";
                return;
            }
            unsigned switches = 0, moving = 0;
            std::vector<std::pair<std::string, const IceSides *>> all;
            for (const auto &[key, s] : ice_sides)
                all.push_back({key, &s});
            std::vector<std::pair<std::uint64_t, const IceSides *>> raw;
            for (const auto &[key, s] : ice_raw_sides)
                raw.push_back({key, &s});
            std::sort(raw.begin(), raw.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
            // floor material values first: they say what makes a floor ice
            std::stable_partition(raw.begin(), raw.end(), [](const auto &r) { return (r.first >> 48) == 5 || (r.first >> 48) == 6; });
            for (const auto &[key, s] : raw)
                all.push_back({ice_raw_label(key), s});
            for (const auto &[key, sides] : all)
            {
                const auto &s = *sides;
                if (s.seen[0].empty() || s.seen[1].empty())
                    continue;
                const bool disjoint = std::none_of(s.seen[0].begin(), s.seen[0].end(), [&](std::uint64_t b)
                                                   { return std::find(s.seen[1].begin(), s.seen[1].end(), b) != s.seen[1].end(); });
                if (!s.varies[0] && !s.varies[1] && disjoint && switches < 200)
                {
                    ++switches;
                    std::cout << "Ice switch: " << key << ": ground " << ice_side_values(s, false) << " | ice " << ice_side_values(s, true) << "\n";
                }
                else if (!s.varies[0] && s.seen[0].size() == 1 && s.varies[1] && moving < 60)
                {
                    ++moving;
                    std::cout << "Ice only: " << key << ": ground " << ice_side_values(s, false) << " | ice " << ice_side_values(s, true) << "\n";
                }
            }
            std::cout << "Ice watch: compared " << all.size() << " values over " << ice_side_samples[0] << " samples on normal ground and "
                      << ice_side_samples[1] << " on ice: " << switches << " switches, " << moving << " that only change on ice\n";
        }
        Address crash_script(Address actor, const char *graph)
        {
            for (const auto c : list(value<Address>(actor + 24), 128))
                if (typed(c, 0x13bbbd0) && value<Address>(c + 24) == actor && typed(value<Address>(c + 56), 0x1745d78) &&
                    string(value<Address>(value<Address>(c + 56) + 16)) == graph)
                    return c;
            return 0;
        }
        void ice_side_sample(Address actor, bool ice, Address material)
        {
            ++ice_side_samples[ice];
            // every script setting on Crash except the ice script itself
            for (const auto c : list(value<Address>(actor + 24), 128))
            {
                if (!typed(c, 0x13bbbd0) || value<Address>(c + 24) != actor)
                    continue;
                const auto meta = value<Address>(c + 56);
                if (!typed(meta, 0x1745d78))
                    continue;
                auto graph = string(value<Address>(meta + 16));
                if (graph.rfind("Scripts.Graph.", 0) == 0)
                    graph.erase(0, 14);
                if (graph == "Crash_IceSlide")
                    continue;
                const auto count = value<unsigned>(meta + 24), size = unsigned(value<std::uint16_t>(meta + 90));
                const auto fields = value<Address>(meta + 40);
                if (!fields || count > 512)
                    continue;
                for (unsigned i = 0; i < count; ++i)
                {
                    const auto field = value<Address>(fields + i * 8);
                    const auto name = string(value<Address>(field + 48));
                    const unsigned offset = value<std::uint16_t>(field + 24), bytes = value<std::uint16_t>(field + 22);
                    if (name.empty() || !bytes || bytes > 8 || offset + bytes > size)
                        continue;
                    std::uint64_t bits = 0;
                    SIZE_T got = 0;
                    if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(c + offset), &bits, bytes, &got) && got == bytes)
                        ice_side_note(ice_sides[graph + "." + name], ice, bits, bytes == 4 ? 4u : bytes == 1 ? 1u : 8u);
                }
            }
            // raw 4-byte words: Crash, his movement controller, and every object the controller points to
            const auto words = [&](Address at, unsigned count, unsigned root, unsigned pointer)
            {
                std::vector<std::uint32_t> raw(count);
                SIZE_T got = 0;
                if (!readable(at, count * 4) ||
                    !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(at), raw.data(), count * 4, &got) || got != count * 4)
                    return std::vector<std::uint32_t>{};
                for (unsigned i = 2; i < count; ++i) // skip the type word
                    ice_side_note(ice_raw_sides[ice_raw_key(root, pointer, i * 4)], ice, raw[i], 4);
                return raw;
            };
            words(actor, 256, 1, 0);
            // the floor material object under Crash (Crash_IceSlide's current material) and what it points to
            if (material)
            {
                const auto raw = words(material, 64, 5, 0);
                for (unsigned i = 2; i + 1 < raw.size(); i += 2)
                {
                    const Address p = raw[i] | (static_cast<Address>(raw[i + 1]) << 32);
                    if (p >= 0x10000 && p <= 0x7ffffffeffffULL && !(p & 7) && p != material)
                        words(p, 64, 6, i * 4);
                }
            }
            const auto controller = value<Address>(actor + 424);
            if (!typed(controller, 0x1589128))
                return;
            const auto raw = words(controller, 512, 2, 0);
            for (unsigned i = 2; i + 1 < raw.size(); i += 2)
            {
                const Address p = raw[i] | (static_cast<Address>(raw[i + 1]) << 32);
                if (p < 0x10000 || p > 0x7ffffffeffffULL || (p & 7) || p == controller || p == actor)
                    continue;
                words(p, 96, 3, i * 4);
            }
        }
        void update_ice_watch(bool on)
        {
            static std::uint64_t next = 0;
            static Address watched_actor = 0;
            static bool was_on = false;
            const auto now = GetTickCount64();
            if (!on)
            {
                if (was_on)
                    ice_side_report(), std::cout << std::flush;
                was_on = false;
                ice_watch_last.clear(), watched_actor = 0;
                return;
            }
            if (!was_on)
                ice_sides.clear(), ice_raw_sides.clear(), ice_side_samples[0] = ice_side_samples[1] = 0, ice_watch_lines = 0, was_on = true,
                was_described_reset = false;
            if (now < next)
                return;
            next = now + 200;
            const auto actor = player();
            if (!actor)
                return;
            if (actor != watched_actor)
                ice_watch_last.clear(), watched_actor = actor;
            const auto c = crash_script(actor, "Scripts.Graph.Crash_IceSlide");
            if (!c)
                return;
            const auto meta = value<Address>(c + 56);
            const auto count = value<unsigned>(meta + 24), size = unsigned(value<std::uint16_t>(meta + 90));
            const auto fields = value<Address>(meta + 40);
            if (!fields || count > 512)
                return;
            const bool first = ice_watch_last.empty();
            for (unsigned i = 0; i < count; ++i)
            {
                const auto field = value<Address>(fields + i * 8);
                const auto name = string(value<Address>(field + 48));
                const unsigned offset = value<std::uint16_t>(field + 24), bytes = value<std::uint16_t>(field + 22);
                if (name.empty() || !bytes || bytes > 8 || offset + bytes > size)
                    continue;
                IceValue v{0, bytes};
                SIZE_T got = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(c + offset), &v.bits, bytes, &got) || got != bytes)
                    continue;
                auto &old = ice_watch_last[name];
                // the two slide floats change every sample on ice: the comparison at the end covers them
                if (!first && old.bits != v.bits && ice_watch_lines < 300 && name.rfind("Float_", 0) != 0)
                {
                    ++ice_watch_lines;
                    std::cout << "Ice watch: " << name << ": " << ice_format(old) << " -> " << ice_format(v) << "\n";
                }
                old = v;
            }
            if (first)
                std::cout << "Ice watch: watching " << ice_watch_last.size() << " Crash_IceSlide settings and comparing everything else on Crash. Walk onto ice and off again.\n";
            const auto surface = ice_watch_last.find("Bool_id_bxsqi703"), mode = ice_watch_last.find("Bool_id_17gxyac6");
            const auto floor = ice_watch_last.find("Collision_Material_id_s2qfleo5");
            const Address material = floor == ice_watch_last.end() ? 0 : static_cast<Address>(floor->second.bits);
            static Address last_material = 0;
            const bool steady = material == last_material; // the floor has not just changed, so the ice flag matches it
            last_material = material;
            if (surface != ice_watch_last.end() && mode != ice_watch_last.end() && (surface->second.bits & 0xff) == (mode->second.bits & 0xff))
            {
                const bool ice = (surface->second.bits & 0xff) != 0;
                ice_side_sample(actor, ice, steady && readable(material, 256) ? material : 0);
                // describe the first ice floor and the first normal floor once: names of what they point to
                static bool described[2] = {};
                if (!was_described_reset)
                    described[0] = described[1] = false, was_described_reset = true;
                if (steady && material && readable(material, 256) && !described[ice])
                {
                    described[ice] = true;
                    std::cout << "Floor material (" << (ice ? "ice" : "normal ground") << "): 0x" << std::hex << material << std::dec << " "
                              << describe_object(material) << "\n";
                    std::vector<Address> inner(32);
                    if (read(material, inner))
                        for (unsigned i = 1; i < inner.size(); ++i)
                            if (inner[i] > 0x10000 && inner[i] < 0x7ffffffeffffULL && !(inner[i] & 7) && readable(inner[i], 72))
                                std::cout << "Floor material (" << (ice ? "ice" : "normal ground") << ")   " << hex(i * 8) << " -> "
                                          << describe_object(inner[i]) << "\n";
                }
            }
            std::cout << std::flush;
        }
        // /move_probe: finds where the game keeps the speed Crash is moving at. Every 50 ms it measures how Crash really
        // moved and checks every pair of floats in Crash, his movement controller and the objects they point to for
        // one pointing the same way. /move_probe off lists the ones that matched most often, with their size compared
        // to his real speed: a value whose size stays a steady multiple of it is his velocity.
        struct MoveMatch
        {
            unsigned aligned = 0;
            double ratio = 0, ratio_sq = 0, size = 0, size_sq = 0;
        };
        std::unordered_map<std::uint64_t, MoveMatch> move_matches;
        unsigned move_samples = 0;
        float move_fastest = 0;
        void move_probe_scan(Address at, unsigned bytes, unsigned root, unsigned pointer, float vx, float vy, float speed)
        {
            std::vector<float> raw(bytes / 4);
            SIZE_T got = 0;
            if (!readable(at, bytes) || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(at), raw.data(), bytes, &got) || got != bytes)
                return;
            for (unsigned i = 2; i + 1 < raw.size(); ++i)
            {
                const float x = raw[i], y = raw[i + 1];
                if (!std::isfinite(x) || !std::isfinite(y))
                    continue;
                const float size = std::hypot(x, y);
                if (size < 1e-3f || size > 1e6f || (x * vx + y * vy) / (size * speed) < 0.97f)
                    continue;
                auto &match = move_matches[ice_raw_key(root, pointer, i * 4)];
                const double ratio = size / speed;
                ++match.aligned, match.ratio += ratio, match.ratio_sq += ratio * ratio, match.size += size, match.size_sq += double(size) * size;
            }
        }
        // an object's pointers to other objects, by their offset
        void move_probe_pointees(Address at, unsigned qwords, unsigned root, Address skip, float vx, float vy, float speed)
        {
            std::vector<std::uint64_t> words(qwords);
            if (!read(at, words))
                return;
            for (unsigned i = 1; i < words.size(); ++i)
            {
                const auto p = static_cast<Address>(words[i]);
                if (p >= 0x10000 && p <= 0x7ffffffeffffULL && !(p & 7) && p != at && p != skip)
                    move_probe_scan(p, 384, root, i * 8, vx, vy, speed);
            }
        }
        void update_move_probe(bool on)
        {
            static bool was_on = false;
            static std::uint64_t next = 0, last_time = 0;
            static std::array<float, 3> last_pos{};
            static Address last_actor = 0;
            const auto now = GetTickCount64();
            if (!on)
            {
                if (was_on)
                {
                    std::vector<std::pair<std::uint64_t, const MoveMatch *>> best;
                    for (const auto &[key, match] : move_matches)
                        if (move_samples && match.aligned * 2 >= move_samples)
                            best.push_back({key, &match});
                    std::sort(best.begin(), best.end(), [](const auto &a, const auto &b) { return a.second->aligned > b.second->aligned; });
                    std::cout << "Move probe: " << move_samples << " samples while Crash moved (fastest " << static_cast<int>(move_fastest)
                              << " units/s); " << best.size() << " values pointed his way at least half the time\n";
                    for (std::size_t i = 0; i < best.size() && i < 25; ++i)
                    {
                        const auto &m = *best[i].second;
                        const double n = m.aligned, ratio = m.ratio / n, size = m.size / n;
                        const double ratio_sd = std::sqrt(std::max(0.0, m.ratio_sq / n - ratio * ratio));
                        const double size_sd = std::sqrt(std::max(0.0, m.size_sq / n - size * size));
                        std::cout << "Move probe: " << ice_raw_label(best[i].first) << ": his way in " << static_cast<int>(100 * n / move_samples)
                                  << "% of samples, " << ratio << "x his speed (spread " << ratio_sd << "), size " << size << " (spread " << size_sd << ")\n";
                    }
                    std::cout << std::flush;
                }
                was_on = false;
                return;
            }
            if (!was_on)
            {
                move_matches.clear(), move_samples = 0, move_fastest = 0, was_on = true, last_actor = 0;
                std::cout << "Move probe on: run around in all directions (turns, stops, starts), then /move_probe off.\n" << std::flush;
            }
            if (now < next)
                return;
            next = now + 50;
            const auto actor = player();
            std::array<float, 3> pos{};
            if (!actor || !read(actor + 32, pos))
            {
                last_actor = 0;
                return;
            }
            if (actor != last_actor || now - last_time > 200)
            {
                last_actor = actor, last_time = now, last_pos = pos;
                return;
            }
            const float dt = static_cast<float>(now - last_time) / 1000;
            const float vx = (pos[0] - last_pos[0]) / dt, vy = (pos[1] - last_pos[1]) / dt, speed = std::hypot(vx, vy);
            last_time = now, last_pos = pos;
            if (dt <= 0 || speed < 150 || speed > 3000)
                return;
            ++move_samples, move_fastest = std::max(move_fastest, speed);
            const auto controller = value<Address>(actor + 424);
            move_probe_scan(actor, 1024, 1, 0, vx, vy, speed);
            move_probe_pointees(actor, 128, 4, controller, vx, vy, speed);
            if (!typed(controller, 0x1589128))
                return;
            move_probe_scan(controller, 2048, 2, 0, vx, vy, speed);
            move_probe_pointees(controller, 256, 3, actor, vx, vy, speed);
        }
        // /ice_test (research for an ice trap that really slides Crash):
        //  slide: Crash's velocity (actor +0x60, units per second, found with /move_probe) only moves part of the way
        //         toward what the game wants each frame, at the rate the game's own ice uses, so his body keeps
        //         sliding while he turns and runs at once.
        //  floor: the floor under Crash (Crash_IceSlide's current floor info, /ice_watch) gets ice grip (+0x20/+0x24:
        //         0.5 on snow, 0.05 on ice) and the IceSurface (+0x18) once Crash has stood on ice in this level.
        static constexpr float ice_test_accel = 330; // units/s per second: the game's ice builds full speed in about 1.8 s
        struct IceSurfaceSeen
        {
            Address surface = 0, type = 0;
            std::string level;
        } ice_surface_seen;
        // the IceSurface asset, checked by its name (its first word is a counter the game keeps changing)
        bool ice_surface_valid()
        {
            const auto s = ice_surface_seen.surface;
            return s && readable(s, 16) && string(value<Address>(s + 8), 32) == "IceSurface";
        }
        Address crash_floor_info(Address actor)
        {
            const auto c = crash_script(actor, "Scripts.Graph.Crash_IceSlide");
            if (!c || !field_at(value<Address>(c + 56), "Collision_Material_id_s2qfleo5", 0x60, 8))
                return 0;
            const auto info = value<Address>(c + 0x60);
            return info && readable(info, 0x40) && typed(info, 0x13ea4d8) ? info : 0;
        }
        //  swap:  every reference to the surface Crash stands on (the level's floor data, /ice_find) is pointed at the
        //         IceSurface, so stepping onto that floor goes through the game's own ice check. New floors he steps
        //         on are swapped too. /ice_test off puts them all back.
        //  push:  Crash's position (actor +0x20) moves with momentum: each frame his movement is replaced by a velocity
        //         that follows the game's at the ice rate.
        // The swap searches all of the game's memory for a surface pointer, so everything it keeps itself is stored
        // scrambled (xor ice_mask) and its own scan buffer and every thread's stack are skipped: otherwise it finds
        // and overwrites its own notes and can no longer put the floors back.
        constexpr Address ice_mask = 0x5a3c96e1f00dbeefULL;
        std::mutex ice_swap_mutex;
        std::vector<std::pair<Address, Address>> ice_swapped;         // reference, original surface ^ ice_mask
        std::map<Address, std::uint64_t> ice_swap_surfaces;           // surface ^ ice_mask -> when last searched
        std::atomic_uint ice_swap_running{0};
        // every 8-byte value equal to `wanted` in the game's writable private memory, skipping thread stacks and the
        // buffer this search reads into
        template <class Match>
        void ice_scan(Match match)
        {
            std::vector<std::uint64_t> chunk(1 << 17);
            const Address own_lo = reinterpret_cast<Address>(chunk.data()), own_hi = own_lo + chunk.size() * 8;
            std::set<Address> stacks; // allocations that contain a guard page: thread stacks
            MEMORY_BASIC_INFORMATION info{};
            for (Address at = 0x10000; at < 0x7ffffffe0000ULL && VirtualQuery(reinterpret_cast<void *>(at), &info, sizeof(info));
                 at = reinterpret_cast<Address>(info.BaseAddress) + info.RegionSize)
            {
                const auto allocation = reinterpret_cast<Address>(info.AllocationBase);
                if (info.State == MEM_COMMIT && (info.Protect & PAGE_GUARD))
                    stacks.insert(allocation);
                if (info.State != MEM_COMMIT || info.Type != MEM_PRIVATE || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
                    !(info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) || stacks.count(allocation))
                    continue;
                const Address start = reinterpret_cast<Address>(info.BaseAddress), end = start + info.RegionSize;
                for (Address p = start; p < end; p += chunk.size() * 8)
                {
                    const auto bytes = std::min<std::uint64_t>(chunk.size() * 8, end - p);
                    SIZE_T got = 0;
                    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(p), chunk.data(), bytes, &got))
                        continue;
                    for (std::size_t i = 0; i < got / 8; ++i)
                    {
                        const Address where = p + i * 8;
                        if ((where < own_lo || where >= own_hi) && match(chunk[i]))
                            if (!match.add(where))
                                return;
                    }
                }
            }
        }
        struct ValueHits
        {
            Address wanted;
            std::vector<Address> *hits;
            bool operator()(std::uint64_t v) const { return v == wanted; }
            bool add(Address where) const { return hits->push_back(where), hits->size() < 50000; }
        };
        void ice_swap_surface(Address surface_masked, Address ice_masked, std::string name)
        {
            ++ice_swap_running;
            std::cout << "Ice test: swap: looking for every reference to " << name << " (a few seconds)\n" << std::flush;
            std::thread([surface_masked, ice_masked, name]
                        {
                const Address surface = surface_masked ^ ice_mask, ice = ice_masked ^ ice_mask;
                std::vector<Address> hits;
                ice_scan(ValueHits{surface, &hits});
                unsigned swapped = 0;
                {
                    std::lock_guard lock(ice_swap_mutex);
                    for (const auto at : hits)
                        if (value<Address>(at) == surface && write(at, ice))
                            ice_swapped.push_back({at, surface ^ ice_mask}), ++swapped;
                }
                std::cout << "Ice test: swap: " << name << " was referenced in " << hits.size() << " places; " << swapped
                          << " now point at the IceSurface\n"
                          << std::flush;
                --ice_swap_running; })
                .detach();
        }
        void ice_swap_restore()
        {
            std::lock_guard lock(ice_swap_mutex);
            const auto ice = ice_surface_seen.surface;
            unsigned restored = 0, changed = 0;
            for (auto it = ice_swapped.rbegin(); it != ice_swapped.rend(); ++it)
            {
                if (!readable(it->first, 8))
                    continue;
                if (value<Address>(it->first) == ice && write(it->first, it->second ^ ice_mask))
                    ++restored;
                else
                    ++changed; // the game reused that memory since: leave it alone
            }
            if (!ice_swapped.empty())
                std::cout << "Ice test: swap undone: " << restored << " floor references put back (" << changed
                          << " had already been replaced by the game)\n"
                          << std::flush;
            ice_swapped.clear(), ice_swap_surfaces.clear();
        }
        // levels without ice: find the IceSurface by its name ("IceSurface" at +8, "CollisionMaterials" at +0x18)
        std::atomic_bool ice_name_search_running{false};
        std::string ice_name_searched_level;
        void ice_surface_by_name(const std::string &level)
        {
            if (ice_name_search_running.exchange(true))
                return;
            ice_name_searched_level = level;
            std::cout << "Ice test: looking for the IceSurface in this level's memory (a few seconds)\n" << std::flush;
            std::thread([level]
                        {
                // pass 1: where the text "IceSurface" sits; pass 2: objects whose name pointer (+8) points at it
                struct TextHits
                {
                    std::vector<Address> *hits;
                    // names may sit at any byte: a word holding "Surf" anywhere is checked around it
                    bool operator()(std::uint64_t v) const
                    {
                        char text[8];
                        std::memcpy(text, &v, 8);
                        for (int k = 0; k <= 4; ++k)
                            if (std::memcmp(text + k, "Surf", 4) == 0)
                                return true;
                        return false;
                    }
                    bool add(Address where) const
                    {
                        for (Address start = where - 8; start < where + 8; ++start)
                            if (string(start, 16) == "IceSurface" &&
                                std::find(hits->begin(), hits->end(), start) == hits->end())
                                hits->push_back(start);
                        return hits->size() < 2000;
                    }
                };
                std::vector<Address> texts;
                ice_scan(TextHits{&texts});
                std::set<Address> wanted(texts.begin(), texts.end());
                struct NameHits
                {
                    const std::set<Address> *wanted;
                    std::vector<Address> *hits;
                    bool operator()(std::uint64_t v) const { return wanted->count(v) != 0; }
                    bool add(Address where) const { return hits->push_back(where), hits->size() < 2000; }
                };
                std::vector<Address> names;
                if (!wanted.empty())
                    ice_scan(NameHits{&wanted, &names});
                Address found = 0;
                for (const auto at : names)
                {
                    const auto object = at - 8;
                    if (readable(object, 0x20) && string(value<Address>(object + 0x18), 32) == "CollisionMaterials")
                    {
                        found = object;
                        break;
                    }
                }
                if (found)
                {
                    std::lock_guard lock(ice_swap_mutex);
                    ice_surface_seen = {found, value<Address>(found), level};
                }
                std::ostringstream out;
                out << (found ? "Ice test: found the IceSurface in this level's memory\n"
                              : "Ice test: this level has no IceSurface loaded (the swap only works where it exists)\n")
                    << "Ice test: the text \"IceSurface\" is in memory " << texts.size() << " times; " << names.size()
                    << " things point at it\n";
                for (std::size_t i = 0; i < names.size() && i < 12; ++i)
                {
                    const auto at = names[i];
                    Address object = 0;
                    for (Address q = at; q + 1024 > at && q >= 0x10000; q -= 8)
                        if (const auto v = value<Address>(q); v >= base() && v < module_end())
                        {
                            object = q;
                            break;
                        }
                    out << "Ice test:   0x" << std::hex << at;
                    if (object)
                        out << " in type +0x" << (value<Address>(object) - base()) << " +0x" << (at - object);
                    out << std::dec << " (+0x18 says \"" << string(value<Address>(at + 0x10), 32) << "\")\n";
                }
                std::cout << out.str() << std::flush;
                ice_name_search_running = false; })
                .detach();
        }
        //  const: the references to the IceSurface that are not floor contacts (the level's ice patches, and maybe the
        //         ice script's own "this is ice" reference) are pointed at the normal floor instead.
        //  copy:  the normal floor's surface gets the IceSurface's own values (everything in it that is not a pointer).
        // Both test how the game recognizes ice, for an Ice Trap that works in levels without ice.
        std::vector<std::pair<Address, Address>> ice_const_swapped; // reference, IceSurface ^ ice_mask
        std::vector<std::pair<Address, std::uint64_t>> ice_copied;   // surface word, original value
        Address ice_other_surface = 0;                                // the last normal floor Crash stood on
        std::atomic_bool ice_const_running{false};
        bool pointer_like(std::uint64_t v) { return v >= 0x10000 && v < 0x7fffffffffffULL && readable(static_cast<Address>(v), 8); }
        void ice_const_start()
        {
            if (ice_const_running.exchange(true))
                return;
            const Address ice_masked = ice_surface_seen.surface ^ ice_mask, other_masked = ice_other_surface ^ ice_mask;
            std::cout << "Ice test: const: looking for the IceSurface's references (a few seconds)\n" << std::flush;
            std::thread([ice_masked, other_masked]
                        {
                const Address ice = ice_masked ^ ice_mask, other = other_masked ^ ice_mask;
                std::vector<Address> hits;
                ice_scan(ValueHits{ice, &hits});
                std::ostringstream out;
                unsigned switched = 0;
                {
                    std::lock_guard lock(ice_swap_mutex);
                    for (const auto at : hits)
                    {
                        if (value<Address>(at - 0x18) == base() + 0x13ea4d8)
                            continue; // a floor contact
                        // what holds it: the nearest type word up to 1 KB before, and the words around it
                        Address object = 0;
                        for (Address q = at; q + 1024 > at && q >= 0x10000; q -= 8)
                            if (const auto v = value<Address>(q); v >= base() && v < module_end())
                            {
                                object = q;
                                break;
                            }
                        out << "Ice test: const:   0x" << std::hex << at;
                        if (object)
                            out << " in type +0x" << (value<Address>(object) - base()) << " +0x" << (at - object);
                        out << " | around:";
                        for (int k = -2; k <= 2; ++k)
                            out << " " << value<Address>(at + k * 8);
                        out << std::dec << "\n";
                        if (value<Address>(at) == ice && write(at, other))
                            ice_const_swapped.push_back({at, ice ^ ice_mask}), ++switched;
                    }
                }
                std::cout << "Ice test: const: the IceSurface had " << hits.size() << " references; " << switched
                          << " that are not floor contacts now point at the normal floor\n"
                          << out.str() << std::flush;
                ice_const_running = false; })
                .detach();
        }
        void ice_const_restore()
        {
            std::lock_guard lock(ice_swap_mutex);
            unsigned restored = 0;
            for (const auto &[at, ice] : ice_const_swapped)
                if (readable(at, 8) && value<Address>(at) == ice_other_surface && write(at, ice ^ ice_mask))
                    ++restored;
            if (!ice_const_swapped.empty())
                std::cout << "Ice test: const undone: " << restored << " of " << ice_const_swapped.size() << " put back\n" << std::flush;
            ice_const_swapped.clear();
        }
        void ice_copy_start()
        {
            const auto ice = ice_surface_seen.surface, other = ice_other_surface;
            std::ostringstream out;
            for (unsigned off = 0x10; off < 0x100; off += 8)
            {
                const auto mine = value<std::uint64_t>(other + off), theirs = value<std::uint64_t>(ice + off);
                if (mine == theirs || pointer_like(mine) || pointer_like(theirs))
                    continue;
                if (write(other + off, theirs))
                {
                    ice_copied.push_back({other + off, mine});
                    out << " +0x" << std::hex << off << std::dec;
                }
            }
            std::cout << "Ice test: copy: " << string(value<Address>(other + 8), 48) << " took " << ice_copied.size()
                      << " values from the IceSurface:" << out.str() << "\n" << std::flush;
        }
        //  name:    the normal floor's surface takes the IceSurface's name (+8), in case the game compares names
        //  copyall: the normal floor's surface takes every value of the IceSurface after its first word
        void ice_copy_words(unsigned from, unsigned to, const char *what)
        {
            const auto ice = ice_surface_seen.surface, other = ice_other_surface;
            const auto name = string(value<Address>(other + 8), 48);
            unsigned copied = 0;
            for (unsigned off = from; off < to; off += 8)
            {
                const auto mine = value<std::uint64_t>(other + off), theirs = value<std::uint64_t>(ice + off);
                if (mine != theirs && write(other + off, theirs))
                    ice_copied.push_back({other + off, mine}), ++copied;
            }
            std::cout << "Ice test: " << what << ": " << name << " took " << copied << " values from the IceSurface\n" << std::flush;
        }
        void ice_copy_restore()
        {
            for (auto it = ice_copied.rbegin(); it != ice_copied.rend(); ++it)
                write(it->first, it->second);
            if (!ice_copied.empty())
                std::cout << "Ice test: copy undone: " << ice_copied.size() << " values put back\n" << std::flush;
            ice_copied.clear();
        }
        // a message for the in-game console (copied into the shared state once per tick)
        std::string ice_note_text;
        void ice_note(const std::string &text)
        {
            std::cout << "Ice test: " << text << "\n" << std::flush;
            ice_note_text = text;
        }
        // a Crash_IceSlide setting by name: its offset and size, or {0, 0}
        std::pair<unsigned, unsigned> ice_slide_field(Address script, const char *name)
        {
            const auto meta = value<Address>(script + 56);
            const auto count = value<unsigned>(meta + 24);
            const auto fields = value<Address>(meta + 40);
            if (!fields || count > 512)
                return {0, 0};
            for (unsigned i = 0; i < count; ++i)
            {
                const auto field = value<Address>(fields + i * 8);
                if (string(value<Address>(field + 48)) == name)
                    return {value<std::uint16_t>(field + 24), value<std::uint16_t>(field + 22)};
            }
            return {0, 0};
        }
        //  which: copyall made the snow slippery, so this finds which one of the IceSurface's values does it. Each round
        //         gives the snow one of them while Crash stands on the ice; when he then walks onto the snow, it reads
        //         whether the game now treats it as ice (Crash_IceSlide's ice flag and the floor's grip), logs it and
        //         puts the value back. Walking ice -> snow -> ice ... runs the rounds one by one.
        struct IceWhich
        {
            bool active = false, applied = false, finished = false;
            std::string level;
            Address ice = 0, snow = 0;
            std::vector<unsigned> offsets;
            std::size_t round = 0;
            std::uint64_t original = 0, snow_since = 0;
            std::array<float, 2> ice_grip{}, snow_grip{};
            bool have_ice_grip = false, have_snow_grip = false;
            std::vector<unsigned> worked;
        } ice_which;
        void ice_which_restore()
        {
            auto &w = ice_which;
            if (w.applied && readable(w.snow + w.offsets[w.round], 8))
                write(w.snow + w.offsets[w.round], w.original);
            w.applied = false;
        }
        void ice_which_start(const std::string &level)
        {
            auto &w = ice_which;
            w = {};
            w.ice = ice_surface_seen.surface, w.snow = ice_other_surface, w.level = level;
            std::ostringstream out;
            out << "Ice test: which: " << string(value<Address>(w.snow + 8), 48) << " and the IceSurface differ in:\n";
            for (unsigned off = 0x10; off < 0x100; off += 8)
            {
                const auto snow = value<std::uint64_t>(w.snow + off), ice = value<std::uint64_t>(w.ice + off);
                if (snow == ice)
                    continue;
                const bool pointers = pointer_like(snow) || pointer_like(ice);
                out << "Ice test:   +0x" << std::hex << off << ": snow 0x" << snow << ", ice 0x" << ice << std::dec;
                if (pointer_like(ice))
                    out << " -> " << (ice == w.ice ? std::string("(the IceSurface itself)") : describe_object(static_cast<Address>(ice)));
                out << "\n";
                if (pointers) // the plain numbers were ruled out by /ice_test copy
                    w.offsets.push_back(off);
            }
            std::cout << out.str() << std::flush;
            if (w.offsets.empty())
            {
                ice_note("which: nothing left to try here. Type /ice_test off.");
                return;
            }
            w.active = true;
            ice_note("which: " + std::to_string(w.offsets.size()) + " rounds, testing on " + string(value<Address>(w.snow + 8), 48) +
                     ". Stand on the ice to start round 1.");
        }
        void update_ice_which(Address actor, const std::string &level)
        {
            auto &w = ice_which;
            if (!w.active || w.finished)
                return;
            if (level != w.level || !readable(w.snow, 0x100) || !readable(w.ice, 0x100))
            {
                w.applied = false, w.active = false;
                ice_note("which: stopped (left the level). Type /ice_test off.");
                return;
            }
            const auto info = actor ? crash_floor_info(actor) : 0;
            if (!info)
                return;
            const auto script = crash_script(actor, "Scripts.Graph.Crash_IceSlide");
            const auto [flag_at, flag_size] = script ? ice_slide_field(script, "Bool_id_bxsqi703") : std::pair<unsigned, unsigned>{0, 0};
            const bool flag = flag_at && flag_size && (value<std::uint8_t>(script + flag_at) != 0);
            const auto surface = value<Address>(info + 0x18);
            const auto grip = value<std::array<float, 2>>(info + 0x20);
            const auto now = GetTickCount64();
            if (surface == w.ice)
            {
                w.snow_since = 0;
                if (!w.have_ice_grip)
                {
                    w.ice_grip = grip, w.have_ice_grip = true;
                    std::cout << "Ice test: which: on the real ice the ice flag is " << flag << ", grip " << grip[0] << "/" << grip[1] << "\n" << std::flush;
                }
                if (!w.applied)
                {
                    const auto off = w.offsets[w.round];
                    w.original = value<std::uint64_t>(w.snow + off);
                    if (write(w.snow + off, value<std::uint64_t>(w.ice + off)))
                    {
                        w.applied = true;
                        std::ostringstream text;
                        text << "which: round " << (w.round + 1) << " of " << w.offsets.size() << " ready (+0x" << std::hex << off
                             << "). Walk onto the snow.";
                        ice_note(text.str());
                    }
                }
                return;
            }
            if (surface != w.snow)
            {
                w.snow_since = 0;
                return;
            }
            if (!w.applied)
            {
                if (!w.have_snow_grip)
                {
                    w.snow_grip = grip, w.have_snow_grip = true;
                    std::cout << "Ice test: which: on the plain snow the ice flag is " << flag << ", grip " << grip[0] << "/" << grip[1] << "\n" << std::flush;
                }
                return;
            }
            if (!w.snow_since)
                w.snow_since = now;
            if (now - w.snow_since < 800)
                return;
            // on the snow for a moment with this round's value: did the game take it as ice?
            const auto off = w.offsets[w.round];
            const bool icy_grip = w.have_ice_grip && std::fabs(grip[0] - w.ice_grip[0]) < 0.02f &&
                                  (!w.have_snow_grip || std::fabs(grip[0] - w.snow_grip[0]) > 0.02f);
            const bool slippery = flag || icy_grip;
            std::cout << "Ice test: which: round " << (w.round + 1) << " (+0x" << std::hex << off << std::dec << "): on the snow the ice flag is "
                      << flag << ", grip " << grip[0] << "/" << grip[1] << (slippery ? " -> ICE" : " -> normal") << "\n" << std::flush;
            if (slippery)
                w.worked.push_back(off);
            ice_which_restore();
            ++w.round;
            std::ostringstream text;
            text << "which: round " << w.round << (slippery ? " looks like ICE." : " looks normal.");
            if (w.round < w.offsets.size())
                text << " Walk back onto the ice for round " << (w.round + 1) << ".";
            else
            {
                w.finished = true;
                text << " All done";
                if (w.worked.empty())
                    text << ", none of them alone. Type /ice_test off and send me the log.";
                else
                {
                    text << ": it's";
                    for (const auto o : w.worked)
                        text << " +0x" << std::hex << o << std::dec;
                    text << ". Type /ice_test off and send me the log.";
                }
            }
            ice_note(text.str());
        }
        //  kick: the prototype Ice Trap. /ice_test which showed that a floor type's +0x28 (its "Ice"/"Snow"/... movement
        //        kind, type +0x13ea438) decides whether Crash slides, and the IceSurface exists in every level. But the
        //        game only looks at it when Crash's floor piece changes: Crash_IceSlide gets the new piece in
        //        Collision_Material s2qfleo5 and the old one in kuddrvja. Kick gives the floor type Crash stands on the
        //        Ice kind, then looks through Crash's objects for where the game keeps the piece he stands on and
        //        tries putting another piece there, one place at a time, until the ice script runs (his ice flag turns
        //        on). Turning it off puts the floor's kind back and kicks the same place again so he stops at once.
        struct IceKickHit
        {
            Address at = 0;
            std::string path;
        };
        struct IceKick
        {
            enum Stage { Idle, WaitIce, Searching, Trying, Holding, Stopping } stage = Idle;
            std::string level;
            Address script = 0, piece = 0, other = 0, surface = 0, kind = 0;
            unsigned kudd = 0, flag = 0;
            std::uint64_t surface_old = 0;
            bool applied = false;
            std::vector<IceKickHit> hits;
            std::size_t trial = 0;
            Address trial_at = 0;
            std::uint64_t trial_time = 0, kudd_before = 0;
            IceKickHit worked;
            std::uint64_t stop_time = 0;
        } ice_kick;
        std::mutex ice_kick_mutex;
        std::atomic_bool ice_kick_search_done{false};
        std::vector<IceKickHit> ice_kick_found;
        Address ice_kick_found_other = 0;
        void ice_kick_search(Address actor, Address piece, Address script, Address script_end)
        {
            ice_kick_search_done = false;
            std::thread([actor, piece, script, script_end]
                        {
                struct Node
                {
                    Address at;
                    unsigned size;
                    int depth;
                    std::string path;
                };
                const auto controller = value<Address>(actor + 424);
                std::deque<Node> queue{{actor, 0x800, 0, "crash"}, {controller, 0x1000, 0, "controller"}};
                std::set<Address> seen{actor, controller};
                std::vector<IceKickHit> hits;
                Address other = 0;
                const auto lo = base(), hi = module_end();
                while (!queue.empty() && seen.size() < 6000 && hits.size() < 40)
                {
                    const auto node = queue.front();
                    queue.pop_front();
                    std::vector<std::uint64_t> words(node.size / 8);
                    if (!read(node.at, words))
                    {
                        words.resize(0x100 / 8);
                        if (!read(node.at, words))
                            continue;
                    }
                    for (std::size_t i = 0; i < words.size(); ++i)
                    {
                        const auto w = static_cast<Address>(words[i]);
                        const Address where = node.at + i * 8;
                        std::ostringstream label;
                        label << node.path << " +0x" << std::hex << i * 8;
                        if (w == piece)
                        {
                            if (where < script || where >= script_end)
                                hits.push_back({where, label.str()});
                            continue;
                        }
                        if (w < 0x10000 || w > 0x7ffffffeffffULL || (w & 7) || (w >= lo && w < hi))
                            continue;
                        if (!other && typed(w, 0x13ea4d8) && readable(value<Address>(w + 0x18), 16))
                            other = w;
                        if (node.depth < 3 && seen.insert(w).second)
                            queue.push_back({w, 0x400, node.depth + 1, label.str() + " ->"});
                    }
                }
                {
                    std::lock_guard lock(ice_kick_mutex);
                    ice_kick_found = std::move(hits), ice_kick_found_other = other;
                }
                std::cout << "Ice test: kick: looked through " << seen.size() << " of Crash's objects\n" << std::flush;
                ice_kick_search_done = true; })
                .detach();
        }
        void ice_kick_restore_kind()
        {
            auto &k = ice_kick;
            if (k.applied && readable(k.surface + 0x28, 8) && value<Address>(k.surface + 0x28) == k.kind)
                write(k.surface + 0x28, k.surface_old);
            k.applied = false;
        }
        void update_ice_kick(bool on, Address actor, const std::string &level)
        {
            auto &k = ice_kick;
            const auto now = GetTickCount64();
            if (!on && k.stage != IceKick::Idle && k.stage != IceKick::Stopping)
            {
                // turning it off: put the floor's kind back, then kick the place that worked so he stops at once
                ice_kick_restore_kind();
                if (k.worked.at && level == k.level && actor && k.script && readable(k.worked.at, 8))
                {
                    const auto piece = value<Address>(k.script + 0x60);
                    const auto other = value<Address>(k.script + k.kudd);
                    if (piece && other && other != piece && value<Address>(k.worked.at) == piece && write(k.worked.at, other))
                    {
                        k.stage = IceKick::Stopping, k.stop_time = now, k.trial_at = k.worked.at, k.piece = piece, k.other = other;
                        ice_note("kick off: the floor is back to normal. Crash should stop sliding now.");
                        return;
                    }
                }
                k.stage = IceKick::Idle;
                ice_note("kick off: the floor is back to normal (he stops after stepping onto another piece of floor).");
                return;
            }
            if (k.stage == IceKick::Stopping)
            {
                if (now - k.stop_time < 300)
                    return;
                if (readable(k.trial_at, 8) && value<Address>(k.trial_at) == k.other)
                    write(k.trial_at, k.piece);
                const bool flag = k.script && k.flag && value<std::uint8_t>(k.script + k.flag);
                std::cout << "Ice test: kick off: the ice flag is now " << flag << "\n" << std::flush;
                k.stage = IceKick::Idle;
                return;
            }
            if (!on)
                return;
            if (k.stage != IceKick::Idle && level != k.level)
            {
                ice_kick_restore_kind();
                k.stage = IceKick::Idle;
                ice_note("kick: stopped (left the level). Type /ice_test off.");
                return;
            }
            if (!actor)
                return;
            const auto script = crash_script(actor, "Scripts.Graph.Crash_IceSlide");
            if (!script)
                return;
            if (k.stage == IceKick::Idle)
            {
                k = {};
                k.level = level, k.stage = IceKick::WaitIce, k.script = script;
                k.kudd = ice_slide_field(script, "Collision_Material_id_kuddrvja").first;
                k.flag = ice_slide_field(script, "Bool_id_bxsqi703").first;
                if (!(ice_surface_seen.level == level && ice_surface_valid()))
                    ice_surface_by_name(level);
                ice_note("kick: getting ready. Stand still on normal ground.");
                return;
            }
            if (k.stage == IceKick::WaitIce)
            {
                if (!(ice_surface_seen.level == level && ice_surface_valid()))
                {
                    if (!ice_name_search_running && ice_name_searched_level == level)
                    {
                        k.stage = IceKick::Holding;
                        ice_note("kick: no IceSurface found in this level. Type /ice_test off and send me the log.");
                    }
                    return; // the search by name is still running
                }
                const auto info = crash_floor_info(actor);
                const auto surface = info ? value<Address>(info + 0x18) : 0;
                const auto kind = value<Address>(ice_surface_seen.surface + 0x28);
                if (!typed(kind, 0x13ea438) || string(value<Address>(kind + 0x10), 16) != "Ice")
                {
                    k.stage = IceKick::Holding;
                    ice_note("kick: the IceSurface doesn't look right here. Type /ice_test off and send me the log.");
                    return;
                }
                if (!surface || surface == ice_surface_seen.surface || !readable(surface + 0x28, 8))
                    return; // on real ice, or in the air: wait for normal ground
                k.surface = surface, k.kind = kind, k.surface_old = value<std::uint64_t>(surface + 0x28);
                k.applied = write(surface + 0x28, kind);
                k.script = script, k.piece = value<Address>(script + 0x60);
                const auto size = unsigned(value<std::uint16_t>(value<Address>(script + 56) + 90));
                std::cout << "Ice test: kick: " << string(value<Address>(surface + 8), 48) << " now has the Ice kind (was "
                          << string(value<Address>(static_cast<Address>(k.surface_old) + 0x10), 32) << ")\n" << std::flush;
                ice_kick_search(actor, k.piece, script, script + size);
                k.stage = IceKick::Searching;
                ice_note("kick: " + string(value<Address>(surface + 8), 48) + " is now ice. Searching (stand still).");
                return;
            }
            if (k.stage == IceKick::Searching)
            {
                if (!ice_kick_search_done)
                    return;
                {
                    std::lock_guard lock(ice_kick_mutex);
                    k.hits = ice_kick_found;
                    if (ice_kick_found_other && ice_kick_found_other != k.piece)
                        k.other = ice_kick_found_other;
                }
                const auto kudd = value<Address>(script + k.kudd);
                if (kudd && kudd != k.piece && typed(kudd, 0x13ea4d8))
                    k.other = kudd; // the floor piece he stood on before: always a real one
                k.hits.push_back({script + 0x60, "Crash_IceSlide's own floor setting"});
                std::ostringstream out;
                out << "Ice test: kick: the floor piece 0x" << std::hex << k.piece << " is kept in " << std::dec << k.hits.size() << " places:\n";
                for (const auto &h : k.hits)
                    out << "Ice test:   " << h.path << "\n";
                std::cout << out.str() << std::flush;
                if (!k.other)
                {
                    k.stage = IceKick::Holding;
                    ice_note("kick: no other floor piece to try with. Walk around a bit, then /ice_test off and /ice_test kick again.");
                    return;
                }
                k.stage = IceKick::Trying, k.trial = 0, k.trial_at = 0;
                ice_note("kick: trying " + std::to_string(k.hits.size()) + " places (stand still, a few seconds).");
                return;
            }
            if (k.stage != IceKick::Trying)
                return;
            const bool flag = k.flag && value<std::uint8_t>(script + k.flag);
            if (k.trial_at)
            {
                const auto kudd = value<Address>(script + k.kudd);
                const bool fired = flag || kudd != k.kudd_before;
                if (!fired && now - k.trial_time < 350)
                    return;
                if (readable(k.trial_at, 8) && value<Address>(k.trial_at) == k.other)
                    write(k.trial_at, k.piece);
                const auto &hit = k.hits[k.trial];
                std::cout << "Ice test: kick: try " << (k.trial + 1) << " (" << hit.path << "): "
                          << (flag ? "the ice script ran, ICE ON" : fired ? "the floor changed, but no ice" : "nothing") << "\n" << std::flush;
                k.trial_at = 0;
                if (flag)
                {
                    k.worked = hit, k.stage = IceKick::Holding;
                    ice_note("kick: it worked (try " + std::to_string(k.trial + 1) + ")! Crash should slide now. /ice_test off to stop it.");
                    return;
                }
                ++k.trial;
            }
            if (flag)
            {
                k.stage = IceKick::Holding;
                ice_note("kick: Crash is already on ice. /ice_test off, stand on normal ground, then /ice_test kick again.");
                return;
            }
            if (value<Address>(script + 0x60) != k.piece)
            {
                k.stage = IceKick::Holding;
                ice_note("kick: Crash moved to another piece of floor during the test. /ice_test off and try again standing still.");
                return;
            }
            if (k.trial >= k.hits.size())
            {
                k.stage = IceKick::Holding;
                ice_note("kick: none of them worked. Walk onto another piece of floor to start sliding. Send me the log.");
                return;
            }
            const auto at = k.hits[k.trial].at;
            if (!readable(at, 8) || value<Address>(at) != k.piece)
            {
                ++k.trial;
                return;
            }
            k.kudd_before = value<Address>(script + k.kudd);
            if (write(at, k.other))
                k.trial_at = at, k.trial_time = now;
            else
                ++k.trial;
        }
        //  track: finds where the game keeps the floor piece Crash stands on, by watching memory. It searches all of
        //         the game's memory for the piece he stands on, then each time he walks onto a different kind of
        //         floor keeps only the places that changed to the new piece. What is left follows his floor. Then
        //         it gives his floor the Ice kind and puts another piece in those places one at a time: the right
        //         one makes the game re-check the floor at once (his ice flag turns on and he slides). Off puts the
        //         kind back and does the same so he stops at once.
        struct IceTrack
        {
            enum Stage { Idle, Scanning, Narrowing, Trying, Holding, Stopping } stage = Idle;
            std::string level;
            Address script = 0, script_end = 0, surface = 0, kind = 0, fake = 0, piece = 0;
            unsigned kudd = 0, flag = 0;
            std::uint64_t surface_old = 0, changed_time = 0, trial_time = 0, stop_time = 0;
            Address last_seen = 0, filtered_for = 0;
            unsigned changes = 0;
            bool applied = false;
            std::vector<Address> places;
            std::set<Address> pieces_seen; // stored ^ ice_mask
            std::size_t trial = 0;
            Address trial_at = 0, worked = 0;
        } ice_track;
        std::atomic_bool ice_track_scan_done{false};
        std::vector<Address> ice_track_found;
        std::mutex ice_track_mutex;
        void ice_track_restore_kind()
        {
            auto &t = ice_track;
            if (t.applied && readable(t.surface + 0x28, 8) && value<Address>(t.surface + 0x28) == t.kind)
                write(t.surface + 0x28, t.surface_old);
            t.applied = false;
        }
        std::string ice_track_owner(Address at, Address actor)
        {
            std::ostringstream out;
            Address object = 0;
            for (Address q = at; q + 1024 > at && q >= 0x10000; q -= 8)
                if (const auto v = value<Address>(q); v >= base() && v < module_end())
                {
                    object = q;
                    break;
                }
            if (!object)
                return "no type nearby";
            out << "type +0x" << std::hex << (value<Address>(object) - base()) << " +0x" << (at - object);
            // is that object Crash, his controller, one of his components, or pointed to by one of them?
            std::vector<std::pair<Address, std::string>> roots{{actor, "crash"}, {value<Address>(actor + 424), "controller"}};
            const auto components = list(value<Address>(actor + 24), 256);
            for (std::size_t i = 0; i < components.size(); ++i)
                roots.push_back({components[i], "component " + std::to_string(i) + " (type +0x" + [&]
                                                                         {
                                                                             std::ostringstream t;
                                                                             t << std::hex << (value<Address>(components[i]) - base());
                                                                             return t.str();
                                                                         }() + ")"});
            for (const auto &[root, name] : roots)
            {
                if (object == root)
                {
                    out << " = " << name;
                    return out.str();
                }
                std::vector<std::uint64_t> words(0x800 / 8);
                if (!read(root, words))
                    continue;
                for (std::size_t i = 0; i < words.size(); ++i)
                    if (words[i] == object)
                    {
                        out << " <- " << name << " +0x" << std::hex << i * 8;
                        return out.str();
                    }
            }
            return out.str();
        }
        void update_ice_track(bool on, Address actor, const std::string &level)
        {
            auto &t = ice_track;
            const auto now = GetTickCount64();
            if (!on && t.stage != IceTrack::Idle && t.stage != IceTrack::Stopping)
            {
                ice_track_restore_kind();
                if (t.worked && level == t.level && readable(t.worked, 8) && readable(t.script + 0x60, 8))
                {
                    const auto piece = value<Address>(t.script + 0x60);
                    if (piece && t.fake && value<Address>(t.worked) == piece && write(t.worked, t.fake))
                    {
                        t.stage = IceTrack::Stopping, t.stop_time = now, t.piece = piece;
                        ice_note("track off: the floor is back to normal. Crash should stop sliding now.");
                        return;
                    }
                }
                t.stage = IceTrack::Idle;
                ice_note("track off: the floor is back to normal.");
                return;
            }
            if (t.stage == IceTrack::Stopping)
            {
                if (now - t.stop_time < 300)
                    return;
                if (readable(t.worked, 8) && value<Address>(t.worked) == t.fake)
                    write(t.worked, t.piece);
                std::cout << "Ice test: track off: the ice flag is now " << (t.flag && value<std::uint8_t>(t.script + t.flag)) << "\n" << std::flush;
                t.stage = IceTrack::Idle;
                return;
            }
            if (!on)
                return;
            if (t.stage != IceTrack::Idle && level != t.level)
            {
                ice_track_restore_kind();
                t.stage = IceTrack::Idle;
                ice_note("track: stopped (left the level). Type /ice_test off.");
                return;
            }
            if (!actor)
                return;
            const auto script = crash_script(actor, "Scripts.Graph.Crash_IceSlide");
            if (!script)
                return;
            const auto piece = value<Address>(script + 0x60);
            if (t.stage == IceTrack::Idle)
            {
                if (!typed(piece, 0x13ea4d8))
                    return; // in the air or loading
                t = {};
                t.level = level, t.script = script;
                t.script_end = script + value<std::uint16_t>(value<Address>(script + 56) + 90);
                t.kudd = ice_slide_field(script, "Collision_Material_id_kuddrvja").first;
                t.flag = ice_slide_field(script, "Bool_id_bxsqi703").first;
                t.last_seen = t.filtered_for = piece, t.changed_time = now;
                t.pieces_seen.insert(piece ^ ice_mask);
                t.stage = IceTrack::Scanning;
                ice_track_scan_done = false;
                const Address wanted_masked = piece ^ ice_mask;
                std::thread([wanted_masked]
                            {
                    std::vector<Address> hits;
                    struct Hits
                    {
                        Address wanted;
                        std::vector<Address> *hits;
                        bool operator()(std::uint64_t v) const { return v == wanted; }
                        bool add(Address where) const { return hits->push_back(where), hits->size() < 400000; }
                    };
                    ice_scan(Hits{wanted_masked ^ ice_mask, &hits});
                    {
                        std::lock_guard lock(ice_track_mutex);
                        ice_track_found = std::move(hits);
                    }
                    ice_track_scan_done = true; })
                    .detach();
                if (!(ice_surface_seen.level == level && ice_surface_valid()))
                    ice_surface_by_name(level);
                ice_note("track: searching memory (stand still, a few seconds).");
                return;
            }
            if (t.stage == IceTrack::Scanning)
            {
                if (!ice_track_scan_done)
                    return;
                {
                    std::lock_guard lock(ice_track_mutex);
                    t.places = std::move(ice_track_found);
                    ice_track_found.clear();
                }
                // keep the places that still hold the piece he stands on
                const auto now_piece = piece;
                t.places.erase(std::remove_if(t.places.begin(), t.places.end(),
                                              [&](Address at) { return value<Address>(at) != now_piece; }),
                               t.places.end());
                std::cout << "Ice test: track: the floor piece 0x" << std::hex << piece << std::dec << " ("
                          << string(value<Address>(value<Address>(piece + 0x18) + 8), 48) << ") is in " << t.places.size() << " places\n"
                          << std::flush;
                t.stage = IceTrack::Narrowing;
                ice_note("track: found " + std::to_string(t.places.size()) +
                         " places. Now walk onto the ice, then back onto the snow, then onto the ice and the snow again.");
                return;
            }
            if (t.stage == IceTrack::Narrowing)
            {
                if (piece != t.last_seen)
                    t.last_seen = piece, t.changed_time = now;
                if (!typed(piece, 0x13ea4d8) || piece == t.filtered_for || now - t.changed_time < 400)
                    return;
                // he has stood on a different piece for a moment: keep only the places that changed to it
                const auto before = t.places.size();
                t.places.erase(std::remove_if(t.places.begin(), t.places.end(),
                                              [&](Address at) { return value<Address>(at) != piece; }),
                               t.places.end());
                t.filtered_for = piece, ++t.changes;
                t.pieces_seen.insert(piece ^ ice_mask);
                const auto name = string(value<Address>(value<Address>(piece + 0x18) + 8), 48);
                std::cout << "Ice test: track: on " << name << " (0x" << std::hex << piece << std::dec << "): " << before << " -> "
                          << t.places.size() << " places\n"
                          << std::flush;
                if (t.places.empty())
                {
                    t.stage = IceTrack::Holding;
                    ice_note("track: lost it (the game doesn't keep the floor that way). Type /ice_test off and send me the log.");
                    return;
                }
                const bool on_ice = ice_surface_seen.level == level && value<Address>(piece + 0x18) == ice_surface_seen.surface;
                if (t.changes < 3 || t.places.size() > 24 || on_ice)
                {
                    ice_note("track: " + std::to_string(t.places.size()) + " places left. Keep walking: " +
                             (on_ice ? "now onto the snow." : "now onto the ice."));
                    return;
                }
                std::ostringstream out;
                out << "Ice test: track: these follow Crash's floor:\n";
                for (const auto at : t.places)
                    out << "Ice test:   0x" << std::hex << at << std::dec
                        << (at >= t.script && at < t.script_end ? " (Crash_IceSlide's own setting)" : "") << " in "
                        << ice_track_owner(at, actor) << "\n";
                std::cout << out.str() << std::flush;
                // another piece to put in their place: one he stood on before
                for (const auto masked : t.pieces_seen)
                    if (const auto p = masked ^ ice_mask; p != piece && typed(p, 0x13ea4d8))
                    {
                        t.fake = p;
                        break;
                    }
                const auto kind = ice_surface_valid() ? value<Address>(ice_surface_seen.surface + 0x28) : 0;
                const auto surface = value<Address>(piece + 0x18);
                if (!t.fake || !typed(kind, 0x13ea438) || !readable(surface + 0x28, 8))
                {
                    t.stage = IceTrack::Holding;
                    ice_note("track: found the places but can't test them here. Type /ice_test off and send me the log.");
                    return;
                }
                t.surface = surface, t.kind = kind, t.surface_old = value<std::uint64_t>(surface + 0x28);
                t.applied = write(surface + 0x28, kind);
                t.piece = piece, t.trial = 0, t.trial_at = 0, t.stage = IceTrack::Trying;
                ice_note("track: " + std::to_string(t.places.size()) + " places follow your floor. " + name +
                         " is now ice: stand still while it tries them.");
                return;
            }
            if (t.stage != IceTrack::Trying)
                return;
            const bool flag = t.flag && value<std::uint8_t>(script + t.flag);
            if (t.trial_at)
            {
                if (!flag && now - t.trial_time < 350)
                    return;
                if (readable(t.trial_at, 8) && value<Address>(t.trial_at) == t.fake)
                    write(t.trial_at, t.piece);
                std::cout << "Ice test: track: try " << (t.trial + 1) << " (0x" << std::hex << t.trial_at << std::dec << "): "
                          << (flag ? "ICE ON" : "nothing") << "\n" << std::flush;
                if (flag)
                {
                    t.worked = t.trial_at, t.trial_at = 0, t.stage = IceTrack::Holding;
                    ice_note("track: it worked (try " + std::to_string(t.trial + 1) + ")! Crash should slide now. /ice_test off to stop it.");
                    return;
                }
                t.trial_at = 0, ++t.trial;
            }
            else if (flag)
            {
                t.stage = IceTrack::Holding;
                ice_note("track: Crash is on ice already. /ice_test off, stand on the snow, and /ice_test track again.");
                return;
            }
            if (piece != t.piece)
            {
                t.stage = IceTrack::Holding;
                ice_note("track: you moved onto other floor during the test. /ice_test off and try again.");
                return;
            }
            if (t.trial >= t.places.size())
            {
                t.stage = IceTrack::Holding;
                ice_note("track: none of them made the game re-check the floor. Type /ice_test off and send me the log.");
                return;
            }
            const auto at = t.places[t.trial];
            if (!readable(at, 8) || value<Address>(at) != t.piece)
            {
                ++t.trial;
                return;
            }
            if (write(at, t.fake))
                t.trial_at = at, t.trial_time = now;
            else
                ++t.trial;
        }
        //  who / whopage (research): catch the game code that writes Crash_IceSlide's floor setting (s2qfleo5) when
        //  Crash walks onto a different kind of floor, with the chain of calls that led there and each frame's saved
        //  registers, then dump those functions' code to the log. That shows where the game notices the floor
        //  change and what it compares, so the Ice Trap can make it re-check the floor at once.
        //  who:     a hardware write watch (debug register 0) on that setting, in every thread of the game.
        //  whopage: if the hardware watch is refused, the page holding it is guarded instead (slower).
        struct WhoFrame
        {
            std::uint64_t rip, rsp, rbx, rbp, rsi, rdi, r12, r13, r14, r15;
        };
        struct WhoCapture
        {
            std::uint64_t value = 0;
            unsigned count = 0;
            DWORD thread = 0;
            WhoFrame frames[20]{};
        };
        WhoCapture who_captures[6];
        std::atomic_uint who_count{0}, who_hits{0};
        std::atomic<std::uint64_t> who_target{0}, who_last{0}, who_arm_address{0}, who_page{0};
        std::atomic_bool who_armed{false};
        PVOID who_handler_cookie = nullptr;
        constexpr DWORD who_arm_code = 0xE0C0FFEE; // raised by us: sets this thread's debug registers
        void who_capture(const CONTEXT &context)
        {
            const auto target = who_target.load();
            if (!target)
                return;
            ++who_hits;
            const auto v = *reinterpret_cast<volatile std::uint64_t *>(target);
            if (v == who_last.load())
                return;
            who_last = v;
            const auto i = who_count.load();
            if (i >= std::size(who_captures))
                return;
            auto &cap = who_captures[i];
            cap.value = v, cap.thread = GetCurrentThreadId();
            CONTEXT c = context;
            unsigned n = 0;
            for (; n < 20 && c.Rip >= 0x10000 && c.Rsp; ++n)
            {
                cap.frames[n] = {c.Rip, c.Rsp, c.Rbx, c.Rbp, c.Rsi, c.Rdi, c.R12, c.R13, c.R14, c.R15};
                DWORD64 image = 0;
                const auto entry = RtlLookupFunctionEntry(c.Rip, &image, nullptr);
                if (!entry)
                {
                    c.Rip = *reinterpret_cast<const std::uint64_t *>(c.Rsp);
                    c.Rsp += 8;
                }
                else
                {
                    PVOID data = nullptr;
                    DWORD64 frame = 0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, c.Rip, entry, &c, &data, &frame, nullptr);
                }
            }
            cap.count = n;
            who_count = i + 1;
        }
        LONG CALLBACK who_handler(EXCEPTION_POINTERS *e)
        {
            auto *record = e->ExceptionRecord;
            auto *context = e->ContextRecord;
            if (record->ExceptionCode == who_arm_code)
            {
                const auto at = who_arm_address.load();
                if (at)
                    context->Dr0 = at, context->Dr7 = (context->Dr7 & ~0xF0003ULL) | 1ULL | (1ULL << 16) | (2ULL << 18);
                else
                    context->Dr0 = 0, context->Dr7 &= ~0xF0003ULL;
                context->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
                return EXCEPTION_CONTINUE_EXECUTION;
            }
            const auto page = who_page.load();
            if (record->ExceptionCode == STATUS_GUARD_PAGE_VIOLATION)
            {
                if (!page || (record->ExceptionInformation[1] & ~0xFFFULL) != page)
                    return EXCEPTION_CONTINUE_SEARCH;
                context->EFlags |= 0x100; // step over this access, then guard the page again
                return EXCEPTION_CONTINUE_EXECUTION;
            }
            if (record->ExceptionCode != EXCEPTION_SINGLE_STEP)
                return EXCEPTION_CONTINUE_SEARCH;
            if (context->Dr6 & 1) // the hardware watch
            {
                context->Dr6 &= ~0xFULL;
                if (who_armed)
                    who_capture(*context);
                return EXCEPTION_CONTINUE_EXECUTION;
            }
            if (page && (context->Dr6 & 0x4000)) // the step after a guarded access
            {
                if (who_armed)
                {
                    who_capture(*context);
                    DWORD old = 0;
                    VirtualProtect(reinterpret_cast<void *>(page), 1, PAGE_READWRITE | PAGE_GUARD, &old);
                }
                return EXCEPTION_CONTINUE_EXECUTION;
            }
            return EXCEPTION_CONTINUE_SEARCH;
        }
        // the hardware watch on (at != 0) or off in every thread; returns how many threads took it
        unsigned who_debug_registers(std::uint64_t at)
        {
            if (!who_handler_cookie)
                who_handler_cookie = AddVectoredExceptionHandler(1, who_handler);
            who_arm_address = at;
            const DWORD self = GetCurrentThreadId(), process = GetCurrentProcessId();
            unsigned done = 0;
            const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshot != INVALID_HANDLE_VALUE)
            {
                THREADENTRY32 entry{};
                entry.dwSize = sizeof(entry);
                for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry))
                {
                    if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self)
                        continue;
                    const HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
                    if (!thread)
                        continue;
                    if (SuspendThread(thread) != static_cast<DWORD>(-1))
                    {
                        CONTEXT c{};
                        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                        if (GetThreadContext(thread, &c))
                        {
                            if (at)
                                c.Dr0 = at, c.Dr7 = (c.Dr7 & ~0xF0003ULL) | 1ULL | (1ULL << 16) | (2ULL << 18);
                            else
                                c.Dr0 = 0, c.Dr7 &= ~0xF0003ULL;
                            if (SetThreadContext(thread, &c))
                                ++done;
                        }
                        ResumeThread(thread);
                    }
                    CloseHandle(thread);
                }
                CloseHandle(snapshot);
            }
            RaiseException(who_arm_code, 0, 0, nullptr); // this thread, through the handler
            return done + 1;
        }
        struct WhoState
        {
            unsigned mode = 0, reported = 0;
            std::uint64_t started = 0;
            DWORD page_protect = 0;
            bool finished = false;
            std::set<std::uint64_t> dumped;
        } who_state;
        void who_stop()
        {
            auto &w = who_state;
            if (!w.mode)
                return;
            who_armed = false;
            if (w.mode == 12)
                who_debug_registers(0);
            if (const auto page = who_page.exchange(0))
            {
                DWORD old = 0;
                VirtualProtect(reinterpret_cast<void *>(page), 1, w.page_protect ? w.page_protect : PAGE_READWRITE, &old);
            }
            who_target = 0;
            std::cout << "Ice test: who: watch off (" << who_hits.load() << " writes seen)\n" << std::flush;
            w.mode = 0;
        }
        std::string who_register(std::uint64_t v)
        {
            std::ostringstream out;
            out << "0x" << std::hex << v;
            if (v >= 0x10000 && v < 0x7ffffffeffffULL && readable(static_cast<Address>(v), 8))
                if (const auto type = value<Address>(static_cast<Address>(v)); type >= base() && type < module_end())
                    out << "(type +0x" << (type - base()) << ")";
            return out.str();
        }
        void who_report()
        {
            auto &w = who_state;
            const auto count = who_count.load();
            for (; w.reported < count; ++w.reported)
            {
                const auto &cap = who_captures[w.reported];
                std::ostringstream out;
                out << "Who: write " << (w.reported + 1) << " (thread " << cap.thread << "): the floor setting became 0x" << std::hex
                    << cap.value << std::dec;
                if (typed(static_cast<Address>(cap.value), 0x13ea4d8))
                    out << " (" << string(value<Address>(value<Address>(static_cast<Address>(cap.value) + 0x18) + 8), 48) << ")";
                out << "\n";
                for (unsigned k = 0; k < cap.count; ++k)
                {
                    const auto &f = cap.frames[k];
                    out << "Who:   #" << k << " ";
                    if (f.rip >= base() && f.rip < module_end())
                        out << "+0x" << std::hex << (f.rip - base());
                    else
                        out << "0x" << std::hex << f.rip;
                    out << std::dec << " rsp=0x" << std::hex << f.rsp << std::dec << " rbx=" << who_register(f.rbx)
                        << " rbp=" << who_register(f.rbp) << " rsi=" << who_register(f.rsi) << " rdi=" << who_register(f.rdi)
                        << " r12=" << who_register(f.r12) << " r13=" << who_register(f.r13) << " r14=" << who_register(f.r14)
                        << " r15=" << who_register(f.r15) << "\n";
                }
                std::cout << out.str() << std::flush;
                // the code of every function in the chain, once
                for (unsigned k = 0; k < cap.count && k < 14; ++k)
                {
                    const auto rip = cap.frames[k].rip;
                    DWORD64 image = 0;
                    const auto entry = RtlLookupFunctionEntry(rip, &image, nullptr);
                    if (!entry || image != base())
                        continue;
                    const std::uint64_t begin = image + entry->BeginAddress, end = image + entry->EndAddress;
                    if (!w.dumped.insert(begin).second)
                        continue;
                    const auto stop = std::min<std::uint64_t>(end, begin + 4096);
                    std::ostringstream code;
                    code << "Who: code +0x" << std::hex << (begin - base()) << "..+0x" << (end - base()) << "\n";
                    for (std::uint64_t at = begin; at < stop; at += 64)
                    {
                        std::array<unsigned char, 64> bytes{};
                        const auto n = static_cast<unsigned>(std::min<std::uint64_t>(64, stop - at));
                        SIZE_T got = 0;
                        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(at), bytes.data(), n, &got);
                        code << "Who: +0x" << (at - base()) << " ";
                        for (unsigned b = 0; b < got; ++b)
                            code << std::setw(2) << std::setfill('0') << unsigned(bytes[b]);
                        code << "\n";
                    }
                    std::cout << code.str() << std::flush;
                }
            }
        }
        void update_ice_who(unsigned mode, Address actor)
        {
            auto &w = who_state;
            const auto now = GetTickCount64();
            if (w.mode && mode != w.mode)
            {
                who_report();
                who_stop();
            }
            if (mode != 12 && mode != 13)
            {
                w.finished = false;
                return;
            }
            if (w.finished)
                return;
            if (!w.mode)
            {
                const auto script = actor ? crash_script(actor, "Scripts.Graph.Crash_IceSlide") : 0;
                if (!script || !field_at(value<Address>(script + 56), "Collision_Material_id_s2qfleo5", 0x60, 8))
                    return;
                for (auto &cap : who_captures)
                    cap = {};
                who_count = 0, who_hits = 0;
                w = {};
                w.mode = mode, w.started = now;
                const auto target = script + 0x60;
                who_last = value<std::uint64_t>(target);
                who_target = target;
                who_armed = true;
                if (mode == 12)
                {
                    const auto threads = who_debug_registers(target);
                    std::cout << "Ice test: who: hardware watch on in " << threads << " threads\n" << std::flush;
                }
                else
                {
                    if (!who_handler_cookie)
                        who_handler_cookie = AddVectoredExceptionHandler(1, who_handler);
                    MEMORY_BASIC_INFORMATION info{};
                    VirtualQuery(reinterpret_cast<void *>(target), &info, sizeof(info));
                    w.page_protect = info.Protect;
                    const auto page = target & ~0xFFFULL;
                    who_page = page;
                    DWORD old = 0;
                    VirtualProtect(reinterpret_cast<void *>(page), 1, PAGE_READWRITE | PAGE_GUARD, &old);
                    std::cout << "Ice test: whopage: page 0x" << std::hex << page << std::dec << " guarded\n" << std::flush;
                }
                ice_note("who: watching. Walk onto the ice, then back onto the snow.");
                return;
            }
            if (w.mode == 13)
                if (const auto page = who_page.load())
                {
                    DWORD old = 0; // our own reads of that page take the guard off: put it back
                    VirtualProtect(reinterpret_cast<void *>(page), 1, PAGE_READWRITE | PAGE_GUARD, &old);
                }
            who_report();
            if (who_count.load() >= 3 || who_count.load() >= std::size(who_captures))
            {
                who_stop();
                w.finished = true;
                ice_note("who: caught it. Type /ice_test off and send me the log.");
            }
            else if (now - w.started > 30000)
            {
                const auto caught = who_count.load();
                who_stop();
                w.finished = true;
                ice_note(caught ? "who: caught " + std::to_string(caught) + ". Type /ice_test off and send me the log."
                                : "who: nothing caught in 30 seconds. Type /ice_test off and send me the log.");
            }
        }
        //  contact (research): the floor change is noticed in the game's code at +0x510b70, called every frame from
        //  Crash's update (+0x4bcc70) with Crash and his ground contact object (type +0x1509a38). This dumps that code
        //  and the functions it calls, then watches Crash and the contact object: every time he settles on a
        //  different kind of floor it notes their values, and at the end lists the ones that follow the floor kind
        //  (one value on snow, another on ice). One of them is what the game compares.
        void code_dump(Address begin, Address end, const char *why)
        {
            std::ostringstream code;
            code << "Code: " << why << " +0x" << std::hex << (begin - base()) << "..+0x" << (end - base()) << "\n";
            for (Address at = begin; at < end; at += 64)
            {
                std::array<unsigned char, 64> bytes{};
                const auto n = static_cast<unsigned>(std::min<Address>(64, end - at));
                SIZE_T got = 0;
                ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(at), bytes.data(), n, &got);
                code << "Code: +0x" << (at - base()) << " ";
                for (unsigned b = 0; b < got; ++b)
                    code << std::setw(2) << std::setfill('0') << unsigned(bytes[b]);
                code << "\n";
            }
            std::cout << code.str() << std::flush;
        }
        // a function and, one level down, every function it calls directly
        void code_dump_tree(Address begin, Address end, std::set<Address> &done)
        {
            if (!done.insert(begin).second)
                return;
            code_dump(begin, end, "function");
            std::vector<unsigned char> bytes(end - begin);
            SIZE_T got = 0;
            ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(begin), bytes.data(), bytes.size(), &got);
            for (std::size_t i = 0; i + 5 <= got; ++i)
                if (bytes[i] == 0xe8)
                {
                    std::int32_t rel = 0;
                    std::memcpy(&rel, &bytes[i + 1], 4);
                    const Address target = begin + i + 5 + rel;
                    if (target < base() + 0x1000 || target >= module_end() || done.count(target))
                        continue;
                    DWORD64 image = 0;
                    const auto entry = RtlLookupFunctionEntry(target, &image, nullptr);
                    if (!entry || image + entry->BeginAddress != target)
                        continue; // not the start of a function: a false match inside an instruction
                    done.insert(target);
                    code_dump(target, std::min<Address>(image + entry->EndAddress, target + 0x200), "callee");
                }
        }
        Address find_typed_near(Address actor, Address rva)
        {
            std::deque<std::pair<Address, int>> queue{{actor, 0}};
            std::set<Address> seen{actor};
            while (!queue.empty() && seen.size() < 4000)
            {
                const auto [at, depth] = queue.front();
                queue.pop_front();
                std::vector<std::uint64_t> words((depth ? 0x400 : 0x1000) / 8);
                if (!read(at, words))
                    continue;
                for (const auto w : words)
                {
                    const auto p = static_cast<Address>(w);
                    if (p < 0x10000 || p > 0x7ffffffeffffULL || (p & 7) || (p >= base() && p < module_end()))
                        continue;
                    if (typed(p, rva))
                        return p;
                    if (depth < 2 && seen.insert(p).second)
                        queue.push_back({p, depth + 1});
                }
            }
            return 0;
        }
        struct ContactWatch
        {
            bool active = false, dumped = false;
            Address actor = 0, contact = 0, dispatcher = 0, script = 0;
            Address last_piece = 0, settled_piece = 0;
            std::uint64_t changed = 0;
            std::vector<std::string> kinds;                       // the floor kind at each settle
            std::vector<std::vector<std::uint64_t>> contact_at, actor_at;
        } contact_watch;
        void contact_report()
        {
            auto &w = contact_watch;
            std::ostringstream out;
            out << "Contact: " << w.kinds.size() << " floors:";
            for (const auto &k : w.kinds)
                out << " " << k;
            out << "\n";
            const auto list = [&](const char *what, const std::vector<std::vector<std::uint64_t>> &snaps)
            {
                if (snaps.size() < 2)
                    return;
                unsigned shown = 0;
                for (std::size_t i = 0; i < snaps[0].size() && shown < 80; ++i)
                {
                    // same value on the same kind of floor every time, and different kinds differ somewhere
                    std::map<std::string, std::uint64_t> by_kind;
                    bool consistent = true, differs = false;
                    for (std::size_t s = 0; s < snaps.size() && consistent; ++s)
                    {
                        auto [it, fresh] = by_kind.emplace(w.kinds[s], snaps[s][i]);
                        if (!fresh && it->second != snaps[s][i])
                            consistent = false;
                    }
                    for (const auto &[kind, v] : by_kind)
                        differs |= v != by_kind.begin()->second;
                    if (!consistent || !differs)
                        continue;
                    ++shown;
                    out << "Contact: " << what << " +0x" << std::hex << i * 8 << ":";
                    for (const auto &[kind, v] : by_kind)
                        out << " " << kind << "=0x" << v;
                    out << std::dec << "\n";
                }
                out << "Contact: " << what << ": " << shown << " values follow the floor kind\n";
            };
            list("crash", w.actor_at);
            list("contact", w.contact_at);
            std::cout << out.str() << std::flush;
        }
        void update_contact_watch(bool on, Address actor)
        {
            auto &w = contact_watch;
            if (!on)
            {
                if (w.active)
                    contact_report();
                w.active = false;
                return;
            }
            if (!w.dumped)
            {
                w.dumped = true;
                std::set<Address> done;
                code_dump_tree(base() + 0x510b70, base() + 0x510e80, done);
                code_dump_tree(base() + 0x4dcf50, base() + 0x4dd0d0, done);
                code_dump(base() + 0x4bcb70, base() + 0x4bcc70, "before the actor update");
            }
            if (!actor)
                return;
            if (!w.active || w.actor != actor)
            {
                w = {};
                w.dumped = true, w.active = true, w.actor = actor;
                w.contact = find_typed_near(actor, 0x1509a38);
                w.dispatcher = find_typed_near(actor, 0x14cb600);
                w.script = crash_script(actor, "Scripts.Graph.Crash_IceSlide");
                std::cout << "Contact: crash 0x" << std::hex << actor << ", contact 0x" << w.contact << " (crash +0x" << (w.contact - actor)
                          << "), dispatcher 0x" << w.dispatcher << " (crash +0x" << (w.dispatcher - actor) << ")\n" << std::dec << std::flush;
                ice_note(w.contact ? "contact: watching. Walk ice -> snow -> ice -> snow (stop a second on each), then /ice_test off."
                                   : "contact: Crash's contact object wasn't found. /ice_test off and send me the log.");
                return;
            }
            if (!w.contact || !w.script)
                return;
            const auto piece = value<Address>(w.script + 0x60);
            const auto now = GetTickCount64();
            if (piece != w.last_piece)
                w.last_piece = piece, w.changed = now;
            if (piece == w.settled_piece || now - w.changed < 500 || !typed(piece, 0x13ea4d8) || w.kinds.size() >= 12)
                return;
            w.settled_piece = piece;
            std::vector<std::uint64_t> c(0x1000 / 8), a(0x2000 / 8);
            if (!read(w.contact, c))
                c.assign(c.size(), 0);
            if (!read(actor, a))
            {
                a.assign(a.size(), 0);
                a.resize(0x800 / 8);
                if (!read(actor, a))
                    a.assign(a.size(), 0);
                a.resize(0x2000 / 8, 0);
            }
            const auto kind = string(value<Address>(value<Address>(value<Address>(piece + 0x18) + 0x28) + 0x10), 24);
            w.kinds.push_back(kind.empty() ? "?" : kind);
            w.contact_at.push_back(std::move(c)), w.actor_at.push_back(std::move(a));
            ice_note("contact: on " + w.kinds.back() + " (" + std::to_string(w.kinds.size()) + "). Keep walking ice <-> snow.");
        }
        // Slippery Trap: the game's own ice. Every floor type (SnowSurface, MetalSurface, ...) has a movement kind at
        // +0x28 ("Ice", "Snow", "Metal", ...) that Crash_IceSlide checks whenever the game tells it Crash's floor
        // changed. The IceSurface, kept with every floor type in the game's CollisionMaterials (loaded in every level),
        // has the "Ice" kind. While the trap runs, each floor type Crash stands on gets the Ice kind, and Crash +0x198
        // (the floor the game compared last, in +0x510b70, run every frame from Crash's update) is cleared, so the game
        // re-checks his floor on the next frame and the ice starts at once. When the trap ends the kinds go back and
        // his floor is re-checked again, so he stops sliding at once. Without the IceSurface or Crash_IceSlide (not
        // found yet, or a character without it) the stick-momentum Slippery in ingame.cpp runs instead.
        struct IceFloor
        {
            Address kind = 0;                                  // the Ice kind
            std::vector<std::pair<Address, Address>> changed;  // floor type, its own kind
            std::uint64_t search_time = 0;
            std::string search_level;
            Address actor = 0;
            bool has_slide = false;
        } ice_floor;
        std::atomic_bool ice_floor_engaged{false};
        bool ice_kind_valid(Address kind) { return kind && typed(kind, 0x13ea438) && string(value<Address>(kind + 0x10), 16) == "Ice"; }
        bool floor_type(Address surface)
        {
            return surface && readable(surface, 0x30) && string(value<Address>(surface + 0x18), 32) == "CollisionMaterials";
        }
        Address find_ice_kind(const std::string &level)
        {
            auto &f = ice_floor;
            if (ice_kind_valid(f.kind))
                return f.kind;
            f.kind = 0;
            if (const auto s = ice_surface_seen.surface; s && readable(s, 0x30) && string(value<Address>(s + 8), 32) == "IceSurface")
                if (const auto k = value<Address>(s + 0x28); ice_kind_valid(k))
                {
                    std::cout << "Slippery Trap: the game's ice is ready\n" << std::flush;
                    return f.kind = k;
                }
            // look for it once per level, at most once a minute (a few seconds in the background)
            const auto now = GetTickCount64();
            if (!level.empty() && !ice_name_search_running && level != f.search_level && (!f.search_time || now - f.search_time > 60000))
            {
                f.search_time = now, f.search_level = level;
                ice_surface_by_name(level);
            }
            return 0;
        }
        // makes the game re-check Crash's floor on the next frame
        void kick_floor(Address actor)
        {
            if (!typed(actor, 0x1592220))
                return;
            const auto handle = value<Address>(actor + 0x198);
            if (handle && typed(resolve(handle), 0x13ea4d8))
                write(actor + 0x198, Address{0});
        }
        void restore_ice_floor(Address actor)
        {
            auto &f = ice_floor;
            if (f.changed.empty())
                return;
            for (auto it = f.changed.rbegin(); it != f.changed.rend(); ++it)
                if (floor_type(it->first) && value<Address>(it->first + 0x28) == f.kind)
                    write(it->first + 0x28, it->second);
            std::cout << "Slippery Trap: " << f.changed.size() << " floor types back to normal\n" << std::flush;
            f.changed.clear();
            kick_floor(actor);
        }
        void update_ice_floor(const Shared &state, Address actor, const std::string &level)
        {
            auto &f = ice_floor;
            const auto now = GetTickCount64();
            const auto kind = level.empty() ? 0 : find_ice_kind(level);
            // Crash_IceSlide can load a moment after Crash: look again every second until it is there
            static std::uint64_t slide_checked = 0;
            if (actor != f.actor || (!f.has_slide && now - slide_checked > 1000))
                f.actor = actor, slide_checked = now, f.has_slide = actor && crash_script(actor, "Scripts.Graph.Crash_IceSlide");
            const bool want = !level.empty() && !state.traps_paused && !(state.traps_waiting & trap_bit(TrapSlippery)) &&
                              !trap_gimmick_level(level) && timer_active(state.slippery_until, now);
            const bool can = want && kind && actor && f.has_slide;
            ice_floor_engaged = can;
            if (!can)
            {
                restore_ice_floor(actor);
                return;
            }
            // the floor type under Crash (none while he is in the air, or for a frame after a re-check)
            const auto record = resolve(value<Address>(actor + 0x198));
            if (!typed(record, 0x13ea4d8))
                return;
            const auto surface = value<Address>(record + 0x18);
            if (!floor_type(surface) || value<Address>(surface + 0x28) == kind)
                return;
            if (std::none_of(f.changed.begin(), f.changed.end(), [&](const auto &c) { return c.first == surface; }))
                f.changed.push_back({surface, value<Address>(surface + 0x28)});
            if (write(surface + 0x28, kind))
            {
                std::cout << "Slippery Trap: " << string(value<Address>(surface + 8), 48) << " is ice now\n" << std::flush;
                kick_floor(actor);
            }
        }
        void update_ice_test(unsigned mode)
        {
            static unsigned announced = 0;
            static unsigned previous = 0;
            static std::array<float, 3> expect{};
            static float mx = 0, my = 0;
            static Address push_actor = 0;
            static std::chrono::steady_clock::time_point push_last{};
            static Address slide_actor = 0;
            static float vx = 0, vy = 0;
            static std::chrono::steady_clock::time_point last{};
            const auto actor = player();
            const auto level = level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184)));
            // remember the IceSurface whenever Crash stands on it (not while name/copyall/which dress the snow up as ice)
            if (actor && mode != 7 && mode != 8 && mode != 9)
                if (const auto info = crash_floor_info(actor))
                {
                    const auto surface = value<Address>(info + 0x18);
                    if (surface && readable(surface, 16) && string(value<Address>(surface + 8), 32) == "IceSurface")
                        ice_surface_seen = {surface, value<Address>(surface), level};
                }
            if (actor)
                if (const auto info = crash_floor_info(actor))
                    if (const auto surface = value<Address>(info + 0x18);
                        surface && surface != ice_surface_seen.surface && readable(surface, 16) && string(value<Address>(surface + 8), 32) != "IceSurface")
                        ice_other_surface = surface;
            if (mode != announced)
            {
                const bool have_ice = ice_surface_seen.surface && ice_surface_seen.level == level;
                std::cout << (mode == 1   ? "Ice test: slide on. Crash's speed now follows you slowly, like on ice.\n"
                              : mode == 2 ? (have_ice ? "Ice test: floor on. The floor under Crash gets ice grip and the IceSurface.\n"
                                                      : "Ice test: floor on. The floor under Crash gets ice grip (no IceSurface yet: stand on real ice once in this level).\n")
                              : mode == 3 ? (have_ice ? "Ice test: swap on. Floors Crash steps on are switched to the IceSurface (each takes a few seconds to find).\n"
                                                      : "Ice test: swap on. No IceSurface seen in this level yet: looking for it by name.\n")
                              : mode == 4 ? "Ice test: push on. Crash's position now moves with momentum.\n"
                              : mode == 5 ? "Ice test: const on.\n"
                              : mode == 6 ? "Ice test: copy on.\n"
                              : mode == 7 ? "Ice test: name on.\n"
                              : mode == 8 ? "Ice test: copyall on.\n"
                              : mode == 9 ? "Ice test: which on.\n"
                              : mode == 10 ? "Ice test: kick on.\n"
                              : mode == 11 ? "Ice test: track on.\n"
                              : mode == 12 ? "Ice test: who on.\n"
                              : mode == 13 ? "Ice test: whopage on.\n"
                              : mode == 14 ? "Ice test: contact on.\n"
                                          : "Ice test: off\n")
                          << std::flush;
                announced = mode;
            }
            // a new level frees the old one's floors: forget them instead of writing to memory that is gone
            static std::string swap_level;
            if (level != swap_level)
            {
                std::lock_guard lock(ice_swap_mutex);
                ice_swapped.clear(), ice_swap_surfaces.clear(), swap_level = level;
            }
            update_ice_kick(mode == 10, actor, level);
            update_ice_track(mode == 11, actor, level);
            update_ice_who(mode, actor);
            update_contact_watch(mode == 14, actor);
            if (previous == 3 && mode != 3)
                ice_swap_restore();
            if (previous == 5 && mode != 5)
                ice_const_restore();
            if ((previous == 6 || previous == 7 || previous == 8) && mode != previous)
                ice_copy_restore();
            if (previous == 9 && mode != 9)
                ice_which_restore(), ice_which.active = false;
            if (mode == 9 && previous != 9)
            {
                if (!(ice_surface_seen.level == level && ice_surface_valid()) || !ice_other_surface)
                    ice_note("stand on the ice and on the snow once in this level first, then type /ice_test which again");
                else
                    ice_which_start(level);
            }
            if (mode == 9)
                update_ice_which(actor, level);
            if (mode >= 5 && mode <= 8 && previous != mode)
            {
                if (!(ice_surface_seen.level == level && ice_surface_valid()) || !ice_other_surface)
                    std::cout << "Ice test: stand on the ice and on the snow once in this level first, then try again\n" << std::flush;
                else if (mode == 5)
                    ice_const_start();
                else if (mode == 6)
                    ice_copy_start();
                else if (mode == 7)
                    ice_copy_words(8, 16, "name");
                else
                    ice_copy_words(8, 0x100, "copyall");
            }
            previous = mode;
            if (mode == 3 && actor && (ice_surface_seen.level != level || !ice_surface_valid()) && ice_name_searched_level != level)
                ice_surface_by_name(level);
            if (mode == 3 && actor && ice_surface_seen.level == level && ice_surface_valid())
                if (const auto info = crash_floor_info(actor))
                {
                    const auto surface = value<Address>(info + 0x18);
                    const auto now = GetTickCount64();
                    bool fresh = false;
                    if (surface && surface != ice_surface_seen.surface && readable(surface, 16))
                    {
                        // a floor is searched again after 5 seconds, in case the game made new references to it since
                        std::lock_guard lock(ice_swap_mutex);
                        auto &last = ice_swap_surfaces[surface ^ ice_mask];
                        if (!last || now - last > 5000)
                            fresh = true, last = now;
                    }
                    if (fresh && ice_swap_running < 2)
                        ice_swap_surface(surface ^ ice_mask, ice_surface_seen.surface ^ ice_mask, string(value<Address>(surface + 8), 48));
                    else if (fresh)
                    {
                        std::lock_guard lock(ice_swap_mutex);
                        ice_swap_surfaces[surface ^ ice_mask] = 0; // try again once a search finishes
                    }
                    // the floor he is on right now, too (remembered so turning it off puts it back at once)
                    if (surface && surface != ice_surface_seen.surface && write(info + 0x18, ice_surface_seen.surface))
                    {
                        std::lock_guard lock(ice_swap_mutex);
                        ice_swapped.push_back({info + 0x18, surface ^ ice_mask});
                    }
                }
            if (mode == 4 && actor)
            {
                std::array<float, 3> p{};
                if (!read(actor + 32, p) || !std::isfinite(p[0]) || !std::isfinite(p[1]))
                    return;
                const auto now = std::chrono::steady_clock::now();
                const float dt = std::chrono::duration<float>(now - push_last).count();
                push_last = now;
                if (actor != push_actor || dt > 0.25f || dt <= 0 || std::hypot(p[0] - expect[0], p[1] - expect[1]) > 300)
                {
                    push_actor = actor, mx = my = 0, expect = p;
                    return;
                }
                // how the game moved him this frame, from where we put him
                const float gx = (p[0] - expect[0]) / dt, gy = (p[1] - expect[1]) / dt;
                const float dx = gx - mx, dy = gy - my, gap = std::hypot(dx, dy), step = ice_test_accel * dt;
                if (gap <= step)
                    mx = gx, my = gy;
                else
                    mx += dx / gap * step, my += dy / gap * step;
                // does the game keep the position we write? Whenever our position differed from the game's own by
                // over 20 units, see which one he moved on from
                static unsigned kept = 0, undone = 0;
                static Address reported_for = 0;
                static std::array<float, 2> game_own{};
                static bool compare = false;
                if (compare && push_actor != reported_for)
                {
                    const float from_ours = std::hypot(p[0] - expect[0], p[1] - expect[1]);
                    const float from_game = std::hypot(p[0] - game_own[0], p[1] - game_own[1]);
                    (from_ours < from_game ? kept : undone)++;
                    if (kept + undone >= 40)
                    {
                        std::cout << "Ice test: push: the game kept " << kept << " of 40 position changes\n" << std::flush;
                        reported_for = push_actor, kept = undone = 0;
                    }
                }
                const std::array<float, 2> moved{expect[0] + mx * dt, expect[1] + my * dt};
                game_own = {p[0], p[1]};
                compare = std::hypot(moved[0] - p[0], moved[1] - p[1]) > 20;
                write(actor + 32, moved);
                expect = {moved[0], moved[1], p[2]};
                return;
            }
            if (mode != 4)
                push_actor = 0;
            if (mode >= 3)
                return;
            if (mode != 1 || !actor)
                slide_actor = 0;
            if (!actor || !mode)
                return;
            if (mode == 1)
            {
                std::array<float, 3> v{};
                if (!read(actor + 0x60, v) || !std::isfinite(v[0]) || !std::isfinite(v[1]))
                    return;
                const auto now = std::chrono::steady_clock::now();
                const float dt = std::chrono::duration<float>(now - last).count();
                last = now;
                if (actor != slide_actor || dt > 0.25f)
                {
                    slide_actor = actor, vx = v[0], vy = v[1];
                    return;
                }
                const float dx = v[0] - vx, dy = v[1] - vy, gap = std::hypot(dx, dy), step = ice_test_accel * dt;
                if (gap <= step)
                    vx = v[0], vy = v[1];
                else
                    vx += dx / gap * step, vy += dy / gap * step;
                write(actor + 0x60, std::array<float, 2>{vx, vy});
                return;
            }
            const auto info = crash_floor_info(actor);
            if (!info)
                return;
            const std::array<float, 2> ice_grip{0.05f, 0.05f};
            if (value<std::array<float, 2>>(info + 0x20) != ice_grip)
                write(info + 0x20, ice_grip);
            if (ice_surface_seen.level == level && ice_surface_valid() && value<Address>(info + 0x18) != ice_surface_seen.surface)
                write(info + 0x18, ice_surface_seen.surface);
        }
        // /ice_find (research): searches all of the game's memory, on a background thread, for references to the
        // IceSurface asset (seen when Crash stood on ice) and to the surface Crash stands on now, and says what kind
        // of object holds each one. The ice script decides "ice" by comparing the floor's surface with a reference it
        // keeps; pointing that reference at the floor Crash stands on would switch the game's own ice on anywhere.
        std::atomic_bool ice_find_running{false};
        void ice_find()
        {
            if (ice_find_running.exchange(true))
            {
                std::cout << "Ice find: a search is already running\n" << std::flush;
                return;
            }
            struct Target
            {
                Address value;
                std::string name;
            };
            std::vector<Target> targets;
            const auto actor = player();
            const auto level = level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184)));
            if (ice_surface_seen.surface && ice_surface_seen.level == level)
                targets.push_back({ice_surface_seen.surface, "IceSurface"});
            if (const auto info = actor ? crash_floor_info(actor) : 0)
                if (const auto surface = value<Address>(info + 0x18); surface && surface != ice_surface_seen.surface)
                    targets.push_back({surface, "floor now (" + string(value<Address>(surface + 8), 48) + ")"});
            const auto slide = actor ? crash_script(actor, "Scripts.Graph.Crash_IceSlide") : 0;
            const Address slide_end = slide ? slide + value<std::uint16_t>(value<Address>(slide + 56) + 90) : 0;
            const auto slide_data = slide ? value<Address>(slide + 0x10) : 0, slide_meta = slide ? value<Address>(slide + 56) : 0;
            // a level without ice seen: also look for the IceSurface by its name
            if (!(ice_surface_seen.level == level && ice_surface_valid()))
                ice_surface_by_name(level);
            if (targets.empty())
            {
                std::cout << "Ice find: stand on real ice once in this level (or stand anywhere in a level), then try again\n" << std::flush;
                ice_find_running = false;
                return;
            }
            std::cout << "Ice find: searching all game memory in the background (a few seconds) for " << targets.size()
                      << " surfaces. Crash_IceSlide is at 0x" << std::hex << slide << ", its data 0x" << slide_data << ", its graph 0x"
                      << slide_meta << std::dec << "\n" << std::flush;
            std::thread([this_targets = targets, slide, slide_end, slide_data, slide_meta]
                        {
                const auto lo = base(), hi = module_end();
                std::vector<std::vector<Address>> hits(this_targets.size());
                std::vector<std::uint64_t> chunk(1 << 17); // 1 MB
                std::uint64_t scanned = 0;
                MEMORY_BASIC_INFORMATION info{};
                for (Address at = 0x10000; at < 0x7ffffffe0000ULL && VirtualQuery(reinterpret_cast<void *>(at), &info, sizeof(info));
                     at = reinterpret_cast<Address>(info.BaseAddress) + info.RegionSize)
                {
                    if (info.State != MEM_COMMIT || info.Type != MEM_PRIVATE || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
                        !(info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY)))
                        continue;
                    const Address start = reinterpret_cast<Address>(info.BaseAddress), end = start + info.RegionSize;
                    for (Address p = start; p < end; p += chunk.size() * 8)
                    {
                        const auto bytes = std::min<std::uint64_t>(chunk.size() * 8, end - p);
                        SIZE_T got = 0;
                        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(p), chunk.data(), bytes, &got))
                            continue;
                        scanned += got;
                        for (std::size_t i = 0; i < got / 8; ++i)
                            for (std::size_t t = 0; t < this_targets.size(); ++t)
                                if (chunk[i] == this_targets[t].value && hits[t].size() < 4000)
                                    hits[t].push_back(p + i * 8);
                    }
                }
                std::ostringstream out;
                out << "Ice find: searched " << scanned / (1024 * 1024) << " MB\n";
                for (std::size_t t = 0; t < this_targets.size(); ++t)
                {
                    out << "Ice find: " << this_targets[t].name << " (0x" << std::hex << this_targets[t].value << std::dec << ") is referenced in "
                        << hits[t].size() << " places\n";
                    std::map<std::string, unsigned> kinds;
                    unsigned listed = 0;
                    for (const auto at : hits[t])
                    {
                        // the object holding it: the nearest type word up to 1 KB before
                        Address object = 0, type = 0;
                        for (Address q = at; q + 1024 >= at && q >= 0x10000; q -= 8)
                        {
                            const auto v = value<Address>(q);
                            if (v >= lo && v < hi)
                            {
                                object = q, type = v - lo;
                                break;
                            }
                            if (q == at - 1024)
                                break;
                        }
                        std::ostringstream kind;
                        kind << (object ? "type +0x" : "no type") << std::hex << (object ? type : 0);
                        const bool special = (slide && at >= slide && at < slide_end) || (slide_data && at >= slide_data && at < slide_data + 0x400) ||
                                             (slide_meta && at >= slide_meta && at < slide_meta + 0x400);
                        if (++kinds[kind.str()] <= 2 || special)
                        {
                            if (listed++ < 120)
                            {
                                out << "Ice find:   0x" << std::hex << at << std::dec;
                                if (object)
                                    out << " = " << kind.str() << " +0x" << std::hex << (at - object) << std::dec;
                                if (slide && at >= slide && at < slide_end)
                                    out << "  [in Crash_IceSlide +0x" << std::hex << (at - slide) << std::dec << "]";
                                if (slide_data && at >= slide_data && at < slide_data + 0x400)
                                    out << "  [in Crash_IceSlide's data +0x" << std::hex << (at - slide_data) << std::dec << "]";
                                if (slide_meta && at >= slide_meta && at < slide_meta + 0x400)
                                    out << "  [in Crash_IceSlide's graph +0x" << std::hex << (at - slide_meta) << std::dec << "]";
                                out << "\n";
                            }
                        }
                    }
                    for (const auto &[kind, count] : kinds)
                        out << "Ice find:   " << count << " in objects of " << kind << "\n";
                }
                std::cout << out.str() << std::flush;
                ice_find_running = false; })
                .detach();
        }
        void update_c3_buttons(Shared &state, const std::string &zone)
        {
            static std::uint64_t next = 0, pending_triggers = 0;
            // the secret platform is requested once per hub visit. (V1.2.24 asked again while it wasn't moving, which
            // made a second pad after a ride and left the next ride on a black screen)
            static std::set<std::pair<Address, Address>> platform_spawn_attempts;
            if (!state.c3_buttons_ready || hub_campaign(zone) != 3)
            {
                pending_triggers = 0;
                platform_spawn_attempts.clear();
                return;
            }
            if (GetTickCount64() < next)
                return;
            next = GetTickCount64() + 250;
            const auto refresh_triggers = pending_triggers;
            pending_triggers = 0;
            if (!code_matches(0xc988c0, std::array<unsigned char, 16>{0x4c, 0x8b, 0xdc, 0x49, 0x89, 0x6b, 0x18, 0x56,
                                                                      0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x70}))
                throw std::runtime_error("Button script-method executor differs");
            unsigned refreshed = 0, arranged = 0, trigger_refreshes = 0;
            // resolve current owners on this thread. never queue spawned pointers across a level load
            for (const auto entity : event_owners())
            {
                const auto name = string(value<Address>(entity + 128));
                if (name == "Generic_Path_Platform_Start_FadeOut" || name == "Generic_Path_Platform_End_FadeIn")
                {
                    const auto components = list(value<Address>(entity + 24), 64);
                    const auto offset = name == "Generic_Path_Platform_Start_FadeOut" ? 372300u : 365140u;
                    const auto cap = entity >= offset ? entity - offset + 364580 : 0;
                    const bool unlocked = typed(cap, 0x14c8ef8) && string(value<Address>(cap + 128)) == "FloorCap" &&
                                          !(value<unsigned>(cap + 56) & 0x100);
                    // enabling a previously disabled owner can leave its spawner empty after hub reload
                    const bool moving = std::any_of(components.begin(), components.end(), [](Address c)
                                                    { return typed(c, 0x13bbbd0) && string(value<Address>(value<Address>(c + 56) + 16)) == "Scripts.Graph.common_Path_Platform_Mover" &&
                                                             (value<unsigned char>(c + 64) || value<Address>(c + 160) || value<Address>(c + 224)); });
                    if (unlocked && !moving)
                        for (const auto component : components)
                        {
                            const auto method = c3_platform_spawn_method(component, entity);
                            if (method && platform_spawn_attempts.emplace(entity, component).second)
                            {
                                reinterpret_cast<void (*)(Address, Address, unsigned, Address)>(base() + 0xc988c0)(method, component, 0, 0);
                                std::cout << "Crash 3 secret platform spawn requested through the game script ("
                                          << name << ").\n" << std::flush;
                            }
                        }
                }
                const auto index = c3_button_index(name);
                if (index < 0)
                    continue;
                const unsigned char desired = (state.c3_buttons_active >> index) & 1;
                const auto components = list(value<Address>(entity + 24), 64);
                bool moved = false, visibility_changed = false;
                for (const auto component : components)
                {
                    const auto method = c3_button_refresh_method(component, entity);
                    if (!method)
                        continue;
                    const auto target = c3_layout_target(component), layout_method = c3_button_refresh_method(component, entity, true);
                    std::array<float, 3> position{}, destination{};
                    if (target && layout_method && read(entity + 32, position) && read(target + 32, destination) && position != destination &&
                        std::all_of(destination.begin(), destination.end(), [](float f)
                                    { return std::isfinite(f); }) &&
                        std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2]) &&
                        std::abs(position[0] - destination[0]) < 1024 && std::abs(position[1] - destination[1]) < 1024 && std::abs(position[2] - destination[2]) < 1024)
                    {
                        reinterpret_cast<void (*)(Address, Address, unsigned, Address)>(base() + 0xc988c0)(layout_method, component, 0, 0);
                        ++arranged;
                        moved = true;
                    }
                    if (read(entity + 32, position))
                        for (unsigned i = 0; i < state.entrance_count; ++i)
                            if (state.entrances[i].c3_button == index)
                            {
                                std::memcpy(state.entrances[i].position, position.data(), 12);
                                break;
                            }
                    const auto enabled = value<unsigned char>(component + 128), shown = value<unsigned char>(component + 88);
                    if (enabled > 1 || shown > 1)
                        throw std::runtime_error("Invalid button visibility state");
                    if (enabled == desired && shown == desired)
                        continue;
                    reinterpret_cast<void (*)(Address, Address, unsigned, Address)>(base() + 0xc988c0)(method, component, 0, 0);
                    ++refreshed;
                    visibility_changed = true;
                }
                const auto bit = std::uint64_t(1) << index;
                // show starts the trigger later. retain only its catalogue bit until registration settles.
                if (desired && (moved || visibility_changed || (refresh_triggers & bit)))
                {
                    bool applied = false;
                    if (!moved && !visibility_changed)
                        for (const auto component : components)
                            if (c3_static_trigger(component, entity))
                            {
                                if (!code_matches(0x5912a0, std::array<unsigned char, 16>{0x40, 0x53, 0x48, 0x81, 0xec, 0xc0, 0, 0,
                                                                                          0, 0x80, 0xb9, 0x70, 0x06, 0, 0, 0}))
                                    throw std::runtime_error("Game trigger-refresh function differs");
                                reinterpret_cast<void (*)(Address)>(base() + 0x5912a0)(component);
                                ++trigger_refreshes;
                                applied = true;
                            }
                    if (!applied)
                        pending_triggers |= bit;
                }
            }
            if (refreshed)
                std::cout << "Crash 3 button visibility refreshed: " << refreshed << ".\n"
                          << std::flush;
            if (arranged)
                std::cout << "Crash 3 six-button layout applied: " << arranged << ".\n"
                          << std::flush;
            if (trigger_refreshes)
                std::cout << "Crash 3 moved trigger bindings refreshed: " << trigger_refreshes << ".\n"
                          << std::flush;
        }

    }
    std::uintptr_t crate_identity_owner(std::uintptr_t owner, const char **name)
    {
        if (!owner || !name)
            return 0;
        if (!flying_crate_actor(owner))
            return owner;
        const auto world = value<Address>(base() + 0x1a64690);
        const auto key = level_key(string(value<Address>(world + 184)));
        const BoxSpec *spec = nullptr;
        const auto root = flying_root(owner, prefab_instances(), key, spec);
        if (root)
            *name = spec->name;
        return root ? root : owner;
    }
    void inherit_license()
    {
        if (!code_matches(0x4fdb50, std::array<unsigned char, 20>{0x48, 0x8b, 0xc4, 0x55, 0x56, 0x41, 0x55, 0x41, 0x57,
                                                                  0x48, 0x8d, 0x68, 0xb8, 0x48, 0x81, 0xec, 0x28, 0x01, 0x00, 0x00}))
            throw std::runtime_error("Game license-acceptance setter differs");
        // installed while startup waits at the save gate
        license_hook.attach(0x7550c0, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20},
                            reinterpret_cast<Address>(license_query));
    }
    void preserve_shared_ui_audio()
    {
        // the common resource loader serves both sound banks and their samples. installed before the first hub loads.
        ui_audio_hook.attach(0x307ee0, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x55, 0x57, 0x41, 0x56},
                             reinterpret_cast<Address>(load_ui_audio));
    }
    void configure_hub_access(const std::array<unsigned, 3> &areas)
    {
        if ((areas[0] & ~7u) || (areas[1] & ~31u) || (areas[2] & ~31u))
            throw std::runtime_error("Invalid hub area permissions");
        hub_areas.store(areas[0] | (areas[1] << 5) | (areas[2] << 10), std::memory_order_relaxed);
        // copy each complete leaf function, including its internal short branches and returns.
        hub_bool_hook.attach(0xc9c130, {0x48, 0x8b, 0x41, 0x18, 0x48, 0x85, 0xc0, 0x74, 0x11, 0x48, 0x8b, 0x0a,
                                      0x48, 0x85, 0xc9, 0x74, 0x09, 0x0f, 0xb7, 0x40, 0x18, 0x0f, 0xb6, 0x04,
                                      0x08, 0xc3, 0x32, 0xc0, 0xc3}, reinterpret_cast<Address>(hub_bool_field));
        hub_int_hook.attach(0xc9e820, {0x4c, 0x8b, 0x02, 0x4d, 0x85, 0xc0, 0x74, 0x12, 0x48, 0x8b, 0x41, 0x18,
                                     0x48, 0x85, 0xc0, 0x74, 0x09, 0x0f, 0xb7, 0x40, 0x18, 0x42, 0x8b, 0x04,
                                     0x00, 0xc3, 0x33, 0xc0, 0xc3}, reinterpret_cast<Address>(hub_int_field));
        if (!hub_gate_query_hook.installed &&
            !code_matches(0xcb6db0, std::array<unsigned char, 10>{0x48, 0x85, 0xd2, 0x74, 0x36, 0x48, 0x89, 0x5c, 0x24, 8}))
            throw std::runtime_error("Hub graph dispatcher differs from the verified build");
        hub_gate_query_hook.attach(0x831da0, {0x48, 0x89, 0x5c, 0x24, 8, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
                                            0x48, 0x83, 0xec, 0x20}, reinterpret_cast<Address>(hub_gate_query));
    }
    void configure_movement(const std::map<std::string, unsigned> &styles, bool shuffle_moves)
    {
        // with no Crashsanity style the hooks keep each campaign's own controls, so Ability Shuffle can share them
        if (!styles.empty() || shuffle_moves)
        {
            // installed at the startup save gate, before the first player runs its graph's OnStart
            if (!code_matches(0xcb6db0, std::array<unsigned char, 10>{0x48, 0x85, 0xd2, 0x74, 0x36, 0x48, 0x89, 0x5c, 0x24, 0x08}))
                throw std::runtime_error("Movement graph dispatcher differs from the verified build");
            if (!code_matches(0x47cfa0, std::array<unsigned char, 15>{0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20}))
                throw std::runtime_error("Movement attack event lookup differs from the verified build");
            if (!movement_input_code)
            {
                if (!code_matches(0x4c06ee, std::array<unsigned char, 4>{0x0f, 0x92, 0xc0, 0xc3}))
                    throw std::runtime_error("Movement input getter differs from the verified build");
                const auto code = movement_input_stub(reinterpret_cast<Address>(&blocked_crouch_controller));
                const auto memory = VirtualAlloc(nullptr, code.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                if (!memory)
                    throw std::runtime_error("Cannot allocate movement input guard");
                std::memcpy(memory, code.data(), code.size());
                DWORD before = 0;
                if (!VirtualProtect(memory, code.size(), PAGE_EXECUTE_READ, &before))
                {
                    VirtualFree(memory, 0, MEM_RELEASE);
                    throw std::runtime_error("Cannot protect movement input guard");
                }
                FlushInstructionCache(GetCurrentProcess(), memory, code.size());
                movement_input_code = reinterpret_cast<Address>(memory);
            }
            movement_input_hook.attach(0x4c06e0, {0x48, 0x8b, 0x41, 0x10, 0x8b, 0xca, 0x48, 0x8b, 0x40, 0x10, 0x48, 0x0f, 0xa3, 0xc8},
                                       movement_input_code);
            // the attack selector returns 0..3 or 4 for no attack. walking slide uses TertiaryFadeIn.
            movement_attack_hook.attach(0x483670, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20},
                                        reinterpret_cast<Address>(movement_attack));
            // CSkillUpgradeFilter resolves its skill handle at +24 and applies the normal/inverted mode at +16
            movement_skill_hook.attach(0x4e9f60, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18},
                                       reinterpret_cast<Address>(movement_skill));
            movement_check_hook.attach(0x82f220, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20},
                                       reinterpret_cast<Address>(movement_check));
        }
        std::lock_guard lock(movement_mutex);
        movement_styles = styles;
    }
    std::vector<LiveBox> live_boxes(const Shared &state, const std::string &zone)
    {
        if (!marker_bounds(state.marker_mode))
            return {};
        std::lock_guard lock(crate_mutex);
        std::vector<LiveBox> result;
        for (const auto &c : crates(state, zone))
        {
            LiveBox box;
            box.color = c.identity.color;
            if (!crate_corners(c.model, box))
                continue;
            result.push_back(box);
        }
        return result;
    }
    // Crash's render model: his model component (+176, type +0x150df30, not a crate's) keeps it at +40 (V1.2.1 probe)
    Address crash_render_model(Address actor)
    {
        const auto component = actor ? value<Address>(actor + 176) : 0;
        const auto model = component ? value<Address>(component + 40) : 0;
        return typed(model, 0x1676cb8) ? model : 0;
    }
    // every name an object holds within its first 0x100 bytes (pointers to readable text)
    std::string names_in(Address object)
    {
        std::string result;
        for (unsigned off = 0; off < 0x100; off += 8)
        {
            const auto p = value<Address>(object + off);
            if (p < 0x10000 || p > 0x7ffffffeffffULL || !readable(p, 4))
                continue;
            const auto text = string(p, 64);
            if (text.size() >= 3 && std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 32 && c < 127; }))
                result += " " + hex(off) + "=\"" + text + "\"";
        }
        return result;
    }
    // /crash_probe part two: the six "Crash_mesh" pieces under his render model (+0xc0, then +0xd0..+0xf8), each with its
    // first words and every object it points to (and those objects' own pointers) that holds a name, to tell which
    // piece is his fur, pants, shoes, laces or mohawk and where each piece's color settings are
    void crash_parts_probe(Address model)
    {
        const auto parts = value<Address>(model + 0xc0);
        if (!readable(parts, 0x100))
            return;
        for (unsigned i = 0; i < 6; ++i)
        {
            const auto piece = value<Address>(parts + 0xd0 + i * 8);
            if (!readable(piece, 0x100))
                continue;
            std::ostringstream words;
            for (unsigned off = 0; off < 0x100; off += 8)
                words << " " << std::hex << value<std::uint64_t>(piece + off);
            std::cout << "Crash part " << i << ": " << describe_object(piece) << ";" << names_in(piece) << "\nCrash part " << i << " words:" << words.str()
                      << std::dec << "\n";
            std::set<Address> seen{piece};
            for (unsigned off = 0; off < 0x100; off += 8)
            {
                const auto p = value<Address>(piece + off);
                if (p < 0x10000 || p > 0x7ffffffeffffULL || !readable(p, 0x100) || !seen.insert(p).second)
                    continue;
                const auto t = value<Address>(p);
                if (t < base() || t >= module_end())
                    continue;
                std::cout << "Crash part " << i << " " << hex(off) << ": type " << hex(static_cast<unsigned>(t - base())) << names_in(p) << "\n";
                for (unsigned off2 = 0; off2 < 0x100; off2 += 8)
                {
                    const auto q = value<Address>(p + off2);
                    if (q < 0x10000 || q > 0x7ffffffeffffULL || !readable(q, 0x100) || !seen.insert(q).second)
                        continue;
                    const auto t2 = value<Address>(q);
                    if (t2 < base() || t2 >= module_end())
                        continue;
                    const auto names = names_in(q);
                    if (!names.empty())
                        std::cout << "Crash part " << i << " " << hex(off) << hex(off2) << ": type " << hex(static_cast<unsigned>(t2 - base())) << names << "\n";
                }
            }
        }
        std::cout << std::flush;
    }
    // Crash's colors, research. /crash_probe: from Crash's model component (+176) and render model, follow pointers to
    // game objects (5 levels, at most 500 objects) and log each with the names it holds, to find his model's parts
    // (fur, pants, shoes, laces, the mohawk) and what color settings each has. Read-only.
    void crash_probe()
    {
        const auto actor = player();
        if (!actor)
        {
            std::cout << "Crash probe: Crash not found. Try it inside a level.\n" << std::flush;
            return;
        }
        const auto component = value<Address>(actor + 176), model = crash_render_model(actor);
        std::cout << "Crash probe: Crash " << describe_object(actor) << "; +176 " << describe_object(component) << "; render model "
                  << (model ? describe_object(model) : std::string("not found")) << "\n" << std::flush;
        struct Node
        {
            Address at;
            std::string path;
            unsigned depth;
        };
        std::deque<Node> queue{{model ? model : component, model ? "model" : "component", 0}};
        if (model)
            queue.push_back({component, "component", 0});
        std::set<Address> seen{actor};
        unsigned shown = 0;
        while (!queue.empty() && shown < 500)
        {
            const auto n = queue.front();
            queue.pop_front();
            if (!n.at || !seen.insert(n.at).second || !readable(n.at, 0x100))
                continue;
            const auto type = value<Address>(n.at);
            if (type < base() || type >= module_end())
                continue;
            ++shown;
            std::cout << "Crash probe: " << n.path << " = " << describe_object(n.at) << "\n";
            if (n.depth >= 5)
                continue;
            // a list: count at +16, its entries at +32
            const auto count = value<unsigned>(n.at + 16);
            const auto entries = value<Address>(n.at + 32);
            if (count && count <= 128 && readable(entries, count * 8))
                for (unsigned i = 0; i < count; ++i)
                    queue.push_back({value<Address>(entries + i * 8), n.path + "[" + std::to_string(i) + "]", n.depth + 1});
            for (unsigned off = 8; off < 0x100; off += 8)
            {
                const auto p = value<Address>(n.at + off);
                if (p > 0x10000 && p < 0x7ffffffeffffULL && !seen.count(p) && readable(p, 16))
                    if (const auto t = value<Address>(p); t >= base() && t < module_end())
                        queue.push_back({p, n.path + hex(off), n.depth + 1});
            }
        }
        std::cout << "Crash probe: " << shown << " objects listed\n" << std::flush;
        if (model)
            crash_parts_probe(model);
    }
    // /crash_tint: the crate markers' tint (a CFxTintBundle) on Crash's whole model, blending his colors toward one.
    // Put back on his new model after a respawn or a new level; removed with amount 0.
    struct CrashTint
    {
        Address model = 0, bundle = 0, handle = 0;
        std::array<float, 4> blend{};
    } crash_tint_state;
    void update_crash_tint(const Shared &state)
    {
        auto &t = crash_tint_state;
        const std::array<float, 4> want{state.crash_tint[0], state.crash_tint[1], state.crash_tint[2], state.crash_tint[3]};
        const bool on = want[3] > 0;
        const auto actor = player();
        const auto model = crash_render_model(actor);
        if (t.bundle && (!on || model != t.model || resolve(t.handle) != t.model))
        {
            if (resolve(t.handle) == t.model && typed(t.model, 0x1676cb8))
            {
                const auto bundles = list(value<Address>(t.model + 224), 64);
                if (std::find(bundles.begin(), bundles.end(), t.bundle) != bundles.end())
                    reinterpret_cast<void (*)(Address, Address)>(base() + 0xaf7370)(t.model, t.bundle);
            }
            release(t.bundle);
            model_handle(t.handle, 0);
            t = {};
            if (!on)
                std::cout << "Crash tint: off\n" << std::flush;
        }
        if (!on || !model)
            return;
        if (!t.bundle)
        {
            std::lock_guard lock(crate_mutex);
            if (!tint_api())
                return;
            const auto bundle = reinterpret_cast<Address (*)(Address)>(base() + 0x882b0)(0);
            if (!typed(bundle, 0x138da80))
                return;
            retain(bundle);
            Address handle = 0;
            model_handle(handle, model);
            if (resolve(handle) != model || !write(bundle + 32, default_tint) || !write(bundle + 48, want))
            {
                model_handle(handle, 0);
                release(bundle);
                return;
            }
            reinterpret_cast<void (*)(Address, Address)>(base() + 0xacd1c0)(model, bundle);
            t = {model, bundle, handle, want};
            std::cout << "Crash tint: on (" << want[0] << ", " << want[1] << ", " << want[2] << " at " << static_cast<int>(want[3] * 100 + 0.5f)
                      << "%) on " << describe_object(model) << "\n" << std::flush;
        }
        else if (t.blend != want && write(t.bundle + 48, want))
            t.blend = want;
    }
    void update_gameplay(Shared &state)
    {
        const auto marker_world = value<Address>(base() + 0x1a64690);
        update_c3_buttons(state, string(value<Address>(marker_world + 184)));
        update_collectibles(state, string(value<Address>(marker_world + 184)));
        update_tints(state, string(value<Address>(marker_world + 184)));
        const auto actor = player();
        {
            const bool in_level = !level_key(string(value<Address>(marker_world + 184))).empty();
            const bool camera = scene().camera_active;
            state.player_ready = in_level && (actor ? player_can_move(actor, camera) : camera) ? 1u : 0u;
            // running traps pause every frame Crash can't move (hubs, menus, loading, the pause menu, the level's
            // intro, dying, respawning): their end times move on by that time, so they pick up again at once
            static std::uint64_t last_frame = 0;
            static bool was_paused = false;
            const auto now = GetTickCount64();
            const bool paused = !state.player_ready || state.menu_paused;
            if (state.menu_paused)
                menu_seen_at = now;
            // a trap that does nothing in this level waits for one where it works: its time stands still meanwhile
            unsigned off_here = 0;
            if (in_level && !paused)
            {
                if (gimmick_stage())
                    off_here |= trap_bit(TrapMoveLock) | trap_bit(TrapSlippery);
                if (slow_trap_exempt())
                    off_here |= trap_bit(TrapSlow);
                if (darkness_trap_exempt())
                    off_here |= trap_bit(TrapDarkness);
            }
            if (last_frame && now > last_frame)
            {
                // the game didn't run for a while (the pause menu, a load): the client moved the trap times on while it
                // waited, up to stall_accounted; the rest of the gap is added here. Otherwise a frame Crash couldn't move
                // in moves them on by that frame.
                const bool stalled = now - last_frame > 250;
                const auto from = stalled ? std::max(last_frame, state.stall_accounted) : last_frame;
                if (stalled || paused || was_paused)
                {
                    if (now > from)
                        for (auto *until : {&state.move_lock_until, &state.reverse_until, &state.freeze_until, &state.slippery_until,
                                            &state.slow_until, &state.darkness_until})
                            if (*until > from)
                                *until += now - from;
                }
                else
                    // Slow, Slippery and Reverse take turns: one waiting for another to end stands still too, and so does
                    // one with no effect in this level
                    for (const auto &[kind, until] : {std::pair{TrapReverse, &state.reverse_until}, std::pair{TrapSlippery, &state.slippery_until},
                                                      std::pair{TrapSlow, &state.slow_until}, std::pair{TrapMoveLock, &state.move_lock_until},
                                                      std::pair{TrapDarkness, &state.darkness_until}})
                        if (((state.traps_waiting | off_here) & trap_bit(kind)) && *until > last_frame)
                            *until += now - last_frame;
            }
            // mercy: a death takes 5 seconds off every timed trap, running or waiting; one with less left ends
            if (const auto deaths = mercy_deaths.exchange(0))
            {
                const auto cut = static_cast<std::uint64_t>(deaths) * trap_death_mercy_ms;
                unsigned shortened = 0;
                for (auto *until : {&state.move_lock_until, &state.reverse_until, &state.freeze_until, &state.slippery_until,
                                    &state.slow_until, &state.darkness_until})
                    if (*until > now)
                    {
                        *until = *until - now > cut ? *until - cut : now;
                        ++shortened;
                    }
                if (shortened)
                    std::cout << "Trap mercy: Crash died, " << shortened << " trap timer" << (shortened == 1 ? "" : "s") << " "
                              << cut / 1000 << " s shorter.\n" << std::flush;
            }
            last_frame = now, was_paused = paused;
            state.traps_paused = paused ? 1u : 0u;
            state.traps_off_here = off_here;
            state.heartbeat_game = now;
        }
        vanilla_movement = state.vanilla_controls != 0;
        move_shuffle = state.move_shuffle != 0;
        owned_moves = state.owned_moves;
        move_lock_until = state.traps_paused ? 0 : state.move_lock_until;
        if (state.nitro_probe_request != state.nitro_probe_ack)
        {
            state.nitro_probe_ack = state.nitro_probe_request;
            nitro_probe();
        }
        update_nitro_trap(state, level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184))));
        if (state.aku_give_request != state.aku_give_ack)
        {
            state.aku_give_ack = state.aku_give_request;
            aku_give();
        }
        aku_give_followup();
        if (!level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184))).empty())
            update_aku_lock(state.aku_lock != 0);
        // Crash's colors (research), in levels and hubs
        update_crash_tint(state);
        if (state.crash_probe_request != state.crash_probe_ack)
        {
            state.crash_probe_ack = state.crash_probe_request;
            crash_probe();
        }
        update_ice_watch(state.ice_watch != 0);
        update_move_probe(state.move_probe != 0);
        update_ice_test(state.ice_test <= 14 ? state.ice_test : 0);
        update_ice_floor(state, actor, level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184))));
        if (!ice_note_text.empty())
        {
            copy(state.ice_note, ice_note_text);
            ++state.ice_note_id;
            ice_note_text.clear();
        }
        if (state.ice_find_request != state.ice_find_ack)
        {
            state.ice_find_ack = state.ice_find_request;
            ice_find();
        }
        if (state.ice_probe_request != state.ice_probe_ack)
        {
            state.ice_probe_ack = state.ice_probe_request;
            ice_probe();
        }
        if (state.nitro_scan_request != state.nitro_scan_ack)
        {
            state.nitro_scan_ack = state.nitro_scan_request;
            nitro_scan();
        }
        if (state.nitro_test_request != state.nitro_test_ack)
        {
            state.nitro_test_ack = state.nitro_test_request;
            nitro_test();
        }
        nitro_test_followup();
        const auto world = value<Address>(base() + 0x1a64690);
        const auto key = level_key(string(value<Address>(world + 184)));
        // mixed vehicle levels retain normal controls until their active form is mapped
        const bool walking = actor && typed(value<Address>(actor + 424), 0x1589128) && !special_level(key);
        const auto desired = walking && !vanilla_movement ? assigned_movement() : 0;
        // Crash 1 controls and Move Lock Traps block crouch/slide input live. Ability Shuffle without Crouch Jump
        // removes the crouch component at level start like Crash 1 does, but the button still reached the game, so a
        // crouch (a tutorial popup asks for one) could start and never end and every jump became a high jump. Block the
        // button too while neither Crouch Jump nor Slide is owned (the same button slides, so an owned Slide keeps it).
        const bool unowned_crouch = crouch_locked() && move_blocked(MoveSlide, move_shuffle, vanilla_movement, owned_moves);
        blocked_crouch_controller = walking && (movement_style(desired, scene_campaign(), false) == 1 || move_lock_active() || unowned_crouch)
                                        ? value<Address>(actor + 424) : 0;
        active_style = 0;
        state.applied_style = 0;
        if (desired && movement_check_hook.installed && movement_input_hook.installed && movement_attack_hook.installed && movement_skill_hook.installed)
        {
            bool crouch = false;
            for (const auto component : list(value<Address>(actor + 24), 128))
                if (typed(component, 0x13bbbd0) && value<Address>(component + 24) == actor &&
                    (value<unsigned>(component + 32) & 1) && !(value<unsigned>(component + 32) & 24) &&
                    string(value<Address>(value<Address>(component + 56) + 16)) == "Scripts.Graph.Crash_CrouchCrawlSlide")
                    crouch = true;
            if (desired == 1 || crouch || crouch_locked())
            {
                missing_crouch_actor = 0;
                active_style = desired;
                state.applied_style = desired;
            }
            else if (actor != missing_crouch_actor)
            {
                missing_crouch_actor = actor;
                copy(state.error, "Crashsanity waiting for crouch/slide component. Re-enter the level.");
            }
        }
        // Crash's damage scripts are bound in every level, DeathLink or not: Aku Aku Sanity's lock (its NoAkuAkuMasks
        // switch) and the Aku Aku research need them too. Deaths are only sent when DeathLink is on (the client checks)
        if (key.empty())
        {
            last_actor = 0;
            death_handle = 0;
            remote_death = false;
            state.death_ack = state.death_request;
            return;
        }
        // scripted deaths keep positive actor health. observe PlayerDied before the actor disappears
        // on foot that's Crash (or Coco); in hog, polar bear, jetpack and vehicle stages the form the player rides or flies
        const auto body = actor ? actor : controlled_actor();
        if (!body)
            return;
        bind_player_death(body);
        update_aku_watch();
        if (!code_matches(0x4df230, std::array<unsigned char, 16>{0x48, 0x89, 0x5c, 0x24, 8, 0x57, 0x48, 0x83,
                                                                  0xec, 0x20, 0x8b, 0xb9, 0xf8, 0, 0, 0}))
            throw std::runtime_error("DeathLink health getter differs");
        const auto health = reinterpret_cast<int (*)(Address)>(base() + 0x4df230)(body);
        if (health < 0 || health > 10000)
            return;
        const auto died_variable = bound_player_died();
        const bool scripted_death = died_variable && reinterpret_cast<unsigned char (*)(Address)>(base() + 0x4e5c60)(died_variable);
        const bool alive = health > 0 && !(value<unsigned char>(body + 288) & 4) && !scripted_death;
        if (!alive)
            state.death_ack = state.death_request;
        // on a ride (and for a form with no PlayerDied) a death is also its health running out: the jet ski levels
        // never set PlayerDied when Coco crashes, only the pause menu's Restart does. If PlayerDied doesn't follow
        // within 1.5 s, the health counts as the death (with the same bonus round, time trial and Restart rules)
        if (trap_gimmick_level(key) || (!died_variable && body != actor))
        {
            const auto now = GetTickCount64();
            const bool health_dead = health <= 0 || (value<unsigned char>(body + 288) & 4);
            if (body != health_actor)
                health_death_at = 0;
            else if (health_alive && health_dead)
                health_death_at = now;
            if (health_death_at && now - health_death_at > 1500)
            {
                if (now - died_flag_at.load() > 4000)
                {
                    std::cout << "DeathLink: " << string(value<Address>(body + 128), 64) << " ran out of health on a ride.\n" << std::flush;
                    record_death_change(false, true);
                }
                health_death_at = 0;
            }
            health_actor = body;
            health_alive = !health_dead;
        }
        if (remote_death && alive && GetTickCount64() > remote_deadline)
        {
            remote_death = false;
            copy(state.error, "DeathLink death was not observed after the game death script");
        }
        if (state.death_request != state.death_ack && alive && !remote_death && !state.console_open &&
            death_controls_ready(body, scene().camera_active))
        {
            remote_death = true;
            remote_deadline = GetTickCount64() + 3000;
            if (request_scripted_death(body))
                state.death_ack = state.death_request;
            else
            {
                remote_death = false;
                copy(state.error, "DeathLink waiting for the player's death script");
            }
        }
    }
    unsigned take_observed_deaths() { return observed_deaths.exchange(0); }
    bool real_ice_active() { return ice_floor_engaged.load(); }
    void restore_gameplay(bool game_thread)
    {
        std::lock_guard lock(crate_mutex);
        restore_tints(game_thread);
        active_style = 0;
        move_lock_until = 0;
        // these guards remain active because an existing actor could retain its crouch component
        vanilla_movement = true;
        const auto actor = player(), controller = value<Address>(actor + 424);
        blocked_crouch_controller = actor && typed(controller, 0x1589128) && scene_campaign() == 1 ? controller : 0;
        restore_ice_floor(actor);
        ice_floor_engaged = false;
        death_handle = 0;
        death_hook.restore();
        event_crash = 0;
        event_hook.restore();
        observed_deaths = 0;
        last_actor = 0;
        remote_death = false;
        restore_collectible_hooks();
    }
    unsigned movement_campaign(std::uintptr_t manager)
    {
        const auto desired = active_style.load();
        return desired ? desired : reinterpret_cast<unsigned (*)(Address)>(base() + 0x131400)(manager);
    }
    bool crash_position(std::array<float, 3> &at)
    {
        const auto actor = player();
        return actor && read(actor + 32, at);
    }
    bool gimmick_stage()
    {
        return trap_gimmick_level(level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184))));
    }
    bool nitro_trap_exempt()
    {
        const auto key = level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184)));
        return trap_gimmick_level(key) && !nitro_gimmick_ok(key);
    }
    bool slow_trap_exempt()
    {
        // Boulders, Boulder Dash, Crash Dash, Bone Yard, Dino Might!
        static const std::set<std::string> chases{"L104", "L113", "L205", "L304", "L311"};
        const auto key = level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184)));
        return trap_gimmick_level(key) || chases.count(key);
    }
    bool darkness_trap_exempt()
    {
        const auto key = level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184)));
        // Darkness stays on underwater (Under Pressure, Deep Trouble); Coco's jet ski levels (Makin' Waves, Tell No
        // Tales, Ski Crazed); the jet-board levels are not gimmick levels here, so it stays on there too
        static const std::set<std::string> dark{"L302", "L312"};
        return special_level(key) && !dark.count(key);
    }
    unsigned movement_double_jump(unsigned owned)
    {
        if (!owned)
            return 0;
        const auto style = active_style.load();
        const auto manager = value<Address>(base() + 0x1a64690);
        const auto campaign = style ? 0 : manager ? reinterpret_cast<unsigned (*)(Address)>(base() + 0x131400)(manager)
                                                  : 0;
        return movement_double_jump_allowed(owned, style, campaign);
    }
}
