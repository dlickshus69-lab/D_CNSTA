#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

// for newly allocated copies only. live pointer replacement still checks its expected value.
inline void verified_copy(HANDLE process, std::uintptr_t address, const void *bytes, std::size_t size,
                          const char *error)
{
    std::vector<unsigned char> actual(size);
    SIZE_T count = 0;
    if (!WriteProcessMemory(process, reinterpret_cast<void *>(address), bytes, size, &count) || count != size ||
        !ReadProcessMemory(process, reinterpret_cast<void *>(address), actual.data(), size, &count) || count != size ||
        std::memcmp(bytes, actual.data(), size))
        throw std::runtime_error(error);
}
