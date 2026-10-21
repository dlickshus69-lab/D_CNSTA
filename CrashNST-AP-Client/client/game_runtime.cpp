#include "game_runtime.h"
#include "engine_memory.h"
#include "function_hook.h"
#include "movement_rules.h"
#include "trap_rules.h"
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
#include <thread>
#include <chrono>
#include <unordered_map>
#include <string_view>
#ifdef _MSC_VER
#include <intrin.h>
#endif

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
        FunctionHook death_hook;
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
        void record_death_change(bool before, bool after)
        {
            if (before == after)
                return;
            if (!after)
            {
                remote_death = false;
                return;
            }
            if (!remote_death.exchange(false))
                ++observed_deaths;
        }
        void player_died_set(Address variable, unsigned char requested)
        {
            const bool tracked = variable && variable == bound_player_died() &&
                                 !level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184))).empty();
            const auto getter = reinterpret_cast<unsigned char (*)(Address)>(base() + 0x4e5c60);
            const bool before = tracked && getter(variable);
            reinterpret_cast<void (*)(Address, unsigned char)>(death_hook.original)(variable, requested);
            if (tracked && variable == bound_player_died())
                record_death_change(before, getter(variable) != 0);
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
        bool request_scripted_death(Address actor)
        {
            if (!code_matches(0x767da0, std::array<unsigned char, 16>{0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x57, 0x48,
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
        void bind_player_death(Address actor)
        {
            if (actor == last_actor && bound_player_died())
                return;
            if (actor != last_actor)
            {
                last_actor = actor;
                remote_death = false;
            }
            death_handle = 0;
            for (const auto c : list(value<Address>(actor + 24), 128))
            {
                if (!typed(c, 0x13bbbd0) || value<Address>(c + 24) != actor || !(value<unsigned>(c + 32) & 1) ||
                    (value<unsigned>(c + 32) & 24) || !(value<unsigned char>(c + 40) & 1))
                    continue;
                const auto meta = value<Address>(c + 56), data = value<Address>(c + 16), dm = value<Address>(data + 32);
                // DamageManager's data holds the PlayerDied handle at +80 , component+56 identifies the graph
                if (!typed(meta, 0x1745d78) || string(value<Address>(meta + 16)) != "Scripts.Graph.Crash_DamageManager" ||
                    !typed(data, 0x13bbb10) || !dm || dm != value<Address>(meta + 176) || !field_at(dm, "PlayerDied", 80, 8))
                    continue;
                death_handle = value<Address>(data + 80);
                if (!bound_player_died())
                {
                    death_handle = 0;
                    continue;
                }
                if (!code_matches(0x4e5c60, std::array<unsigned char, 5>{0xe9, 0xab, 0xe9, 0xff, 0xff}))
                    throw std::runtime_error("Game PlayerDied getter differs");
                death_hook.attach(0x502000, {0x48, 0x89, 0x5c, 0x24, 8, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x40}, reinterpret_cast<Address>(player_died_set));
                return;
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
        // follow pointers from the crate's spawner, entity, models and every script on them (each script read in
        // full) up to four levels deep, and list every place holding one of the targets
        std::vector<ScanHit> position_scan(const NitroPick &pick, const std::vector<std::array<float, 3>> &targets,
                                           std::vector<unsigned> &which, unsigned &visited)
        {
            struct Node
            {
                Address at;
                std::string path;
                unsigned depth, span;
            };
            constexpr unsigned max_objects = 60000, max_depth = 4;
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
            while (!queue.empty() && visited < max_objects)
            {
                const auto node = std::move(queue.front());
                queue.pop_front();
                SIZE_T got = 0;
                unsigned size = 0;
                for (const unsigned attempt : {node.span, 512u, 64u})
                    if (attempt <= node.span && readable(node.at, attempt) && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(node.at), bytes.data(), attempt, &got) && got == attempt)
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
                    if (visited + queue.size() >= max_objects || p < 0x10000 || p > 0x7ffffffeffffULL || (p & 7) ||
                        (p >= image_start && p < image_end) || !seen.insert(p).second)
                        continue;
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
        // The spot is ahead of Crash in the direction he is going, about 230 units (1.5 crates) away: close enough to
        // be a surprise, far enough to jump it or stop. Floor he stood on earlier in this level (an earlier try, or
        // ground he came back over) comes first; otherwise it goes straight ahead along the line he is running, at
        // his ground height and following the slope he is on, and only while he is on the ground. Never on a crate.
        static constexpr float nitro_ahead = 230, nitro_near = 190, nitro_far = 290;
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
            if (!crash_position(at))
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
        bool crash_heading(float &hx, float &hy, std::size_t &end)
        {
            const auto n = crash_path.size();
            if (n < 6)
                return false;
            const auto latest = crash_path.back().time;
            for (std::size_t i = n - 1; i >= 5 && latest - crash_path[i].time <= 3000; --i)
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
        bool ahead_spot(std::array<float, 3> &spot)
        {
            float hx = 0, hy = 0;
            std::size_t end = 0;
            if (!crash_heading(hx, hy, end))
                return false;
            const auto &now = crash_path.back().at;
            std::vector<std::array<float, 3>> crates;
            for (const auto owner : event_owners())
            {
                std::array<float, 3> at{};
                if (string(value<Address>(owner + 128)).rfind("Crate_", 0) == 0 && read(owner + 32, at))
                    crates.push_back(at);
            }
            const auto clear = [&](const std::array<float, 3> &p)
            {
                return std::none_of(crates.begin(), crates.end(), [&](const std::array<float, 3> &c)
                                    { return std::hypot(c[0] - p[0], c[1] - p[1]) < 110 && std::abs(c[2] - p[2]) < 160; });
            };
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
            const auto n = crash_path.size();
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
            for (const float d : {nitro_ahead, nitro_ahead + 30, nitro_ahead - 30, nitro_ahead + 60})
            {
                const std::array<float, 3> p{now[0] + hx * d, now[1] + hy * d, now[2] + slope * d};
                if (clear(p))
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
        NitroSpawner nitro_spawner_near(const std::array<float, 3> &to)
        {
            NitroSpawner best;
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
                }
            }
            return best;
        }
        struct NitroSpawn
        {
            Address component = 0, owner = 0, old_handle = 0;
            std::vector<std::pair<Address, std::array<float, 3>>> restore; // spawner position copies
            std::uint64_t started = 0;
            std::string level, name;
        } nitro_spawn_state;
        // the Nitro a trap spawned, watched so the log says how long it lasted
        struct TrapNitro
        {
            Address entity = 0;
            std::array<float, 3> at{};
            std::uint64_t appeared = 0;
            std::string level;
        } trap_nitro;
        void watch_trap_nitro()
        {
            auto &t = trap_nitro;
            if (!t.entity)
                return;
            const auto level = level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184)));
            std::array<float, 3> at{}, crash{};
            if (level == t.level && live_crate(t.entity) && read(t.entity + 32, at) && std::hypot(at[0] - t.at[0], at[1] - t.at[1]) < 20)
                return;
            const auto lasted = (GetTickCount64() - t.appeared) / 100;
            std::cout << "Nitro Trap: its Nitro lasted " << lasted / 10 << "." << lasted % 10 << " s ("
                      << (level != t.level ? std::string("level changed")
                          : crash_position(crash) ? "Crash " + std::to_string(static_cast<int>(std::hypot(crash[0] - t.at[0], crash[1] - t.at[1]))) + " away from it"
                                                  : std::string("Crash not found"))
                      << ")\n" << std::flush;
            t = {};
        }
        void finish_nitro_spawn(const char *how)
        {
            auto &s = nitro_spawn_state;
            for (const auto &[at, original] : s.restore)
                write(at, original);
            write(s.component + 232, s.old_handle); // the spawner keeps its own crate
            std::cout << "Nitro spawn: " << how << "; " << s.name << "'s spawner is back to normal\n" << std::flush;
            s = {};
        }
        void nitro_spawn_followup();
        // starts a spawn; false with a reason when it cannot right now
        bool nitro_spawn(std::string &why)
        {
            if (nitro_spawn_state.component)
                return why = "one is still spawning", false;
            if (gimmick_stage())
                return why = "gimmick level", false;
            std::array<float, 3> crash{}, spot{};
            if (!crash_position(crash))
                return why = "Crash not found", false;
            if (!ahead_spot(spot))
                return why = "no spot ahead yet: Crash needs to be running on the ground", false;
            const auto spawner = nitro_spawner_near(spot);
            if (!spawner.component)
                return why = "no Nitro crates in this level", false;
            auto &s = nitro_spawn_state;
            s = {spawner.component, spawner.owner, value<Address>(spawner.component + 232), {}, GetTickCount64(),
                 level_key(string(value<Address>(value<Address>(base() + 0x1a64690) + 184))), spawner.name};
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
            std::cout << "Nitro spawn: " << spawner.name << "'s spawner (" << static_cast<int>(spawner.distance)
                      << " from the spot) spawns at " << triple(spot) << ", " << static_cast<int>(std::hypot(spot[0] - crash[0], spot[1] - crash[1]))
                      << " from Crash\n" << std::flush;
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
                std::cout << "Nitro spawn: a new Nitro appeared at " << triple(at) << " after " << elapsed << " ms\n";
                trap_nitro = {crate, at, GetTickCount64(), s.level};
                finish_nitro_spawn("done");
            }
            else if (elapsed > 2000)
                finish_nitro_spawn("nothing spawned within 2 seconds");
        }
        // requests from /nitro_spawn and the Nitro Trap; a request waits up to 5 seconds for a spot
        void update_nitro_trap(Shared &state, const std::string &level)
        {
            record_crash_path(level);
            nitro_spawn_followup();
            watch_trap_nitro();
            const auto now = GetTickCount64();
            static std::uint64_t next_ready = 0, give_up = 0;
            if (now >= next_ready)
            {
                next_ready = now + 500;
                std::array<float, 3> crash{}, spot{};
                state.nitro_ready = !level.empty() && !gimmick_stage() && !nitro_spawn_state.component && crash_position(crash) &&
                                    ahead_spot(spot) && nitro_spawner_near(spot).component;
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
                                std::cout << "Crash 3 secret platform spawn requested through the game script.\n"
                                          << std::flush;
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
    void update_gameplay(Shared &state)
    {
        const auto marker_world = value<Address>(base() + 0x1a64690);
        update_c3_buttons(state, string(value<Address>(marker_world + 184)));
        update_collectibles(state, string(value<Address>(marker_world + 184)));
        update_tints(state, string(value<Address>(marker_world + 184)));
        const auto actor = player();
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
        update_ice_watch(state.ice_watch != 0);
        update_move_probe(state.move_probe != 0);
        update_ice_test(state.ice_test <= 9 ? state.ice_test : 0);
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
        // Death Traps use the same scripted death, so it also runs without DeathLink
        if (key.empty() || !(state.death_link || state.trap_kills))
        {
            last_actor = 0;
            death_handle = 0;
            remote_death = false;
            state.death_ack = state.death_request;
            return;
        }
        // scripted deaths keep positive actor health. observe PlayerDied before the actor disappears
        if (!actor)
            return;
        bind_player_death(actor);
        if (!code_matches(0x4df230, std::array<unsigned char, 16>{0x48, 0x89, 0x5c, 0x24, 8, 0x57, 0x48, 0x83,
                                                                  0xec, 0x20, 0x8b, 0xb9, 0xf8, 0, 0, 0}))
            throw std::runtime_error("DeathLink health getter differs");
        const auto health = reinterpret_cast<int (*)(Address)>(base() + 0x4df230)(actor);
        if (health < 0 || health > 10000)
            return;
        const auto died_variable = bound_player_died();
        const bool scripted_death = died_variable && reinterpret_cast<unsigned char (*)(Address)>(base() + 0x4e5c60)(died_variable);
        const bool alive = health > 0 && !(value<unsigned char>(actor + 288) & 4) && !scripted_death;
        if (!alive)
            state.death_ack = state.death_request;
        if (remote_death && alive && GetTickCount64() > remote_deadline)
        {
            remote_death = false;
            copy(state.error, "DeathLink death was not observed after the game death script");
        }
        if (state.death_request != state.death_ack && alive && !remote_death && !state.console_open &&
            death_controls_ready(actor, scene().camera_active))
        {
            remote_death = true;
            remote_deadline = GetTickCount64() + 3000;
            if (request_scripted_death(actor))
                state.death_ack = state.death_request;
            else
            {
                remote_death = false;
                copy(state.error, "DeathLink waiting for the player's death script");
            }
        }
    }
    unsigned take_observed_deaths() { return observed_deaths.exchange(0); }
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
        death_handle = 0;
        death_hook.restore();
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
        // Darkness stays on underwater (Under Pressure, Deep Trouble) and on Coco's jet ski (Makin' Waves, Tell No
        // Tales, Ski Crazed); the jet-board levels are not gimmick levels here, so it stays on there too
        static const std::set<std::string> dark{"L302", "L312", "L305", "L318", "L326"};
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
