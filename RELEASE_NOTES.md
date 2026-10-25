## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Papu Papu's slam paced by boss speed:** his slam comes 0.9 s after he stands at 100%, 0.72 s at 125%, 0.6 s at 150%, 0.51 s at 175% and 0.45 s at 200%, on time from his first slam. `/boss_slam 0.5` sets the 200% time to try (the others follow it); `/boss_slam off` goes back. Test: the test seed, `/boss papu`, a few slams, then the log and a clip.
- From V1.3.56: Papu Papu's slam comes faster with boss speed.
- From V1.3.52: Papu Papu's spin starts where he slammed.
- From V1.3.48: Stormy Ascent's crates shut in between iron crates need Spin.
- From V1.3.47: Tomb Wader needs Spin (its first door opens with the wheel).
