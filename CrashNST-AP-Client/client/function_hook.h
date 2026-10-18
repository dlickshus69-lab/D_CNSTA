#pragma once
#include <cstdint>
#include <initializer_list>
#include <vector>
namespace ingame
{
    // pinned prefixes end on instruction boundaries and contain no relative operands.
    struct FunctionHook
    {
        std::uintptr_t rva = 0, original = 0;
        std::vector<unsigned char> before, patched;
        bool installed = false;
        static std::vector<unsigned char> jump(std::uintptr_t destination, unsigned size);
        void write(const std::vector<unsigned char> &from, const std::vector<unsigned char> &to);
        void attach(std::uintptr_t at, std::initializer_list<unsigned char> prefix, std::uintptr_t callback);
        void restore();
    };
} // namespace ingame
