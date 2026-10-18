#include "function_hook.h"
#include "engine_memory.h"
#include <stdexcept>
namespace ingame
{
    using namespace engine_memory;
    std::vector<unsigned char> FunctionHook::jump(Address destination, unsigned size)
    {
        // FF 25 + zero displacement jumps through the following 64-bit address without messing with a register
        std::vector<unsigned char> code(size, 0x90);
        code[0] = 0xff;
        code[1] = 0x25;
        std::fill(code.begin() + 2, code.begin() + 6, 0);
        std::memcpy(code.data() + 6, &destination, 8);
        return code;
    }
    void FunctionHook::write(const std::vector<unsigned char> &from, const std::vector<unsigned char> &to)
    {
        std::vector<unsigned char> actual(from.size());
        SIZE_T got = 0;
        const auto at = reinterpret_cast<void *>(base() + rva);
        if (!ReadProcessMemory(GetCurrentProcess(), at, actual.data(), actual.size(), &got) || got != actual.size() ||
            actual != from)
            throw std::runtime_error("Local collectible hook changed");
        DWORD previous = 0;
        if (!VirtualProtect(at, to.size(), PAGE_EXECUTE_READWRITE, &previous))
            throw std::runtime_error("Cannot protect collectible hook");
        const bool written =
            WriteProcessMemory(GetCurrentProcess(), at, to.data(), to.size(), &got) && got == to.size();
        DWORD unused = 0;
        const bool protected_again = VirtualProtect(at, to.size(), previous, &unused) != 0;
        FlushInstructionCache(GetCurrentProcess(), at, to.size());
        if (!written || !protected_again)
            throw std::runtime_error("Cannot write collectible hook");
    }
    void FunctionHook::attach(Address at, std::initializer_list<unsigned char> prefix, Address callback)
    {
        if (installed)
            return;
        if (!original)
        {
            rva = at;
            before = prefix;
            // prefixes must end on instruction boundaries and be safe to replay at another address
            std::vector<unsigned char> actual(before.size());
            SIZE_T got = 0;
            if (before.size() < 14 ||
                !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(base() + rva), actual.data(),
                                   actual.size(), &got) ||
                got != actual.size() || actual != before)
                throw std::runtime_error("Local collectible function differs");
            auto trampoline = before;
            // calling original replays the displaced prologue, then resumes after the overwritten bytes
            const auto resume = jump(base() + rva + before.size(), 14);
            trampoline.insert(trampoline.end(), resume.begin(), resume.end());
            const auto memory = reinterpret_cast<Address>(
                VirtualAlloc(nullptr, trampoline.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (!memory)
                throw std::runtime_error("Cannot allocate collectible trampoline");
            std::memcpy(reinterpret_cast<void *>(memory), trampoline.data(), trampoline.size());
            DWORD old = 0;
            if (!VirtualProtect(reinterpret_cast<void *>(memory), trampoline.size(), PAGE_EXECUTE_READ, &old))
            {
                VirtualFree(reinterpret_cast<void *>(memory), 0, MEM_RELEASE);
                throw std::runtime_error("Cannot protect collectible trampoline");
            }
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(memory), trampoline.size());
            original = memory;
            patched = jump(callback, static_cast<unsigned>(before.size()));
        }
        try
        {
            write(before, patched);
            installed = true;
        }
        catch (...)
        {
            std::vector<unsigned char> actual(patched.size());
            SIZE_T got = 0;
            if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(base() + rva), actual.data(),
                                  actual.size(), &got) &&
                got == actual.size() && actual == patched)
            {
                installed = true;
                try
                {
                    restore();
                }
                catch (...)
                {
                }
            }
            throw;
        }
    }
    void FunctionHook::restore()
    {
        if (installed)
        {
            write(patched, before);
            installed = false;
        }
    }

} // namespace ingame
