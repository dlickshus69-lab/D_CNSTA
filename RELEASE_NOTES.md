## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Papu Papu, round 10**:
  - **The angel death, third try.** V1.3.17's log showed what went wrong in V1.3.16: the death Crash's script plays isn't a name. It's an object that starts with the code that plays it, and the angel one is the same object every time. V1.3.16 put the name there, so the game crashed. Now, for a landing on red with no mask, the client swaps DeathLink's falling death for a real angel death object, only when both are the kind seen in the log. It finds one in Papu's attack setting or among Crash's parts, or keeps the one from his spin if his spin killed you earlier. If it finds none, you get the falling death, and the log says why.
  - **Health bar research:** snapshots of the bar's small numbers 1.5 s after the fight starts and 1.5 s after each hit, to find the fill. V1.3.17's log hid numbers that changed often.
- From V1.3.13–17: `/skip_filler yes`, one Aku Aku Mask in boss fights, a landing while red is a real hit for Crash, his last hit always counts, his spins keep the game's length, he stays red through the flinch, the bar has the right number of notches, and the V1.3.16 crash is fixed.
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
