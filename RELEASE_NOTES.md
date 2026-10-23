## What's new

- **Aku Aku Mask, fourth try.** The pickup message (5bnreye5) didn't bring a mask out either. `/aku_give` now also tries setting the game's own mask count (AkuAkuCount) with its own setter, and logs more of Aku Aku's messages so a real mask pickup (`/aku_lock off`, then break an Aku Aku crate) is caught in full.

## Recent builds

- **V1.2.32:** Aku give sends 5bnreye5; the death counter works without DeathLink (just "Deaths: N" solo).
- **V1.2.31:** Aku give turns the no-masks switch off first and can fall back to a crate; Aku Aku Sanity's lock works with DeathLink off.
- **V1.2.30:** Aku Aku Mask filler (experimental, `aku_aku_mask_weight`) and a direct `/aku_give`.
- **V1.2.29:** the death counter is always on screen (total and every player, hubs and levels).
- **V1.2.28:** fixed the game not opening on V1.2.25 to V1.2.27 (the death counter read a file that didn't exist yet).
- **V1.2.27:** a level's clear is sent when you pick up its box gem or relic, if the save missed it.
- **V1.2.26:** the Crash 3 secret pad is asked for once per hub visit again (V1.2.24's retry left the next ride on a black screen).
- **V1.2.25:** death counter (everyone's DeathLink deaths this seed, each player's in the hubs).
- **V1.2.24:** boxes that need a move (arrow crates, crates on TNT, tall stacks: Spin; iron-bound crates, Turtle Woods' underground room: Body Slam), gimmick stages need no moves, box tracker re-attaches after reloads, DeathLink skips time trials and pause-menu Restart, rides hold received DeathLinks, no Darkness on Coco's jet ski.
- **V1.2.23:** trap mercy: each death takes 5 seconds off every timed trap.
- **V1.2.22:** a DeathLink you receive on a ride (hog, polar bear, jet ski, jetpack, vehicles) costs a life instead of breaking the ride. The Great Gate needs Spin (Ability Shuffle, new seeds).
- **V1.2.21:** F2 keeps the last 300 popups and scrolls (mouse wheel, Page Up/Down, arrows, Home/End). Deaths on rides send DeathLink. The Darkness Trap fades in over 5 seconds.
- **V1.2.20:** Crash Crush, Sewer or Later, Un-Bearable and The Eel Deal's bonus round need their moves (Ability Shuffle, new seeds).
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
