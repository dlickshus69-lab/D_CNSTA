## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **A faster spin** (`/boss_speed 150` or `/boss_speed 200`). V1.3.34's log showed his laps took the same 1.2 seconds at every speed: the number it changed isn't the one the game turns him with. The game takes his spin speed from a list in his settings (one speed for each hit he's taken), so now that list is sped up instead. It goes back to the game's own at 100%.
- **A faster slam, found by itself.** His slam is a move the game plays, with no timer to shorten. So in his first two slams of a visit, the build looks around him for the clock that plays his moves (a number counting the seconds that starts over with each move). Then, with `/boss_speed` above 100, it tries the numbers next to that clock as its speed, one in each slam. The one that makes the clock run faster is kept, and from then on every slam plays at your speed. The log says what it found.
- **His moves timed in the log**: for each cycle, how long from when he stands to his slam, from the slam to his spin, his spin, and each lap. At 100% that's his normal speed, to compare with.
- The waits shortened in V1.3.34 (before his spin, and as he comes into the arena) stay shortened.
- From V1.3.34: red at the start the first time in a visit is the game's bounce, with no flinch.
- From V1.3.33: 9 hits at most, no Aku Aku masks in harder boss fights, red from the very start as he comes into the arena.
- From V1.3.32: red ends after the slam that follows his spin.
- From V1.3.31: red follows the game's own "can be hit" switch, so red always means he can't be hit.
- From V1.3.30: a landing on red Papu bounces Crash off, and Papu isn't hurt and doesn't flinch.
- From V1.3.29: the bar lasts until his last hit.
- From V1.3.27: the angel death for a red landing without a mask, no freezes from the bar, no hit while Crash is still blinking.
- From V1.3.13–26: `/aku_give` and `/skip_filler yes`, his spins keep the game's length, and the bar grows by default (`/boss_bar keep` keeps the game's 5 notches).
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
