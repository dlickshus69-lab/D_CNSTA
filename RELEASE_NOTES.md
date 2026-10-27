## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Koala Kong's rally, now all on one rock (V1.5.7's run):** your spins near the landed rock were seen every time, but the game's "spun back" message didn't send it flying. The rock's own messages only announce what happened, so they can't make it fly. Now the client flies the rock itself, the whole way: he swats it back (gold flash), it stops in front of Crash, and your spin sends it flying at him again. Then he swats it again, faster, or, with no swats left, it hits him like a normal throw.
- If you don't spin it within 5 seconds, it drops into the lava.
- The rock stays the same one, so it doesn't sink anymore, and the client also tries to reset its 10-second lifetime with each spin. The log says whether that worked.
- To try: `/invincible on`, then `/boss koala`. Play a whole rally: spin his rock, spin it again each time it comes back, until it hits him. Send the log and the clip.
