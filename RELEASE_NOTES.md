## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Ripper Roo's patterns change (harder bosses on):** each time a stage starts (the fight's start, a death, a hit), he gets two new patterns, picked at random from 64 loops over his platforms (never the game's own V). Each time round he does one or the other.
- **He sometimes hops onto Crash:** now and then (8 s apart at least) his next hop is to the platform Crash is on, then he goes back to his pattern.
- **No more hopping backwards without turning:** his pattern is only changed while he's standing, not as a hop starts.
- **`/roo_files` (for the real Nitro):** type it in the console (F1); it writes `CrashNSTAP\research\roo_nitro_files.zip` in the game's folder. Send the zip with your log.
