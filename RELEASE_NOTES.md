## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Red ends after his slam.** After his spin, Papu Papu slams his club into the ground. His red, and the game's "can't be hit" time with it, now lasts through that slam and ends the moment the club lands, so you jump on him right after the slam.
- **The fight starts red.** The order is now red, then you can hit him, then red again. From the start of the fight (and when it starts over after a death), he's red until the slam after his first spin. If the game already has its "can't be hit" timer from an earlier hit in this visit, landing on him then is the game's own bounce. The very first time in a visit, that timer doesn't exist until his first hit. So a landing during that first red is a real hit that gets taken back: Crash takes the hit and Papu keeps his health and the bar, but he flinches.
- **The health bar's first hit.** With 10 hits on 9 notches, his first hit is the one the bar can't show. The bar only redraws when he's hit, so V1.3.29–31 put its numbers back without it showing. Now the client fills the bar back up the game's own way (the way it refills when the fight starts over), so it should show all 9 notches again right after his first hit, and every hit after takes exactly one. Landing on him while he's red at the start uses the same refill.
- From V1.3.31: red follows the game's own "can be hit" switch, so red always means he can't be hit.
- From V1.3.30: a landing on red Papu bounces Crash off, Papu isn't hurt and doesn't flinch, and Crash takes the hit.
- From V1.3.29: the bar lasts until his last hit.
- From V1.3.27: the angel death for a red landing without a mask, no freezes from the bar, no hit while Crash is still blinking.
- From V1.3.13–26: a red landing with a mask sends the game's own "Aku takes the hit" message, `/aku_give` and `/skip_filler yes`, one Aku Aku Mask item in boss fights, his spins keep the game's length, and the bar grows by default (`/boss_bar keep` keeps the game's 5 notches).
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
