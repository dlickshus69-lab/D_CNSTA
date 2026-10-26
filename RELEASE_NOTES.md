## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Crash fixed:** after a respawn, Papu's wall could write into game memory that no longer held Crash's position, and the game crashed later; now it only touches memory that still holds where Crash is, and drops any that doesn't.
- **Crash reports in the log** (from V1.3.75): if the game crashes, the log says where, so it can be tracked down from one log.
- **From V1.3.74:** as Crash comes up to Papu's wall, the stick's push into it fades out, so he stops at it or runs along it instead of flipping into his falling animation.
- No stutter after a respawn; the wall is up before the fight starts, and Papu's facing stays on his aim.
- His hit window is 2.2 s at 200% (4.4 s at 100%).
