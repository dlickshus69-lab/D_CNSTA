## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **The real Nitro should now land on two of Roo's platforms** with test 3 on and harder bosses on. V1.4.12's log showed the beach crates' spawners were ready, but my check turned them all down because every crate carries a checkpoint script. Fixed.
- What to look for: do the Nitros sit on the platforms, does Roo hop through them or set them off, and do you die when you touch one? Send the log and a clip.
- The stray beach crates stay out of his water (V1.4.12, it worked). The stutter as his fight started should be gone: I took out the one-off search through every script in the fight.
- To try: `/roo_nitro test3` (if it isn't still on), restart, then `/boss ripper1`.
- Same as V1.4.8 otherwise: patterns that grow with each hit, a shorter wait at high Boss Speed, a hop onto you 1 landing in 5.
