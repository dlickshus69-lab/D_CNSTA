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

## Build

Run from this repository's root.
Client builds require the x64 MinGW/CMake/liblzma dependencies listed in the client README.

```sh
git clone --recursive https://github.com/randomcodegen/APCpp.git references/APCpp
git -C references/APCpp checkout 5eb74d0ee8df27fa1033c2c38d3e03ab1c951241
git -C references/APCpp submodule update --init --recursive
python tools/sync_runtime_tables.py --check
cmake -S CrashNST-AP-Client/client -B build/client -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build/client --parallel 4
python CrashNST-APWorld/tools/build_apworld.py
```

An existing dependency checkout can be supplied through `-DAPCPP_SOURCE_DIR=/absolute/path/to/APCpp`   
The world package is written to `CrashNST-APWorld/dist/crash_nst.apworld`  
Engine binaries are under `build/client`  

If using a local liblzma installation, supply `LIBLZMA_INCLUDE_DIR` and `LIBLZMA_LIBRARY` as absolute paths to its include directory and static library.

## Shared data

`CrashNST-APWorld/worlds/crash_nst` holds the item, location, collectible and crate definitions  
`tools/sync_runtime_tables.py` regenerates the client's logic/collectible/crate tables from those definitions

```sh
python tools/sync_runtime_tables.py --check
```
