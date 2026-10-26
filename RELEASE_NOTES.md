## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **No more going through the wall:** the wall now follows Crash's physics itself, not only where he's drawn, so nothing can hide him walking through it (V1.3.71's smoothing could, and he ended up out of the arena).
- **Smoother at the wall:** the smoothing only keeps a spot when all of Crash's physics stays at the wall with it, and switches itself off if he ever gets deep past it.
- **Papu's facing stays steady:** while Crash presses into the wall, his slam's aim no longer leads him into it.
- **No freeze after a respawn:** finding Crash's physics again looks where it was first, at low priority, instead of reading all the game's memory at once.
- His hit window is 2.2 s at 200% (4.4 s at 100%).
