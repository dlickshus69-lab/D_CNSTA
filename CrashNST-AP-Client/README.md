# Crash NST Archipelago

## Install

### Apworld
1. Install `crash_nst.apworld` in Archipelago's `custom_worlds` folder. 
2. Use `Generate Template Options` in the launcher to get a yaml that you can put in your `Players` folder.

### Client
1. In Steam, right-click **Crash Bandicoot N. Sane Trilogy → Manage → Browse local files**. Find `CrashBandicootNSaneTrilogy.exe`.
2. Extract **`CrashNST-AP-Standalone-Windows-x64.zip`** into that folder, or copy **`dinput8.dll` and the entire `CrashNSTAP` folder** from the extracted ZIP.  
        
Place them directly next to the game EXE.  
The resulting layout is:

```text
CrashBandicootNSaneTrilogy.exe  (your existing game)
dinput8.dll
CrashNSTAP/
    crash_nst_ingame.dll
    ...
```

1. Launch NST normally through Steam.
2. The first connection defaults to `127.0.0.1:38281`, slot `CrashPlayer`.  
  
If the settings are wrong, the startup connection console opens. Enter:

```text
/server your-server:port
/slot YourSlotName
/connect
```

Use `/password value` before connecting if your room needs a password. Passwords are hidden and are not saved.

AP save profiles are isolated per seed/team/slot. 
Close NST before switching to another seed or slot.

## Play

See the [player guide](PLAYER_GUIDE.md) for world options, controls, etc.

## Build on Windows

Requirements: CMake 3.24+, an x64 MinGW C++17 toolchain and liblzma 5.8.4+.
The tested toolchain is MSYS2 **MINGW64**. 
In its shell, install the build tools:

```sh
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-make mingw-w64-x86_64-xz git
```

APCpp is an external dependency. From this client folder, clone it:

```sh
git clone --recursive https://github.com/randomcodegen/APCpp.git ../references/APCpp
git -C ../references/APCpp checkout 5eb74d0ee8df27fa1033c2c38d3e03ab1c951241
git -C ../references/APCpp submodule update --init --recursive
cmake -S client -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
```

Use an absolute `APCPP_SOURCE_DIR` if your dependency is elsewhere. 

Outputs are `build/proxy/dinput8.dll` and `build/crash_nst_ingame.dll`.

## Dependencies and coverage

See [THIRD_PARTY.md](THIRD_PARTY.md) and [licenses](licenses).
