## Stable build

This is the **Stable build**, for playing seeds: everything finished so far. What's still being worked on (more of Harder Bosses) is only in the **Testing build**, the other release on this page.

Updating in the middle of a seed is fine: the client fixes work right away. The logic fixes change seeds generated with this release's `crash_nst.apworld`. A seed already running keeps its item placement, but since V1.3.4 the client shows the checks behind obstacles found since as needing their move (F10 still gives normal controls where a move is missing).

## What's new in V1.3.5

- **Tomb Wader needs Spin** (Eric): right at the start, its first door only opens when you spin the wheel next to it. Logic expects Spin for everything there in new seeds, and on a running seed the client shows Tomb Wader's checks as needing Spin.

## From V1.3.4

- No free Aku Aku mask after deaths in a row with Aku Aku Sanity; a DeathLink during the box count at a level's end misses you; Un-Bearable strikes on foot, and one waiting on a ride when a level ends gets you in the next level on foot; boxes behind obstacles found since your seed was made no longer show as available (Jungle Rollers' crate wall, Piston It Away, High Time's bonus round, stacks three high, crates under iron-bound ones, Sunset Vista's Cortex bonus round); Sunset Vista's Cortex bonus round needs Spin (new seeds).

## From V1.3.3

- The death counter is shared through the room, so every client shows the same numbers, including deaths while one of you was offline.

## From V1.3.2

- Boxes count again after the warp room or a Continue; no white screen after a game over (DeathLink); DeathLinks on foot in Hang Eight, Air Crash and Plant Food; the log says why a DeathLink or a trap waits; colors fixed when the saved Crash.pak was colored, and `/colors random`; box logic fixes for new seeds.

## From V1.3.1

- Harder Papu Papu in the **Bosses** options (`harder_bosses`, `boss_health`, `boss_speed`, `harder_bosses_aku_aku`), one Aku Aku Mask item per boss fight, and `/skip_filler yes`.

## From V1.3

- The fix for the game closing right at start.
- Colors as a plain list on the options page; glove and eyebrow colors; Crash's colors stay for the whole seed.
- Better box logic in Crash 3, and Aku Aku Mask filler that stacks like a crate's.
