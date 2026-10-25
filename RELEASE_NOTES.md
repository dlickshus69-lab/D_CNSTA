## Stable build

This is the **Stable build**, for playing seeds: everything finished so far. What's still being worked on (more of Harder Bosses) is only in the **Testing build**, the other release on this page.

Updating in the middle of a seed is fine: the client fixes work right away. The logic fixes only change seeds generated with this release's `crash_nst.apworld`; a seed already running keeps its logic (F10 still gives normal controls where a move is missing).

## What's new in V1.3.3

- **The death counter is the same for everyone, and counts deaths while you were offline** (asked for). Every client now writes each death it sees to the room's data on the server, keyed by the DeathLink's time and player, so a death several clients see counts once. Each client reads everyone's deaths back from there. So every client shows the same numbers, including deaths while one of you was offline, as long as someone was online. The first time each client connects with this build, its count so far goes along, and the shared count carries on from the highest one. DeathLinks strike the same way as before.

## From V1.3.2

- Boxes count again after the warp room or a Continue; no white screen after a game over (DeathLink); DeathLinks on foot in Hang Eight, Air Crash and Plant Food; the log says why a DeathLink or a trap waits; colors fixed when the saved Crash.pak was colored, and `/colors random`; box logic fixes for new seeds.

## From V1.3.1

- Harder Papu Papu in the **Bosses** options (`harder_bosses`, `boss_health`, `boss_speed`, `harder_bosses_aku_aku`), one Aku Aku Mask item per boss fight, and `/skip_filler yes`.

## From V1.3

- The fix for the game closing right at start.
- Colors as a plain list on the options page; glove and eyebrow colors; Crash's colors stay for the whole seed.
- Better box logic in Crash 3, and Aku Aku Mask filler that stacks like a crate's.
