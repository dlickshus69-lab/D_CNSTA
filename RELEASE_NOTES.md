## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Another try at the jump as his spin starts.** V1.3.41's watch caught what puts Papu Papu back every frame: a step of the game that copies his facing from his physics onto his picture. So his physics holds his real facing, and his spin starts from it. Now, as he aims, his physics is turned too, so the game itself should turn him and his spin should start where he slammed. This works from his first aimed slam (no waiting for his third spin).
- If his spin still jumps, the watch now goes on his physics, to catch what turns that back.
- **The Bosses options in the YAML** (the same ones as Stable V1.3.1): `harder_bosses`, `boss_health` (100–200), `boss_speed` (100–200) and `harder_bosses_aku_aku`. In the Testing build the slam aim is part of `harder_bosses`, and the test commands (`/boss_rules`, `/boss_health`, `/boss_speed`) still work on top of the YAML.
- From V1.3.41: health one hit every 25% (125 is 6, 150 is 7, 175 is 8, 200 is 9), his aim held until his spin starts, and the search for what turns with him (it found only his transform and his physics shape's angles: the facing lives deeper, in his physics).
- From V1.3.39–40: every slam aimed at Crash (except his first as he comes into the arena), and no dying after his last hit.
- From V1.3.38: the freeze fix (the game froze as he stood up after his first spin in V1.3.37).
- From V1.3.36: his last speed-ups gentler at 200% (2, 2.16, 2.3, 2.35, 2.39); his slam's own speed still isn't found (it comes 0.9 s after he stands, as in the game).
- From V1.3.35: a faster spin (his settings' list of spin speeds), his moves timed in the log.
- From V1.3.34: shorter waits before his spin and as he comes into the arena, and red at the start the first time in a visit is the game's bounce, with no flinch.
- From V1.3.33: 9 hits at most, no Aku Aku masks in harder boss fights, red from the very start as he comes into the arena.
- From V1.3.27–32: red follows the game's own "can be hit" switch and ends after the slam that follows his spin; a landing on red Papu bounces Crash off and hurts him (the angel death without a mask), and Papu isn't hurt and doesn't flinch; the bar lasts until his last hit.
- From V1.3.13–26: `/aku_give` and `/skip_filler yes`, his spins keep the game's length, and the bar grows by default (`/boss_bar keep` keeps the game's 5 notches).
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
