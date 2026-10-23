## What's new

- **Boxes that need a move (Ability Shuffle, new seeds).** Arrow crates, crates sitting on a TNT or Nitro (The High Road, Heavy Machinery) and crates in stacks four high (Cold Hard Crash) need Spin; iron-bound crates and Turtle Woods' underground room need Body Slam. Until you have the move they show as not available yet.
- **Gimmick stages need no moves again** (Crashsanity gives them vanilla controls), so Un-Bearable is back in logic. Crash 2 gold relics on the polar bear, jetpack and jet board no longer expect Speed Shoes (Pack Attack's relic).
- **Box checks after a death.** The box tracker now re-attaches when the level's crates are reloaded, matches crates the game made again after a death or a failed bonus round, and logs any broken crate it can't match.
- **DeathLink:** deaths in time trials and Restart from the pause menu aren't sent. On rides, a death is also seen when the ride's health runs out, and a DeathLink you receive there waits for your next level on foot (taking a life instead didn't work).
- **Darkness** is off on Coco's jet ski levels. **Crash 3's secret platform** is asked for again (up to 5 times) if it doesn't show up.

## Recent builds

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
