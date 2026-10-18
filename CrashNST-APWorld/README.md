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

See the [player guide](https://github.com/randomcodegen/CrashNST-AP/blob/main/CrashNST-APWorld/PLAYER_GUIDE.md) for world options, controls, etc.

## Source layout

- `worlds/crash_nst/__init__.py`: generation, regions, placement, slot data and tracker support
- `worlds/crash_nst/Options.py`: player options
- `worlds/crash_nst/data.py`: level, item, location and logic definitions
- `worlds/crash_nst/crates.json`: crate identities and route requirements

## Build

```
python tools/build_apworld.py
```

The output is `dist/crash_nst.apworld`

Runtime tables are checked into the client folder in this repository. 
From the repository root, run `python tools/sync_runtime_tables.py` after definition changes (use `--check` for verification).
Rebuilding this APWorld alone does not regenerate client tables. 

Crashsanity is experimental. Some power requirements are conservative.
