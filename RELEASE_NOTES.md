## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **The wall is up before the fight starts:** finding Crash's physics runs at full speed again while the intro plays (V1.3.72's slowdown made it take 5 s, long enough to get onto the throne first), and the wall's line is known from the intro.
- **Papu's facing stays on his aim:** when the wall's smoothing tried a new spot, it could knock out his aim for a slam (his spin started up to 73° off). That's fixed, and his aim sets itself up again at his next slam if it ever drops.
- **Smoothing settles in one try:** it tries the spot that worked in your last two runs first.
- **No freeze after a respawn:** finding Crash's physics runs at the lowest priority and looks where it was first, and a full search during the fight takes breaks as it goes.
- His hit window is 2.2 s at 200% (4.4 s at 100%).
