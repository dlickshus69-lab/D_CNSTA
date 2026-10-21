#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
namespace ingame::engine_memory
{
    using Address = std::uintptr_t;
    inline Address base()
    {
        static const auto module = reinterpret_cast<Address>(GetModuleHandleW(nullptr));
        return module;
    }
    template <class T> inline bool read(Address at, T &value)
    {
        SIZE_T got = 0;
        return at &&
               ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(at), &value, sizeof(value), &got) &&
               got == sizeof(value);
    }
    // a vector is filled element by element (the template above would overwrite the vector object itself)
    template <class T> inline bool read(Address at, std::vector<T> &values)
    {
        SIZE_T got = 0;
        const auto bytes = values.size() * sizeof(T);
        return at && !values.empty() &&
               ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(at), values.data(), bytes, &got) && got == bytes;
    }
    template <class T> T value(Address at)
    {
        T result{};
        read(at, result);
        return result;
    }
    inline std::string string(Address at, unsigned max = 128)
    {
        std::array<char, 128> bytes{};
        SIZE_T got = 0;
        max = std::min<unsigned>(max, bytes.size());
        if (!at || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(at), bytes.data(), max, &got))
            return {};
        const auto end = static_cast<const char *>(std::memchr(bytes.data(), 0, got));
        return end ? std::string(bytes.data(), static_cast<std::size_t>(end - bytes.data())) : std::string{};
    }
    inline bool typed(Address at, Address rva)
    {
        return value<Address>(at) == base() + rva;
    }
    inline std::vector<Address> list(Address at, unsigned max)
    {
        const auto count = value<unsigned>(at + 16), capacity = value<unsigned>(at + 20);
        const auto entries = value<Address>(at + 32);
        if (!at || count > max || capacity < count || (count && !entries))
            return {};
        std::vector<Address> result(count);
        SIZE_T got = 0;
        if (count && (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(entries), result.data(),
                                         count * 8, &got) ||
                      got != count * 8))
            return {};
        if (value<unsigned>(at + 16) != count || value<Address>(at + 32) != entries)
            return {};
        return result;
    }
    inline Address resolve(Address handle)
    {
        return handle && !(value<unsigned>(handle) & 0x30000000) ? value<Address>(handle + 40) : 0;
    }
    inline Address player_at(Address manager)
    {
        const auto players = list(value<Address>(manager + 16), 12);
        for (const auto p : players)
        {
            const auto actor = resolve(value<Address>(p + 16));
            const auto name = string(value<Address>(actor + 128));
            if (typed(actor, 0x1592220) && value<unsigned char>(actor + 341) == 1 &&
                resolve(value<Address>(actor + 328)) == p && (name == "Hero_Crash" || name == "Hero_Coco"))
                return actor;
        }
        return 0;
    }
    inline Address player()
    {
        return player_at(value<Address>(base() + 0x1a665e8));
    }
    template <std::size_t N> bool code_matches(Address rva, const std::array<unsigned char, N> &expected)
    {
        std::array<unsigned char, N> actual{};
        return read(base() + rva, actual) && actual == expected;
    }
} // namespace ingame::engine_memory
