## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Papu's invisible wall is drawn in the fight:** red where it pushes Crash back, yellow where it only steers the stick. F7 hides it with the other markers.
- **Stuck report in the log:** when the stick pushes Crash but he barely moves near the wall, the log says where he is, where the stick pointed and how much of it the wall took out.
- **Corners** (from V1.3.78): the wall's rounded ends only steer the stick; landing in a corner doesn't push Crash.
- **Crash fixed** (from V1.3.76), and crash reports in the log (from V1.3.75).
- The stick's push into the wall fades out as Crash comes up to it; no stutter after a respawn; the wall is up before the fight; Papu's facing stays on his aim; his hit window is 2.2 s at 200%.
