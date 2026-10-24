## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **The bounce off red Papu, second try.** V1.3.28's log showed the game's "can't be hit right after a hit" time works: a stomp then only bounces Crash off, with no damage and no flinch. But it lasts 5.5 seconds, and his red time lasts until the end of his next spin, up to 12 seconds. The landings after those 5.5 seconds were still hits, which is why it seemed random. V1.3.28's research found the switch for this on Papu himself, and now it stays on for all of his red time and turns off when he stops being red.
- **Crash gets hurt by those bounces.** In V1.3.28 the client didn't notice the game's own bounce (it comes from Papu's health script), so Crash bounced off red Papu without getting hurt. Now Aku Aku takes the hit, or the angel death.
- **The health bar lasts until his last hit.** V1.3.28's research found the bar's own count of notches left. It starts at 9, the most the bar can draw, and goes down with every hit the game counts. With 10 hits it reached 0 one hit early, and the bar went away. Now that count is kept at his hits left, at most 9. A notch a red landing took also comes back. With 10 hits on 9 notches, one hit early in the fight doesn't move the bar.
- From V1.3.27: the angel death for a red landing without a mask, no freezes from the bar, no hit while Crash is still blinking.
- From V1.3.13–26: a red landing with a mask sends the game's own "Aku takes the hit" message, `/aku_give` and `/skip_filler yes`, one Aku Aku Mask item in boss fights, his spins keep the game's length, and the bar grows by default (`/boss_bar keep` keeps the game's 5 notches).
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
