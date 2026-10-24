## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Red at the start, the first time in a visit.** In V1.3.33 the first fight after arriving had no "can't be hit" timer yet (the game only makes it at his first hit), so landing on red Papu Papu then was a real hit: he flinched and the bar lost a notch, even though Crash still died. Now, for that red, his health script's list of the attacks that hurt him is emptied and his "can't be hit" switch is held on, so the landing should be the game's own bounce: no hit, no flinch, no notch, and Crash still dies. The list goes back as soon as his red ends. If the game doesn't use that list the way it looks, the log says so and the landing is still V1.3.33's hit taken back.
- **Speed, a first step** (`/boss_speed 150` or `/boss_speed 200`): every wait in Papu Papu's fight (his AI's own timers: before his slam, before his spin, after a hit) is cut by the speed as it starts, on top of his faster spin. His settings for those waits stay the game's, so no wait is cut twice, and the speed no longer stacks up from one visit to the next. The log lists each wait with its length, to tell which one is which.
- From V1.3.33: 9 hits at most, no Aku Aku masks in harder boss fights, red from the very start as he comes into the arena.
- From V1.3.32: red ends after the slam that follows his spin.
- From V1.3.31: red follows the game's own "can be hit" switch, so red always means he can't be hit.
- From V1.3.30: a landing on red Papu bounces Crash off, and Papu isn't hurt and doesn't flinch.
- From V1.3.29: the bar lasts until his last hit.
- From V1.3.27: the angel death for a red landing without a mask, no freezes from the bar, no hit while Crash is still blinking.
- From V1.3.13–26: `/aku_give` and `/skip_filler yes`, his spins keep the game's length, and the bar grows by default (`/boss_bar keep` keeps the game's 5 notches).
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
