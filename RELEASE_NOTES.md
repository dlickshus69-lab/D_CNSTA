## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **No stutter at the wall:** the moment the game's physics moves Crash into the wall, it's set back before the frame is drawn. The client tries his physics' places in turn and keeps the one that works; the log says which.
- **His hit window is 2.2 s at 200%** (4.4 s at 100%).
- From V1.3.70: the wall is one straight line across the hut, in front of the throne.
- His slam is the same speed all fight; only his spin speeds up per hit, capped since V1.3.36.
- From V1.3.64: 2 to 5 slams after a hit.
