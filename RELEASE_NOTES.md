## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Red ends when his spin does.** After a hit, Papu Papu is red from when he gets up until the spin after it is over, and then you can hit him. In V1.3.30, red ran on a set timer, so it often stayed on for a second or two after his spin, or ended during it. Now the client ends the game's own "can't be hit" time the moment that spin ends, and his red glow follows the game's own "can be hit" switch, so red still always means he can't be hit.
- **Research for red at the start of the fight, and for the health bar.** The game only has its "can't be hit" time after a hit, so there's nothing to switch on yet when the fight starts. The bar also only redraws when he's hit, which is why it still lost a notch at his first hit and flashed back to full at his second. This build logs what each of the game's messages to Papu Papu changes, and how the bar moves at his first two hits, so the next build can start the fight red and keep the bar clean.
- From V1.3.30: a landing on red Papu bounces Crash off, Papu isn't hurt and doesn't flinch, and Crash takes the hit.
- From V1.3.29: the bar lasts until his last hit.
- From V1.3.27: the angel death for a red landing without a mask, no freezes from the bar, no hit while Crash is still blinking.
- From V1.3.13–26: a red landing with a mask sends the game's own "Aku takes the hit" message, `/aku_give` and `/skip_filler yes`, one Aku Aku Mask item in boss fights, his spins keep the game's length, and the bar grows by default (`/boss_bar keep` keeps the game's 5 notches).
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
