## What's new

- **More levels need their move (Ability Shuffle, new seeds).** Crash Crush needs Slide (the electric gates), Sewer or Later needs Spin and Crouch Jump (the crate walls and low pipes), Un-Bearable needs Body Slam and Slide, and The Eel Deal's bonus round needs Spin. Until you find the move, the level shows as not available yet, so you come back later instead of needing Crashsanity's vanilla controls.
- With `spin_placement: anywhere`, moves really can land anywhere: in a test of 24 seeds with these settings, Spin was in the first checks you can do in about 4 of 10 seeds and took anywhere from 50 to 700 checks of jumping in the rest. Every seed was beatable with every check in logic.

## Recent builds

- **V1.2.19:** popups stay 7 seconds, and holding F2 shows the latest 12 again.

- **V1.2.18:** Ability Shuffle logic: Native Fortress needs Spin and Bee-Having needs Slide (new seeds). Spin Placement `early` removed (older YAMLs get `early_to_mid`).
- **V1.2.17:** title screen and hubs show Available checks (green when you can get any) and Available levels.
- **V1.2.16:** the game opens the way you last set it (window or full screen), in every seed. The Releases page shows these notes.
- **V1.2.15:** a skin is never saved as the "default" Crash anymore. Traps that do nothing in a level (shown as "off here") wait there instead of running out.
- **V1.2.13:** the Jetpack skin choice is gone. That file was the game's own Crash.
- **V1.2.12:** with Boxsanity, the hub labels show `B:` box checks found out of total.
- **V1.2.11:** with Aku Aku Sanity, Lights Out and Fumbling in the Dark need the Aku Aku item in logic (new seeds). Move Lock Traps wait until you have a move to lock. A second trap of the same kind no longer adds time.
- **V1.2.10:** the TNT countdown sound plays softer and fuller. The Nitro Trap is complete.
- **V1.2.9:** no more "LOOK OUT!" after the Nitro countdown. The Nitro falls in 0.31 s.
- **V1.2.7:** Crash skins: `crash_skin` in the YAML's Cosmetic section (`default_crash`, `playstation_1`, `biker`) or `/skin` in the console. The Nitro countdown plays the game's own TNT sound.
