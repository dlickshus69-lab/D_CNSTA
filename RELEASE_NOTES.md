## What's new

- **Ability Shuffle logic knows which levels need a move.** Native Fortress needs Spin and Bee-Having needs Slide for everything past the spot that needs it, in both Ability Logic modes. Logic won't send you there before you have the move, so you won't have to switch to Crashsanity's vanilla controls to get through. New seeds only.
- **Spin Placement `early` is gone.** The choices are `early_to_mid` (default) and `anywhere`; older YAMLs that say `early` get `early_to_mid`.

## Recent builds

- **V1.2.17:** title screen and hubs show Available checks (green when you can get any) and Available levels.
- **V1.2.16:** the game opens the way you last set it (window or full screen), in every seed. The Releases page shows these notes.
- **V1.2.15:** a skin is never saved as the "default" Crash anymore. Traps that do nothing in a level (shown as "off here") wait there instead of running out.
- **V1.2.13:** the Jetpack skin choice is gone. That file was the game's own Crash.
- **V1.2.12:** with Boxsanity, the hub labels show `B:` box checks found out of total.
- **V1.2.11:** with Aku Aku Sanity, Lights Out and Fumbling in the Dark need the Aku Aku item in logic (new seeds). Move Lock Traps wait until you have a move to lock. A second trap of the same kind no longer adds time.
- **V1.2.10:** the TNT countdown sound plays softer and fuller. The Nitro Trap is complete.
- **V1.2.9:** no more "LOOK OUT!" after the Nitro countdown. The Nitro falls in 0.31 s.
- **V1.2.7:** Crash skins: `crash_skin` in the YAML's Cosmetic section (`default_crash`, `playstation_1`, `biker`) or `/skin` in the console. The Nitro countdown plays the game's own TNT sound.
