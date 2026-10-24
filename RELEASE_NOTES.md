## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Red now always means Papu Papu can't be hit.** In V1.3.29's clips, a landing on red Papu sometimes still counted as a hit. The game's "can't be hit right after a hit" time only covered part of his red time, and V1.3.29's other fix didn't stop his health script from taking the hit. Now his red time *is* the game's own can't-be-hit time. Before each hit it's set to last through his get-up, his wait and his next spin, and it gets longer as his spins do. He stays red exactly as long as it lasts. A landing on red Papu bounces Crash off, Papu isn't hurt and doesn't flinch, and Crash takes the hit (Aku Aku, or the angel death without a mask). Red can end a moment before or after his spin does.
- **The start of the health bar.** In V1.3.29's clips, the bar jumped back to full for a moment at his second hit and lost the same notch again. With 10 hits on 9 notches, one hit can't show on the bar, and that's now his first hit: the bar stays full, or drops a notch and goes right back. Every hit after that takes exactly one notch, with no jump back to full.
- From V1.3.29: the bar lasts until his last hit, and Crash gets hurt when he bounces off red Papu.
- From V1.3.27: the angel death for a red landing without a mask, no freezes from the bar, no hit while Crash is still blinking.
- From V1.3.13–26: a red landing with a mask sends the game's own "Aku takes the hit" message, `/aku_give` and `/skip_filler yes`, one Aku Aku Mask item in boss fights, his spins keep the game's length, and the bar grows by default (`/boss_bar keep` keeps the game's 5 notches).
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
