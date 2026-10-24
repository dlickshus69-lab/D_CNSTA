## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

(V1.3.23 was only the README change: the same as V1.3.22.)

- **The angel death, seventh try.** Landing on Papu while he's red without a mask now sends Crash Papu's own spin hit again: the very hit his spin gave Crash earlier in the same visit, so the game itself picks the death (the angel flying up). His spin has to hit Crash once in the visit first (with or without a mask); until then a red landing is still the falling death. If the resent hit doesn't kill Crash, he gets the falling death instead. The log says which death happened.
- **Health bar fill, first fix.** V1.3.22's research and V1.3.21's clip: the blue fill is a picture painted only as long as the boss's own notches go (Papu's 5), so with more notches the bar grew but the blue stopped halfway. Now the picture is stretched by as much as the bar grew, so the blue should reach the end of the bar at full health and go down with each hit. Tests: `/bar_try none` turns the stretch off to compare, `/bar_try health` and `/bar_try scale` stretch two other parts sideways, and `/bar_try off` ends a test.
- From V1.3.13–23: a red landing with a mask sends the game's own "Aku takes the hit" message, `/aku_give` and `/skip_filler yes`, one Aku Aku Mask item in boss fights, his last hit always counts, his spins keep the game's length, he stays red through the flinch, and the bar grows by default (`/boss_bar keep` keeps the game's 5 notches).
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
