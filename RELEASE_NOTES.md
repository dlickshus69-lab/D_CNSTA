## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Every slam aimed at Crash, for real this time** (all but his first one as he comes into the arena). V1.3.38's log showed the game putting Papu Papu back to his own facing every single frame, so each frame's turn was a new little one (15 degrees at most) and he never got all the way around to Crash. Now his turn carries over from frame to frame, and it's also set in the two places the game keeps his facing (V1.3.38's search found them), so the game itself has him facing Crash, and his spin should start from where he slammed.
- If the game still puts him back to his own facing, the client tries the way that held after a hit in V1.3.36 (only his picture turned), and if that doesn't hold either, it leaves that one slam as the game has it instead of flickering between two facings.
- New in V1.3.40 (V1.3.39 had everything else here first): it gives the game a frame to catch up and lets it turn him a little at a time before falling back, so a slam isn't given up on too early, and on a fallback it puts the game's own facing back where it was first, so the fallback works the way V1.3.36's did after a hit.
- **No more dying after the last hit.** In V1.3.38's log the last hit landed just as his spin started, and 1.3 seconds after he was beaten his spinning staff still hit Crash. Once he's beaten, his attack can't hurt Crash.
- The log now says for each slam which way held him, and names the two places that keep his facing (from the game's own names for them).
- From V1.3.38: the freeze fix (the game froze as he stood up after his first spin in V1.3.37).
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
