## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **The angel death, tenth try.** V1.3.26's log showed Papu's hit message reached Crash's damage script, which kept Papu's death (the angel) and then stopped before the death itself. When Papu's spin really kills Crash, that script takes one more step right away (`sdoxdefr`), and that step plays the death. Now, when it stops short, that step is sent too. If Crash still doesn't die from it, the falling death stays as the backup.
- **The freezes are fixed.** The 2 to 5 second freezes at the start of a boss fight and after the first hit were the client searching the health bar's memory for research (V1.3.22 to V1.3.26). That research is done and removed, and the one search still needed (to stretch the blue) now takes a moment instead of seconds.
- **No hit while blinking.** Landing on red Papu while Crash is still blinking from a hit doesn't hurt him, as the game does after any hit.
- From V1.3.25: the health bar's blue is full from the start of the fight and goes down a step per hit (confirmed). With more than 9 hits the bar can't get longer, so the steps don't line up exactly with the notch marks yet.
- From V1.3.13–26: a red landing with a mask sends the game's own "Aku takes the hit" message, `/aku_give` and `/skip_filler yes`, one Aku Aku Mask item in boss fights, his last hit always counts, his spins keep the game's length, he stays red through the flinch, and the bar grows by default (`/boss_bar keep` keeps the game's 5 notches).
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
