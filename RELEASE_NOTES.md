## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **The bounce off red Papu, first try.** Until now a landing on red Papu was a real hit as far as the game knew: he flinched, and the client put his health back afterwards. Papu's health script has a setting the game uses for other bosses: a short time after each hit when he can't be hit. It's 0 for Papu. With the red rule on, it's now set to cover his red time (his get-up, his wait and the start of his next spin), so a landing on red Papu should bounce Crash off the way the game does when a boss can't be hit: no damage to Papu and no flinch. Crash still gets hurt (Aku Aku takes the hit, or the angel death). If the stomp ends up doing nothing at all, the client bounces Crash off itself.
- **Why the health bar ran out 2 hits early** (V1.3.27's clip and log). First, a red landing took a notch off the bar that never came back. The bar keeps its own count, and the client only put Papu's health back. The bounce above fixes that, because a red landing isn't a hit anymore. Second, the bar can only draw 9 notches. With 10 hits, it runs out with one hit still left. This build logs what the bar does at each hit, to fix that next.
- **Research:** every stomp on Papu goes to the log (what took it, and how long after his last hit), plus what on Papu changes when he can't be hit, in case the setting above isn't enough.
- From V1.3.27: the angel death for a red landing without a mask (confirmed), no freezes from the bar, no hit while Crash is still blinking.
- From V1.3.13–26: a red landing with a mask sends the game's own "Aku takes the hit" message, `/aku_give` and `/skip_filler yes`, one Aku Aku Mask item in boss fights, his spins keep the game's length, and the bar grows by default (`/boss_bar keep` keeps the game's 5 notches).
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
