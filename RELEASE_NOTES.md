## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **His last speed-ups are gentler.** At `/boss_speed 200` his spin went 2, 2.16, 2.3, then 2.46 and 2.6 for his last hits (laps of half a second, too fast to beat). Past 2.3 he now keeps only 30% of each step up: 2, 2.16, 2.3, 2.35, 2.39. He still gets a little faster at the end, just not out of reach. 150% stays as it was (it never goes past 2.3).
- **His slam is aimed at Crash** (with `/boss_rules on`). As he stands for his slam, he turns fast toward Crash for the first half of the wind-up, then holds that direction. The slam comes down where Crash was at that moment, so moving away in time still saves you. The log says how far he turned each time, and whether the game kept the turn.
- **His slam's own speed, another try.** V1.3.35 found nothing that counts the seconds of his moves. This build looks more widely, and also next to the number that sets his spin speed. Each one it finds is tried in one slam with `/boss_speed` above 100. If his slam comes clearly sooner than the game's 0.9 seconds, it keeps that one, and his slams play at your speed from then on.
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
