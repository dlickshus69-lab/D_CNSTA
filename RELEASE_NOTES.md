## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **His chair, third try:** while you stand still at a fight's start, the client finds every copy of where Crash is in the game's memory, and the wall in front of the chair moves all the ones that follow him. The log says if it holds.
- **The rare slow slam fixed:** the speed-up threw out the right timing when it found it late.
- **His slam aims 70% of the way** to where you'll be. `/boss_lead 0.5` tries another (0 = where you are, 1 = all the way).
- **His hit window is back to 1.5 s at 200%** (3 s at 100%), over once his next spin starts.
- From V1.3.64: 2 to 5 slams after a hit.
