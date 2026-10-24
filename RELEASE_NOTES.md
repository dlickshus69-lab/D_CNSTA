## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **9 hits at most.** The health bar can only draw 9 notches, so a boss now has 9 hits at most, whatever the health setting. Papu Papu has 9 at 180% and at 200%. Every hit takes exactly one notch, with no notch to make up. A bigger bar for more hits can come later.
- **No Aku Aku masks in harder boss fights.** With any of the harder boss settings on (more health, more speed, or the red rule), Crash loses his mask when the fight starts and gets none during it. Mask items wait for the next level. Every red landing is now the angel death.
- **Red from the very start of the fight.** In V1.3.32, red started once Papu Papu was back in his spot, and you could still hit him while he was jumping into the arena (at the start, and again after a death). Red now starts as he comes into the arena.
- **The game's "can't be hit" timer after a death.** If the timer is still running when the fight starts over, it's kept going for the opening red, so a landing then is the game's own bounce. V1.3.32's log showed a timer that had already ended won't start counting again. This build keeps it set anyway, to see whether that still keeps him from being hit.
- **Landing on him in the opening red the first time in a visit** is still a real hit that gets taken back, with a flinch. The game only creates its timer at his first hit.
- From V1.3.32: red ends after the slam that follows his spin.
- From V1.3.31: red follows the game's own "can be hit" switch, so red always means he can't be hit.
- From V1.3.30: a landing on red Papu bounces Crash off, and Papu isn't hurt and doesn't flinch.
- From V1.3.29: the bar lasts until his last hit.
- From V1.3.27: the angel death for a red landing without a mask, no freezes from the bar, no hit while Crash is still blinking.
- From V1.3.13–26: `/aku_give` and `/skip_filler yes`, his spins keep the game's length, and the bar grows by default (`/boss_bar keep` keeps the game's 5 notches).
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
