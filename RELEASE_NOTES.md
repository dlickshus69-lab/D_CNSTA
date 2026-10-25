## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Papu Papu stays red after every hit** until the slam after his next spin (some hits ended it as his spin started).
- **His hit window goes by speed:** 2 s at 200%, up to 4 s at 100%. `/boss_window` sets the 200% time.
- **His faster slam from his first stand** of a fight (it took two stands before).
- From V1.3.57: his slam paced by boss speed (0.9 s after he stands at 100%, 0.45 s at 200%).
- From V1.3.52: his spin starts where he slammed.
