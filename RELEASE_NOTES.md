## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Papu Papu, round 9**:
  - **The angel death, second try.** V1.3.15 still gave the falling death: both deaths end with Crash's damage script sending one message, and that message carries the death that plays. Now the client changes the death in that message to the one Papu Papu's spin gives (the angel). The log says whether it was changed.
  - **His bar's fill, second try** (`/boss_health 200`). V1.3.15's bar had 10 notches but was only half full, so the fill counts from a health of 500 somewhere other than the ones already changed. His health part's own full health now goes up to match too, if exactly one number there is the game's full health. The log lists every number like his hits or health in his actor, health part and bar, so if it's still half full the next try knows where to look.
- From V1.3.13–15: `/skip_filler yes`, one Aku Aku Mask in boss fights, a landing while red is a real hit for Crash, his last hit always counts, his spins keep the game's length, he stays red through the flinch, and the bar has the right number of notches.
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
