## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Health goes up one hit every 25%** (Eric's call): 100% is the game's 5 hits, 125% is 6, 150% is 7, 175% is 8 and 200% is 9, so 200% is the top of the scale. (Until now it was one hit every 20%, so 9 came already at 170%.) The bar's notches follow.
- **Working on the jump as his spin starts.** V1.3.40's log and clip: every slam is aimed at Crash now, but when his spin starts he snaps back to the facing the game still has for him. The game keeps that facing somewhere the client hadn't found yet, so this build looks for it as he spins: in his first spins of each visit, it reads everything around him three times and keeps whatever turns along with him. From his third spin on, those are turned to the aim too, so his spin should start where he slammed.
- His aim is now held until his spin actually starts, not only until the slam lands.
- If his spin still jumps after that, the client watches the one place the game kept putting back last build, once per visit, and writes to the log which part of the game's code sets it. That shows where the game keeps his facing, for the next build.
- The log says for each aimed slam how far his spin started from the aim, and follows his first few aimed slams frame by frame.
- From V1.3.39–40: every slam aimed at Crash (except his first as he comes into the arena), and no dying after his last hit.
- From V1.3.38: the freeze fix (the game froze as he stood up after his first spin in V1.3.37).
- From V1.3.36: his last speed-ups gentler at 200% (2, 2.16, 2.3, 2.35, 2.39), and the search for his slam's own speed (still not found: his slam comes 0.9 s after he stands, as in the game).
- From V1.3.35: a faster spin (his settings' list of spin speeds), his moves timed in the log.
- From V1.3.34: shorter waits before his spin and as he comes into the arena, and red at the start the first time in a visit is the game's bounce, with no flinch.
- From V1.3.33: 9 hits at most, no Aku Aku masks in harder boss fights, red from the very start as he comes into the arena.
- From V1.3.32: red ends after the slam that follows his spin.
- From V1.3.31: red follows the game's own "can be hit" switch, so red always means he can't be hit.
- From V1.3.30: a landing on red Papu bounces Crash off, and Papu isn't hurt and doesn't flinch.
- From V1.3.29: the bar lasts until his last hit.
- From V1.3.27: the angel death for a red landing without a mask, no freezes from the bar, no hit while Crash is still blinking.
- From V1.3.13–26: `/aku_give` and `/skip_filler yes`, his spins keep the game's length, and the bar grows by default (`/boss_bar keep` keeps the game's 5 notches).
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
