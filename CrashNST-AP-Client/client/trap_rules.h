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
        TrapCount = 8
    };
    inline constexpr const char *trap_names[TrapCount]{"Death Trap", "Move Lock Trap", "Reverse Controls Trap",
                                                       "Freeze Trap", "Life Loss Trap", "Slippery Trap",
                                                       "Slow Trap", "Darkness Trap"};
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
    inline constexpr unsigned freeze_trap_ms = 3000;
    // queued traps fire one at a time, this far apart
    inline constexpr unsigned trap_gap_ms = 10000;

    // a trap of the same kind adds its time to a running one
    inline std::uint64_t extend_timer(std::uint64_t until, std::uint64_t now, std::uint64_t ms)
    {
        return (until > now ? until : now) + ms;
    }
    inline bool timer_active(std::uint64_t until, std::uint64_t now) { return now < until; }
    inline unsigned seconds_left(std::uint64_t until, std::uint64_t now)
    {
        return until > now ? static_cast<unsigned>((until - now + 999) / 1000) : 0;
    }

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
        // one window of Crash's movement; returns +1 (raised), -1 (lowered) or 0
        int window(float ground_speed, float climb_speed, bool pushing, bool slowed)
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

    // Slippery Trap: ice. Crash cannot change direction quickly: his heading swings toward your input at a limited
    // rate, so turns and reversals become wide arcs at speed instead of stops. He builds up from a walk to a run,
    // and when you let go he keeps sliding, running then walking, before he stops. The d-pad steers the same way.
    // Movement keys keep reporting "held" for a moment after release.
    inline constexpr float slippery_turn_rate = 4.2f;  // radians per second, about 240 degrees
    inline constexpr float slippery_accel = 2.0f;      // share of full speed per second
    inline constexpr float slippery_decel = 1.1f;
    inline constexpr float slide_stop = 0.15f, slide_run = 0.6f; // below: stopped, below run: walking
    inline constexpr unsigned slippery_key_hold_ms = 450;
    struct StickMomentum
    {
        float angle = 0, speed = 0;
        std::uint64_t last = 0;
        bool primed = false;
        void apply(std::int16_t &stick_x, std::int16_t &stick_y, std::uint16_t &buttons, std::uint64_t now, float walk_tilt)
        {
            constexpr float pi = 3.14159265358979f;
            float ix = stick_x, iy = stick_y;
            if (std::hypot(ix, iy) <= stick_deadzone)
            {
                // resting stick: the d-pad steers, otherwise there is no input
                ix = static_cast<float>(((buttons & 8u) ? 1 : 0) - ((buttons & 4u) ? 1 : 0));
                iy = static_cast<float>(((buttons & 1u) ? 1 : 0) - ((buttons & 2u) ? 1 : 0));
            }
            buttons = static_cast<std::uint16_t>(buttons & ~0xFu);
            const bool pushing = ix != 0 || iy != 0;
            const float wanted = pushing ? std::atan2(iy, ix) : angle;
            if (!primed || now < last)
            {
                // a trap that starts mid-run keeps the run going
                primed = true, angle = wanted, speed = pushing ? 1.0f : 0.0f;
            }
            else
            {
                const float dt = static_cast<float>(now - last > 100 ? 100 : now - last) / 1000;
                if (pushing)
                {
                    if (speed < slide_stop)
                        angle = wanted; // from a standstill he sets off the way you push
                    else
                    {
                        float turn = std::remainder(wanted - angle, 2 * pi);
                        const float most = slippery_turn_rate * dt;
                        angle += turn > most ? most : turn < -most ? -most : turn;
                    }
                    speed = std::min(1.0f, speed + slippery_accel * dt);
                }
                else
                    speed = std::max(0.0f, speed - slippery_decel * dt);
            }
            last = now;
            if (speed < slide_stop)
            {
                stick_x = stick_y = 0;
                return;
            }
            const float tilt = (speed < slide_run ? walk_tilt : 1.0f) * 32767;
            stick_x = static_cast<std::int16_t>(std::lround(std::cos(angle) * tilt));
            stick_y = static_cast<std::int16_t>(std::lround(std::sin(angle) * tilt));
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
    template <class Emit>
    void darkness_mesh(float width, float height, float cx, float cy, Emit emit)
    {
        constexpr unsigned segments = 64;
        constexpr float pi = 3.14159265358979f;
        const float side = width < height ? width : height;
        const float beyond = 2 * (width + height); // past every screen corner from any centre on screen
        const auto black = [](float t) { return static_cast<unsigned>(dark_alpha * t + 0.5f) << 24; };
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
