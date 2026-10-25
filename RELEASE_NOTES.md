## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Papu Papu's faster slam, next try:** V1.3.55 found the clock in his animation that counts from when he stands up (his slam comes when it reaches about 0.9 s). This build runs that clock faster by `boss_speed` until his slam (his first stand plays as normal while the build watches it). Test: the test seed, `/boss papu`, 5 or 6 slams, then the log and a clip.
- From V1.3.52: Papu Papu's spin starts where he slammed.
- From V1.3.48: Stormy Ascent's crates shut in between iron crates need Spin.
- From V1.3.47: Tomb Wader needs Spin (its first door opens with the wheel).
- From V1.3.46: no free Aku Aku mask after deaths in a row (Aku Aku Sanity); DeathLink misses you during the box count; Un-Bearable strikes on foot.
