#include "game_runtime.h"
#include "engine_memory.h"
#include "function_hook.h"
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
#include <set>
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
        struct Binding
        {
            Address meta = 0, at = 0;
            unsigned original = 1, current = 1;
            std::string name;
        };
        std::vector<Binding> styles;
        void restore_styles()
        {
            for (auto &b : styles)
                if (typed(b.meta, 0x1745d78) && string(value<Address>(b.meta + 16)) == b.name &&
                    typed(b.at - 24, 0x1749820) && value<unsigned>(b.at) == b.current)
                {
                    SIZE_T done = 0;
                    WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(b.at), &b.original, 4, &done);
                    b.current = b.original;
                }
            styles.clear();
        }
        void bind_styles()
        {
            static std::uint64_t retry_after = 0;
            if (GetTickCount64() < retry_after)
                return;
            retry_after = GetTickCount64() + 500;
            const auto context = value<Address>(base() + 0x1acbe40), registry = value<Address>(context + 32);
            const auto count = value<unsigned>(registry);
            const auto bytes = value<std::uint64_t>(registry + 8) & 0xffffffffffffULL;
            const auto entries = value<Address>(registry + 16);
            if (!context || !registry || !entries || count > 65535 || bytes < count * 8 || bytes > 65536 * 8)
                return;
            for (unsigned i = 0; i < count; ++i)
            {
                const auto meta = value<Address>(entries + i * 8);
                if (!typed(meta, 0x1745d78))
                    continue;
                const auto name = string(value<Address>(meta + 16));
                unsigned offset = 0;
                std::vector<unsigned> enums;
                if (name == "Scripts.Graph.Crash_CrouchCrawlSlide")
                {
                    // these slab offsets locate campaign constants used by movement tests, not player physics fields.
                    offset = 93020;
                    enums = {92196};
                }
                if (name == "Scripts.Graph.CrashBandicoot_JumpFlip")
                {
                    offset = 43056;
                    enums = {39200, 39952};
                }
                if (!offset || meta < offset)
                    continue;
                for (const auto e : enums)
                {
                    const auto at = meta - offset + e + 24;
                    if (typed(at - 24, 0x1749820) && value<unsigned>(at) == 1)
                        styles.push_back({meta, at, 1, 1, name});
                }
            }
        }
        unsigned style_value(unsigned desired, unsigned actual, unsigned original) { return !desired ? original : desired == original ? actual
                                                                                                                                      : 0; }
        bool special_level(const std::string &key)
        {
            static const std::vector<std::string> levels{"L107", "L114", "L208", "L213", "L215", "L222", "L224", "L226",
                                                         "L302", "L303", "L305", "L308", "L310", "L312", "L314", "L317", "L318", "L322", "L324", "L326", "L328", "L330", "L331", "L332"};
            return std::find(levels.begin(), levels.end(), key) != levels.end();
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
        unsigned double_jump_allowed(unsigned owned, unsigned style, unsigned scene_campaign) { return owned && (style ? style : scene_campaign) == 3; }
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
    SceneView scene()
    {
        // resolve the live camera through its system and metadata. validate the matrix field before reading +512.
        SceneView result;
        const auto world = value<Address>(base() + 0x1a64690);
        result.zone = string(value<Address>(world + 184));
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
        // try to get pale cyan (#8FD8D8) on all box types
        constexpr std::array<float, 4> blue_tint{1, 1, 1, 1}, blue_blend{0.8f, 3.25f, 10.5f, 0.75f};
        std::array<float, 4> tint_blend(unsigned color)
        {
            if (!color)
                return blue_blend;
            return {(color & 255) * 0.8f / 143, ((color >> 8) & 255) * 3.25f / 216, ((color >> 16) & 255) * 10.5f / 216, blue_blend[3]};
        }
        constexpr std::array<float, 4> default_tint{1, 1, 1, 1}, default_blend{1, 1, 1, 0}, default_emissive{1, 0, 0, 0};
        bool marker_tint(const TintedModel &t)
        {
            return typed(t.bundle, 0x138da80) && value<std::array<float, 4>>(t.bundle + 32) == blue_tint &&
                   value<std::array<float, 4>>(t.bundle + 48) == tint_blend(t.color) && value<std::array<float, 4>>(t.bundle + 64) == default_emissive;
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
            if (!state.visible || state.marker_mode != MarkersTint || level_key(zone).empty())
            {
                restore_tints(true);
                return;
            }
            const auto targets = crates(state, zone);
            for (auto it = tinted_models.begin(); it != tinted_models.end();)
            {
                if (!has_tint(*it) || !marker_tint(*it) || std::none_of(targets.begin(), targets.end(), [&](const auto &c)
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
                    value<std::array<float, 4>>(bundle + 64) != default_emissive || !write(bundle + 32, blue_tint) || !write(bundle + 48, tint_blend(c.identity.color)))
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
                if (has_tint({c.model, bundle, c.identity.color, handle}))
                    tinted_models.push_back({c.model, bundle, c.identity.color, handle});
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
        Address c3_platform_spawn_method(Address component, Address entity)
        {
            if (!typed(component, 0x13bbbd0) || value<Address>(component + 24) != entity ||
                !(value<unsigned>(component + 32) & 1) || (value<unsigned>(component + 32) & 24) ||
                !(value<unsigned char>(component + 40) & 1) || !event_owner(entity) ||
                !(value<unsigned>(entity + 56) & 0x100))
                return 0;
            const auto meta = value<Address>(component + 56), data = value<Address>(component + 16);
            if (!typed(meta, 0x1745d78) || string(value<Address>(meta + 16)) != "Scripts.Graph.common_Spawner_Template" ||
                value<std::uint16_t>(meta + 90) != 344 || meta < 45808 || !typed(data, 0x13bbb10) ||
                value<Address>(data + 32) != value<Address>(meta + 176) || !value<Address>(data + 32) ||
                !field_at(meta, "SpawnedEntity", 232, 8) || !field_at(meta, "isSpawningEnabled", 69, 1) ||
                !field_at(meta, "used", 67, 1) || !field_at(meta, "pendingUsed", 66, 1) ||
                value<unsigned char>(component + 69) != 1 || value<unsigned char>(component + 67) ||
                value<unsigned char>(component + 66) || resolve(value<Address>(component + 232)))
                return 0;
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
                return 0;
            return method;
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
        unsigned desired = 0;
        const auto world = value<Address>(base() + 0x1a64690);
        const auto key = level_key(string(value<Address>(world + 184)));
        // mixed vehicle levels also retain vanilla controls until their active form is mapped
        const bool walking = actor && typed(value<Address>(actor + 424), 0x1589128) && !special_level(key);
        if (walking && !state.vanilla_controls && !key.empty() && key[0] == 'L')
            for (unsigned i = 0; i < state.level_count; ++i)
                if (key == state.levels[i].key)
                    desired = state.levels[i].style;
        if (desired > 3)
            desired = 0;
        active_style = 0;
        state.applied_style = 0;
        if (!desired)
        {
            restore_styles();
        }
        else
        {
            bool changed = false;
            for (const auto &b : styles)
                if (!typed(b.at - 24, 0x1749820) || !typed(b.meta, 0x1745d78) || string(value<Address>(b.meta + 16)) != b.name)
                    changed = true;
            if (changed || styles.size() < 3)
                restore_styles();
            if (styles.empty())
                bind_styles();
            const auto manager = value<Address>(base() + 0x1a64690);
            const auto actual = manager ? reinterpret_cast<unsigned (*)(Address)>(base() + 0x131400)(manager) : 0;
            if (actual >= 1 && actual <= 3 && styles.size() >= 3)
                for (auto &b : styles)
                {
                    if (value<unsigned>(b.at) != b.current)
                    {
                        state.vanilla_controls = 1;
                        active_style = 0;
                        restore_styles();
                        copy(state.error, "Crashsanity stopped: movement query changed");
                        break;
                    }
                    const auto requested = style_value(desired, actual, b.original);
                    SIZE_T done = 0;
                    if (!WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(b.at), &requested, 4, &done) || done != 4)
                        throw std::runtime_error("Cannot update movement-style query");
                    b.current = requested;
                    active_style = desired;
                    state.applied_style = desired;
                }
        }
        if (key.empty() || !state.death_link)
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
        // verify CPlayer's vtable identity before reading health or invoking KillEntity.
        if (value<Address>(base() + 0x1592220 + 0x138) != base() + 0x4eb7f0 ||
            value<Address>(base() + 0x1592220 + 0x178) != base() + 0x4c3590)
            return;
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
            copy(state.error, "DeathLink death was not observed after the game KillEntity function");
        }
        if (state.death_request != state.death_ack && alive && !remote_death && !state.console_open &&
            death_controls_ready(actor, scene().camera_active))
        {
            if (!code_matches(0x4eb7f0, std::array<unsigned char, 16>{0x48, 0x89, 0x5c, 0x24, 0x10,
                                                                      0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57}))
                throw std::runtime_error("DeathLink KillEntity function differs");
            // KillEntity builds game lethal-damage state and runs the death behavior
            remote_death = true;
            remote_deadline = GetTickCount64() + 3000;
            reinterpret_cast<void (*)(Address, unsigned char, unsigned char, unsigned char, Address)>(base() + 0x4eb7f0)(actor, 0, 0, 0, 0);
            state.death_ack = state.death_request;
        }
    }
    unsigned take_observed_deaths() { return observed_deaths.exchange(0); }
    void restore_gameplay(bool game_thread)
    {
        std::lock_guard lock(crate_mutex);
        restore_tints(game_thread);
        active_style = 0;
        restore_styles();
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
    unsigned movement_double_jump(unsigned owned)
    {
        if (!owned)
            return 0;
        const auto style = active_style.load();
        const auto manager = value<Address>(base() + 0x1a64690);
        const auto campaign = style ? 0 : manager ? reinterpret_cast<unsigned (*)(Address)>(base() + 0x131400)(manager)
                                                  : 0;
        return double_jump_allowed(owned, style, campaign);
    }
}
