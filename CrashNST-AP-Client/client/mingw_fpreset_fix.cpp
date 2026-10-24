// MSYS2's MinGW-w64 toolchain (October 2026, from about 20:10 UTC-7 on the 23rd): libmingwex's fesetenv calls msvcrt's
// _fpreset, and the import library no longer has a _fpreset stub, so the call is linked as a 32-bit relative call that
// the C runtime patches when the DLL loads (a runtime pseudo relocation). When Windows puts our DLL more than 2 GB away
// from msvcrt.dll, the patch can't reach and the C runtime stops the game before anything else runs (exception
// 0x40000015 in crash_nst_ingame.dll: V1.3.10, V1.3.11 and Stable V1.3 closed at start for some). Defined here, the call
// stays inside the DLL, and this calls msvcrt's own through a pointer. tools/check_pseudo_relocs.py fails a build that
// has any such 32-bit patch again.
#ifdef __MINGW32__
#include <windows.h>
extern "C" void __cdecl _fpreset(void)
{
    using Reset = void(__cdecl *)(void);
    static const auto reset = reinterpret_cast<Reset>(GetProcAddress(GetModuleHandleW(L"msvcrt.dll"), "_fpreset"));
    if (reset)
        reset();
}
#endif
