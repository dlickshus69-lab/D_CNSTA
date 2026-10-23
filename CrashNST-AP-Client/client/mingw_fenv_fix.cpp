// MSYS2's MinGW-w64 toolchain (GCC 16.2, October 2026): libmingwex's fesetenv.o refers to __mingw_fe_dfl_env, but the
// static libraries no longer define it, so every -static link fails with "undefined reference to __mingw_fe_dfl_env"
// (builds that linked the day before failed too, with no change of ours). MinGW-w64's own CRT defines it as a zeroed
// fenv_t, and fesetenv treats an environment whose fields are both zero as the default one (it resets the FPU), so a
// zeroed one here means the same thing. It is weak, so a toolchain that defines it again takes precedence.
#ifdef __MINGW32__
#include <fenv.h>
extern "C" __attribute__((weak)) const fenv_t __mingw_fe_dfl_env{};
#endif
