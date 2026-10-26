## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **No stutter in Papu Papu's fight:** the game no longer stops for a moment as he stands up. The fight's watches on the game are now set in the background, and only where they're missing.
- **Boss Slam Aim** (new YAML option, 0–100%, default 55): how far ahead of Crash his slam aims. 0 aims where Crash is, 100 where he's heading.
- **Boss Hit Window** now goes from 2 s (20) to 10 s (100), or `by_speed` as before.
- **Papu Papu is done**, and all of him is in **Stable V1.4** too: the red rule, the aimed slams, the faster fight, the wall in front of his throne (F7 hides it) and the YAML options.
- From V1.3.82: landing on red Papu costs a life; Crash keeps his speed along the wall.
