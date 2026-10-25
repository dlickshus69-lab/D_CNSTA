## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Fixed: the game froze** as Papu Papu stood up after his first spin (V1.3.37). A search for his facing that runs then got stuck on a number it couldn't handle. Everything from V1.3.37 below is in this build too, now without the freeze.
- **No more jump when his spin starts after an aimed slam.** V1.3.36 turned only the picture of Papu Papu, but the game keeps his facing in two more places. So as his spin started, the game put him back where it still had him, and the spin began somewhere else. Now all three are turned together, so his spin starts from where he slammed.
- **Every slam is aimed now, not only the ones after a hit.** After a spin, the game kept turning him back each frame (V1.3.36's log), so only slams after a hit stayed aimed. He now turns fast toward Crash for the first half of the wind-up and holds that facing until the slam lands.
- **His first slam as he comes into the arena isn't aimed**, at the start of the fight and when it starts over after a death. That one is left as the game has it.
- From V1.3.36: his last speed-ups gentler at 200% (2, 2.16, 2.3, 2.35, 2.39), and the search for his slam's own speed.
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
