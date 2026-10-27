## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Koala Kong's health bar lasts until his last hit.** The game builds his bar with his usual 4 notches before the client can raise his hits, so each notch now stands for 2 of his 8 hits instead of the bar vanishing halfway. The log also checks how to draw all 8 notches next.
- **Koala Kong's rally (first try, harder bosses on):** when you spin his big rock back, he swats it back at you 2 or 3 times before it can hit him. He flashes gold on each swat, and the rock glows by the swats he has left: red 3, orange 2, yellow 1, green when the next spin will hit him.
- If a swat doesn't bring the rock back, his swats stop for that visit and the log says why. Faster rocks with each swat come once the swat itself works.
- To try: `/invincible on`, then `/boss koala`. Spin his big rocks back a few times, watch the bar and the colors, and send the log plus a clip.
