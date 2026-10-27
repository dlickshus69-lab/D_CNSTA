## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Koala Kong's swat now turns the rock (V1.5.5's run):** it turned on 3 of 4 tries, and the log found where the rock's speed is kept. But the turned rock flew straight through Crash and the floor, because that flight only hits him.
- **Now it lands:** the turn happens when the rock is 40% of the way to him (a gold flash on him), aimed at a spot just in front of Crash. There it stops, and the rock is told it landed, the same message his own throws trigger when they hit the ground. If that works, you can spin it back again, and it's faster on each swat.
- Rock colors as before (red 3, orange 2, yellow 1, green: the next spin hits him). His 8-notch bar is unchanged.
- To try: `/invincible on`, then `/boss koala`. Spin his rock back, then try to spin it again where it lands. Send the log and the clip.
