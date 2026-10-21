#pragma once
#include <cmath>
#include <cstdint>

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
        TrapCount = 7
    };
    inline constexpr const char *trap_names[TrapCount]{"Death Trap", "Move Lock Trap", "Reverse Controls Trap",
                                                       "Freeze Trap", "Life Loss Trap", "Slippery Trap",
                                                       "Slow Trap"};
    inline constexpr bool timed_trap(unsigned kind)
    {
        return kind == TrapMoveLock || kind == TrapReverse || kind == TrapSlippery || kind == TrapSlow;
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

    // Slow Trap: the stick is scaled down; movement keys are pulsed on and off
    inline constexpr float slow_stick_scale = 0.45f;
    inline constexpr unsigned slow_pulse_ms = 70;
    inline std::int16_t scale_axis(std::int16_t value, float scale)
    {
        return static_cast<std::int16_t>(std::lround(value * scale));
    }
    inline bool slow_keys_down(std::uint64_t now) { return (now / slow_pulse_ms) % 2 == 0; }

    // Slippery Trap: the stick eases toward the real input like momentum on ice;
    // released movement keys keep reporting "held" briefly
    inline constexpr float slippery_ease_ms = 350;
    inline constexpr unsigned slippery_key_hold_ms = 250;
    struct StickMomentum
    {
        float x = 0, y = 0;
        std::uint64_t last = 0;
        bool primed = false;
        void apply(std::int16_t &stick_x, std::int16_t &stick_y, std::uint64_t now)
        {
            if (!primed || now < last)
            {
                x = stick_x, y = stick_y, last = now, primed = true;
                return;
            }
            const auto elapsed = static_cast<float>(now - last > 100 ? 100 : now - last);
            const auto blend = 1 - std::exp(-elapsed / slippery_ease_ms);
            x += (stick_x - x) * blend;
            y += (stick_y - y) * blend;
            last = now;
            stick_x = static_cast<std::int16_t>(std::lround(x));
            stick_y = static_cast<std::int16_t>(std::lround(y));
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
