## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Nitros turn into outlines for Roo:** when he's heading to a Nitro's platform or standing on it, the Nitro becomes an outline. It comes back as soon as he's off it, but not while you're standing there.
- **Game over crash, second try:** the beach crates' own level scripts (checkpoint, Aku Aku help, box count) are now kept quiet in Roo's arena. Those scripts are the new suspect.
- **Nitros never impossible:** 2 to start, 3 after your 3rd hit, never more. The free platforms always connect by straight jumps, including the one you're on.
- **No repeat platforms in his pattern:** it grows to 7 at most, each platform once, so he never hops in place.
- To try: test 3 still on, `/boss ripper1` with harder bosses on, and get a game over once. If it still crashes, run `/roo_nitro off`, restart the game and do one game over without the Nitros. Send the logs.
