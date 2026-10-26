## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **No falling animation at the wall:** as Crash comes up to Papu's wall, the stick's push into it fades out, so he stops at it or runs along it with the game's own moves. Before, he was pulled back every frame, which flipped him between running and falling.
- **The pull-back is still there** for what the stick can't stop: a jump's speed, the d-pad and the keyboard.
- **No stutter after a respawn:** if the wall's smoothing loses track of Crash after a respawn, it finds him again within a second, and it re-arms itself if the game starts new threads.
- The wall is up before the fight starts, and Papu's facing stays on his aim.
- His hit window is 2.2 s at 200% (4.4 s at 100%).
