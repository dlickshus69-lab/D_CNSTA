#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace ingame
{
    // trap item IDs are 0xCB0000 + 2500 + kind, matching TRAP_IDS in the apworld.
    // Death Traps are retired from new seeds; the client still applies them for older ones.
    enum TrapKind : unsigned
    {
        TrapDeath = 0,
        TrapMoveLock = 1,
        TrapReverse = 2,
        TrapFreeze = 3,
        TrapLifeLoss = 4,
        TrapSlippery = 5,
        TrapSlow = 6,
        TrapDarkness = 7,
        TrapNitro = 8,
        TrapCount = 9
    };
    inline constexpr const char *trap_names[TrapCount]{"Death Trap", "Move Lock Trap", "Reverse Controls Trap",
                                                       "Freeze Trap", "Life Loss Trap", "Slippery Trap",
                                                       "Slow Trap", "Darkness Trap", "Nitro Trap"};
    inline constexpr bool timed_trap(unsigned kind)
    {
        return kind == TrapMoveLock || kind == TrapReverse || kind == TrapSlippery || kind == TrapSlow ||
               kind == TrapDarkness;
    }
    inline constexpr std::int64_t trap_item(unsigned kind) { return 0xCB0000 + 2500 + kind; }
    inline int trap_kind(std::int64_t item)
    {
        return item >= trap_item(0) && item < trap_item(TrapCount) ? static_cast<int>(item - trap_item(0)) : -1;
    }
    inline constexpr unsigned freeze_trap_ms = 6000;
    // Nitro Trap: "NITRO INCOMING 3, 2, 1" at a TNT crate's own tempo (V1.1.73 clip, frame by frame: 3, 2 and 1 appear
    // 1.4 s, 1.3 s apart and it blows 1.4 s after the 1), then the Nitro: 3 steps in all
    inline constexpr unsigned nitro_countdown_step_ms = 1370;
    inline constexpr unsigned nitro_countdown_ms = 3 * nitro_countdown_step_ms;
    // mercy: each death takes this much off every timed trap, running or waiting its turn
    inline constexpr std::uint64_t trap_death_mercy_ms = 5000;
    // queued traps fire one at a time, this far apart
    inline constexpr unsigned trap_gap_ms = 10000;

    // a trap of the same kind that arrives while one runs (or waits its turn) adds no time: the running one keeps
    // its timer, so a trap never lasts longer than its duration
    inline std::uint64_t start_timer(std::uint64_t until, std::uint64_t now, std::uint64_t ms)
    {
        return until > now ? until : now + ms;
    }
    inline bool timer_active(std::uint64_t until, std::uint64_t now) { return now < until; }
    inline unsigned seconds_left(std::uint64_t until, std::uint64_t now)
    {
        return until > now ? static_cast<unsigned>((until - now + 999) / 1000) : 0;
    }

    // Slow, Slippery and Reverse Controls all change how the stick moves Crash, and together they ruin each other
    // (walking on ice, walking reversed), so they take turns: the one that came first runs, the others wait with their
    // timers standing still, and the next one starts when it ends. Another trap of a running or waiting kind adds
    // no time.
    inline constexpr unsigned turn_traps[]{TrapSlippery, TrapSlow, TrapReverse};
    inline constexpr unsigned turn_trap_count = sizeof(turn_traps) / sizeof(turn_traps[0]);
    inline constexpr unsigned trap_bit(unsigned kind) { return 1u << kind; }
    struct TrapTurns
    {
        unsigned order[turn_trap_count]{}; // indexes into turn_traps, first come first
        unsigned count = 0;
        // timers: the end times of turn_traps, in that order. Returns the trap_bit()s of the traps that wait.
        unsigned update(const std::uint64_t *const (&timers)[turn_trap_count], std::uint64_t now)
        {
            unsigned kept = 0;
            for (unsigned i = 0; i < count; ++i)
                if (timer_active(*timers[order[i]], now))
                    order[kept++] = order[i];
            count = kept;
            for (unsigned t = 0; t < turn_trap_count; ++t)
                if (timer_active(*timers[t], now) && std::find(order, order + count, t) == order + count)
                    order[count++] = t;
            unsigned waiting = 0;
            for (unsigned i = 1; i < count; ++i)
                waiting |= trap_bit(turn_traps[order[i]]);
            return waiting;
        }
        void clear() { count = 0; }
    };

    // Reverse Controls: left/right and forward/back swap on the stick, d-pad, arrows and WASD
    inline std::int16_t reverse_axis(std::int16_t value)
    {
        return value == INT16_MIN ? INT16_MAX : static_cast<std::int16_t>(-value);
    }
    inline std::uint16_t reverse_dpad(std::uint16_t buttons)
    {
        // XINPUT_GAMEPAD_DPAD_UP 1, DOWN 2, LEFT 4, RIGHT 8
        const unsigned up = buttons & 1u, down = buttons & 2u, left = buttons & 4u, right = buttons & 8u;
        return static_cast<std::uint16_t>((buttons & ~0xFu) | (up << 1) | (down >> 1) | (left << 1) | (right >> 1));
    }
    inline unsigned reverse_scan_code(unsigned code)
    {
        // DirectInput DIK_UP/DOWN/LEFT/RIGHT and DIK_W/S/A/D
        switch (code)
        {
        case 0xC8: return 0xD0;
        case 0xD0: return 0xC8;
        case 0xCB: return 0xCD;
        case 0xCD: return 0xCB;
        case 0x11: return 0x1F;
        case 0x1F: return 0x11;
        case 0x1E: return 0x20;
        case 0x20: return 0x1E;
        default: return code;
        }
    }
    inline int reverse_virtual_key(int key)
    {
        // VK_LEFT/UP/RIGHT/DOWN and W/A/S/D
        switch (key)
        {
        case 0x25: return 0x27;
        case 0x27: return 0x25;
        case 0x26: return 0x28;
        case 0x28: return 0x26;
        case 'W': return 'S';
        case 'S': return 'W';
        case 'A': return 'D';
        case 'D': return 'A';
        default: return key;
        }
    }
    // movement keys: DirectInput scan codes and the matching virtual keys
    inline constexpr unsigned char direction_scan_codes[]{0xC8, 0xD0, 0xCB, 0xCD, 0x11, 0x1F, 0x1E, 0x20};
    inline bool direction_virtual_key(int key)
    {
        return (key >= 0x25 && key <= 0x28) || key == 'W' || key == 'A' || key == 'S' || key == 'D';
    }

    // Slow Trap: Crash walks. Any stick push becomes a steady tilt (share of a full push) in the same direction,
    // and the d-pad becomes that same tilt on the stick. A constant tilt never crosses the game's walk/run
    // threshold, so there is no jitter. The keyboard is left alone: keys cannot be half pressed.
    inline constexpr float stick_deadzone = 7849; // XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE
    inline void walk_stick(std::int16_t &x, std::int16_t &y, std::uint16_t &buttons, float tilt)
    {
        float fx = x, fy = y, length = std::hypot(fx, fy);
        if (length <= stick_deadzone)
        {
            // stick at rest: walk in the d-pad's direction instead
            fx = static_cast<float>(((buttons & 8u) ? 1 : 0) - ((buttons & 4u) ? 1 : 0));
            fy = static_cast<float>(((buttons & 1u) ? 1 : 0) - ((buttons & 2u) ? 1 : 0));
            length = std::hypot(fx, fy);
        }
        buttons = static_cast<std::uint16_t>(buttons & ~0xFu);
        if (length == 0)
            return; // neither is pushed: leave the resting stick as it is
        const float scale = tilt * 32767 / length;
        x = static_cast<std::int16_t>(std::lround(fx * scale));
        y = static_cast<std::int16_t>(std::lround(fy * scale));
    }

    // Slow Trap tuner: the game ignores small stick tilts on the ground and runs at large ones (measured: walking
    // about 71-75% of a full push, running above ~76%), and controllers differ. While Slow is on and you push, each
    // 250 ms window of Crash's ground speed decides: running lowers the tilt at once and marks that tilt as too
    // high for the rest of the session; standing still for three windows in a row (not a turn or a bump) raises
    // it, but never back up to a tilt that ran. Walking keeps it. Steps halve when the direction flips.
    struct WalkTuner
    {
        float tilt = 0.71f, step = 0.04f;
        int last = 0;          // last change: +1 raised, -1 lowered
        float run_speed = 0;   // fastest ground speed seen without Slow, units per second
        float run_floor = 1;   // lowest tilt at which Crash ran this session
        unsigned stuck = 0;    // consecutive standing-still windows
        void restart(float start)
        {
            tilt = start, step = 0.04f, last = 0, stuck = 0, run_floor = 1;
        }
        // one window of Crash's movement; returns +1 (raised), -1 (lowered) or 0. A window in which the pushed
        // direction turned can lower the tilt (running is running) but never raises it (a turn is not standing still).
        int window(float ground_speed, float climb_speed, bool pushing, bool slowed, bool turned = false)
        {
            // idle, jumping, falling, or a respawn jump: says nothing about the stick
            if (!pushing || climb_speed > 150 || ground_speed > 4000)
            {
                stuck = 0;
                return 0;
            }
            if (!slowed)
            {
                run_speed = std::max(run_speed, ground_speed);
                return 0;
            }
            const float running = run_speed > 0 ? run_speed * 0.6f : 350, still = std::max(40.0f, run_speed * 0.08f);
            int change = 0;
            if (ground_speed > running)
            {
                run_floor = std::min(run_floor, tilt);
                change = -1;
            }
            else if (turned)
            {
                stuck = 0;
                return 0;
            }
            else if (ground_speed < still && ++stuck >= 3 && tilt + 0.01f < run_floor - 0.015f)
                change = 1;
            else
            {
                if (ground_speed >= still)
                    stuck = 0;
                return 0;
            }
            stuck = 0;
            if (last && change != last)
                step = std::max(step / 2, 0.01f);
            float next = tilt + change * step;
            if (change > 0)
                next = std::min(next, run_floor - 0.02f);
            next = std::min(0.95f, std::max(0.25f, next));
            if (std::abs(next - tilt) < 0.001f)
                return 0;
            tilt = next;
            last = change;
            return change;
        }
    };

    // Slippery Trap: ice, with the numbers the game's own ice uses (measured in Snow Go with /ice_watch: Crash_IceSlide's
    // slide speed takes about 2 seconds to build up to full and about 1.8 seconds to die away). Crash's movement is a
    // velocity that only slowly follows your input: pushing pulls it toward the way you push, letting go lets friction
    // slow it. So he sets off slowly, turns in wide slides, slides on after you let go, and a reversal slides to a
    // stop before he heads back. From a standstill (or at the bottom of a reversal) he walks the way you push.
    // The d-pad steers the same way. Movement keys keep reporting "held" for a moment after release.
    inline constexpr float ice_accel = 0.55f;    // share of full speed per second, toward the way you push
    inline constexpr float ice_friction = 0.6f;  // share of full speed per second lost with nothing pushed
    inline constexpr float slide_stop = 0.15f, slide_run = 0.6f; // below: stopped, below run: walking
    inline constexpr unsigned slippery_key_hold_ms = 450;
    struct StickMomentum
    {
        float vx = 0, vy = 0; // Crash's sliding velocity as a share of full speed
        std::uint64_t last = 0;
        bool primed = false;
        void apply(std::int16_t &stick_x, std::int16_t &stick_y, std::uint16_t &buttons, std::uint64_t now, float walk_tilt)
        {
            float ix = stick_x, iy = stick_y;
            if (std::hypot(ix, iy) <= stick_deadzone)
            {
                // resting stick: the d-pad steers, otherwise there is no input
                ix = static_cast<float>(((buttons & 8u) ? 1 : 0) - ((buttons & 4u) ? 1 : 0));
                iy = static_cast<float>(((buttons & 1u) ? 1 : 0) - ((buttons & 2u) ? 1 : 0));
            }
            buttons = static_cast<std::uint16_t>(buttons & ~0xFu);
            const bool pushing = ix != 0 || iy != 0;
            const float length = pushing ? std::hypot(ix, iy) : 1.0f;
            const float tx = pushing ? ix / length : 0.0f, ty = pushing ? iy / length : 0.0f;
            if (!primed || now < last)
            {
                // a trap that starts mid-run keeps the run going
                primed = true, vx = tx, vy = ty;
            }
            else
            {
                const float dt = static_cast<float>(now - last > 100 ? 100 : now - last) / 1000;
                if (pushing)
                {
                    const float dx = tx - vx, dy = ty - vy, gap = std::hypot(dx, dy), step = ice_accel * dt;
                    if (gap <= step)
                        vx = tx, vy = ty;
                    else
                        vx += dx / gap * step, vy += dy / gap * step;
                }
                else
                {
                    const float speed = std::hypot(vx, vy), drop = ice_friction * dt;
                    if (speed <= drop)
                        vx = vy = 0;
                    else
                        vx -= vx / speed * drop, vy -= vy / speed * drop;
                }
            }
            last = now;
            const float speed = std::hypot(vx, vy);
            if (speed < slide_stop)
            {
                // nearly still: he walks off the way you push, or stands
                const float tilt = pushing ? walk_tilt * 32767 : 0.0f;
                stick_x = static_cast<std::int16_t>(std::lround(tx * tilt));
                stick_y = static_cast<std::int16_t>(std::lround(ty * tilt));
                return;
            }
            const float tilt = (speed < slide_run ? walk_tilt : 1.0f) * 32767;
            stick_x = static_cast<std::int16_t>(std::lround(vx / speed * tilt));
            stick_y = static_cast<std::int16_t>(std::lround(vy / speed * tilt));
        }
    };
    struct KeyMomentum
    {
        std::uint64_t last_down[256]{};
        // returns whether the key should read as held
        bool apply(unsigned code, bool down, std::uint64_t now)
        {
            if (down)
                last_down[code & 255] = now;
            return down || (last_down[code & 255] && now - last_down[code & 255] < slippery_key_hold_ms);
        }
    };

    // Darkness Trap: a round spotlight on Crash that fades smoothly into near-black, like Lights Out.
    // Drawn as triangles whose per-vertex alpha the GPU blends, so there are no visible steps.
    struct DarkVertex
    {
        float x, y;
        unsigned color; // ABGR, black with alpha
    };
    inline constexpr unsigned dark_alpha = 255; // pure black outside the light
    inline constexpr float dark_lit = 0.10f, dark_mid = 0.17f, dark_edge = 0.26f; // radii, share of the shorter side
    // the darkness creeps in over a few seconds (the light closing in on Crash) and lifts quicker when the trap ends
    inline constexpr float darkness_fade_in_ms = 5000, darkness_fade_out_ms = 1500;
    // how dark it is drawn, 0 (not at all) to 1 (fully), for how far the fade has come: eased, so it starts and ends gently
    inline float darkness_strength(float progress)
    {
        progress = progress < 0 ? 0 : progress > 1 ? 1 : progress;
        return progress * progress * (3 - 2 * progress);
    }
    // strength: 1 is the full trap; below that the black is fainter and the light wider
    template <class Emit>
    void darkness_mesh(float width, float height, float cx, float cy, Emit emit, float strength = 1)
    {
        constexpr unsigned segments = 64;
        constexpr float pi = 3.14159265358979f;
        strength = strength < 0 ? 0 : strength > 1 ? 1 : strength;
        const float side = (width < height ? width : height) * (1 + 2.5f * (1 - strength));
        const float beyond = 2 * (width + height) + side * dark_edge; // past every screen corner from any centre on screen
        const auto black = [strength](float t) { return static_cast<unsigned>(dark_alpha * t * strength + 0.5f) << 24; };
        // radius and darkness of each ring edge; nothing is drawn inside the lit circle
        const std::pair<float, unsigned> rings[]{{side * dark_lit, black(0)}, {side * dark_mid, black(0.45f)},
                                                 {side * dark_edge, black(1)}, {beyond, black(1)}};
        for (unsigned i = 0; i < segments; ++i)
        {
            const float a0 = 2 * pi * i / segments, a1 = 2 * pi * (i + 1) / segments;
            const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
            for (unsigned r = 0; r + 1 < sizeof(rings) / sizeof(rings[0]); ++r)
            {
                const auto [inner, inner_color] = rings[r];
                const auto [outer, outer_color] = rings[r + 1];
                const DarkVertex p0{cx + inner * c0, cy + inner * s0, inner_color}, p1{cx + inner * c1, cy + inner * s1, inner_color},
                    q0{cx + outer * c0, cy + outer * s0, outer_color}, q1{cx + outer * c1, cy + outer * s1, outer_color};
                emit(p0, q0, q1);
                emit(p0, q1, p1);
            }
        }
    }

    inline void reverse_keyboard(unsigned char *keys)
    {
        // 256-byte DirectInput keyboard state, indexed by scan code
        static constexpr unsigned char pairs[][2]{{0xC8, 0xD0}, {0xCB, 0xCD}, {0x11, 0x1F}, {0x1E, 0x20}};
        for (const auto &pair : pairs)
        {
            const auto first = keys[pair[0]];
            keys[pair[0]] = keys[pair[1]];
            keys[pair[1]] = first;
        }
    }
}
