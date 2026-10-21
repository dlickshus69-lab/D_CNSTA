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

    // Slippery Trap: ice. Crash follows a velocity that your input only slowly steers. While it is above a small
    // floor he keeps running at full speed in its direction, so he slides on after you let go, drifts wide
    // through turns and skids before reversing. The d-pad steers the same velocity. Movement keys keep
    // reporting "held" for a moment after release.
    inline constexpr float slippery_ease_ms = 650;
    inline constexpr float slide_floor = 0.2f, slide_speed = 0.9f; // shares of a full push
    inline constexpr unsigned slippery_key_hold_ms = 450;
    struct StickMomentum
    {
        float x = 0, y = 0;
        std::uint64_t last = 0;
        bool primed = false;
        void apply(std::int16_t &stick_x, std::int16_t &stick_y, std::uint16_t &buttons, std::uint64_t now)
        {
            float ix = stick_x, iy = stick_y;
            if (std::hypot(ix, iy) <= stick_deadzone)
            {
                // resting stick: the d-pad steers, otherwise there is no input
                ix = static_cast<float>(((buttons & 8u) ? 1 : 0) - ((buttons & 4u) ? 1 : 0));
                iy = static_cast<float>(((buttons & 1u) ? 1 : 0) - ((buttons & 2u) ? 1 : 0));
                if (const float length = std::hypot(ix, iy); length > 0)
                    ix *= 32767 / length, iy *= 32767 / length;
            }
            buttons = static_cast<std::uint16_t>(buttons & ~0xFu);
            if (!primed || now < last)
                x = ix, y = iy, primed = true;
            else
            {
                const auto elapsed = static_cast<float>(now - last > 100 ? 100 : now - last);
                const auto blend = 1 - std::exp(-elapsed / slippery_ease_ms);
                x += (ix - x) * blend;
                y += (iy - y) * blend;
            }
            last = now;
            const float length = std::hypot(x, y);
            if (length < slide_floor * 32767)
            {
                stick_x = stick_y = 0;
                return;
            }
            const float scale = std::max(length, slide_speed * 32767) / length;
            const auto clamp = [](float v) { return static_cast<std::int16_t>(std::lround(v < -32768 ? -32768 : v > 32767 ? 32767 : v)); };
            stick_x = clamp(x * scale);
            stick_y = clamp(y * scale);
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
