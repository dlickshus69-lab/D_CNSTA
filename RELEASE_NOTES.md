## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Papu Papu's hit window:** with the boss rules on, once his red ends you have 5 s to hit him; then he's red again until the slam after his next spin. `/boss_window 4` (1 to 15) tries another time; `/boss_window off` goes back to 5.
- From V1.3.57: Papu Papu's slam paced by boss speed (0.9 s after he stands at 100%, 0.72 at 125%, 0.6 at 150%, 0.51 at 175%, 0.45 at 200%); `/boss_slam 0.5` tries another 200% time.
- From V1.3.52: Papu Papu's spin starts where he slammed.
- From V1.3.48: Stormy Ascent's crates shut in between iron crates need Spin.
- From V1.3.47: Tomb Wader needs Spin (its first door opens with the wheel).
