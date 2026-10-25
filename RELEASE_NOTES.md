## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Papu Papu's mid-spin turn keeps working** after Crash dies in a spin (it switched itself off for the rest of the fight before).
- **His hit window is 1.5 s at 200%**, up to 3 s at 100%.
- **New YAML sliders (new seeds):** Boss Slam Time and Boss Hit Window. Left at `by_speed` they follow Boss Speed; a number sets that time at any speed.
- **His faster slam fixes itself** when it locks onto the wrong timing, instead of slamming at normal speed.
- From V1.3.60: he turns the other way in the middle of each spin.
