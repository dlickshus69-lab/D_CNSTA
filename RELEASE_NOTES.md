## Stable build

This is the **Stable build**, for playing seeds: everything finished so far. What's still being worked on (more of Harder Bosses) is only in the **Testing build**, the other release on this page.

Updating in the middle of a seed is fine: the client fixes work right away. The logic fixes only change seeds generated with this release's `crash_nst.apworld`; a seed already running keeps its logic (F10 still gives normal controls where a move is missing).

## What's new in V1.3.2

- **No more white screen after a game over (DeathLink).** DeathLinks that came while you were at the game over screen were kept, and all struck as the level opened again, which left the screen white until you restarted the level. Now the ones that come at the game over screen are dropped (Crash is already out), and a DeathLink only strikes once the level is open: Crash able to move for 1.5 seconds with the level's HUD on screen.
- **DeathLink in Hang Eight, Air Crash and Plant Food:** these levels mix jet-board parts with parts on foot, and the whole level counted as a ride, so a DeathLink waited ("on a ride") even while walking. Now it strikes on foot as anywhere else, and waits only while Crash is on the board.
- **Crash colors that only partly took are fixed.** If `CrashNSTAP\skins` was lost while Crash wore an older seed's colors, the client kept that colored Crash as the game's own and colored on top of it. Now it checks the copy it colors from: if it already has colors in it, the console says so, and verifying the game's files in Steam fixes it (the colors are made again at the next connection, then show after a restart). Everyone's colored Crash is made once more with this update, so restart the game when the console says so.
- **`/colors random`**: random colors for this seed, kept until `/colors seed` (back to your YAML's colors) or a new seed. They show from the next game start.
- **Box logic fixes** (new seeds, Ability Shuffle):
  - Piston It Away needs Slide to finish.
  - High Time's bonus round needs Crouch Jump (a stack of four iron crates is in the way).
  - Jungle Rollers needs Spin for its crate wall after the first checkpoint, so for the clear, the boxes past the wall and the bonus round.
  - Every crate in a stack three high needs Spin (was four).
  - A crate right under an iron-bound crate needs Spin (The Pits' bonus round).
  - A full pass over every level's boxes is coming in the next Stable build.

## From V1.3.1

- Harder Papu Papu in the **Bosses** options (`harder_bosses`, `boss_health`, `boss_speed`, `harder_bosses_aku_aku`), one Aku Aku Mask item per boss fight, and `/skip_filler yes`.

## From V1.3

- The fix for the game closing right at start.
- Colors as a plain list on the options page; glove and eyebrow colors; Crash's colors stay for the whole seed.
- Better box logic in Crash 3, and Aku Aku Mask filler that stacks like a crate's.
