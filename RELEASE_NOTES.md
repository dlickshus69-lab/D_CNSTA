## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Fix: the game crashed when you landed on red Papu Papu** (V1.3.16). The angel-death try changed the death inside Crash's own death message, and the game couldn't take it. That's gone: a landing on red Papu without a mask is the falling death again, for now.
- **Research for the angel death** (read only, nothing changed): the log shows which death Crash's damage script plays each time he dies in a boss fight, and what Papu's spin hit carries. Let Papu's spin hit you once in the test so the log has the real one to compare.
- **Research for the half-full health bar**: V1.3.16 found no 500 left anywhere in Papu or his bar's first part, so the log now watches much more of the bar (and what it points to) and shows what changes when Papu is hit.
- From V1.3.13–16: `/skip_filler yes`, one Aku Aku Mask in boss fights, a landing while red is a real hit for Crash, his last hit always counts, his spins keep the game's length, he stays red through the flinch, and the bar has the right number of notches.
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
