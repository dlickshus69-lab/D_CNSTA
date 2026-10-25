## Stable build

This is the **Stable build**, for playing seeds: everything finished so far. What's still being worked on (more of Harder Bosses) is only in the **Testing build**, the other release on this page.

Updating in the middle of a seed is fine: the client fixes work right away. The logic fixes change seeds generated with this release's `crash_nst.apworld`. A seed already running keeps its item placement, but since V1.3.4 the client shows the checks behind obstacles found since as needing their move (F10 still gives normal controls where a move is missing).

## What's new in V1.3.4

- **No free Aku Aku mask after deaths in a row with Aku Aku Sanity** (asked for). The game hands you a mask after dying a few times in a row; with Aku Aku Sanity on, that stays off, Aku Aku item or not. Masks still come from Aku Aku crates once you have the item, and from Aku Aku Mask items. The log says `Aku Aku Sanity: kept the free mask ...`.
- **A DeathLink during the box count at a level's end misses you** (asked for). On "...but you missed N boxes" and "Press Enter to continue", the level is already won, so a DeathLink then is dropped instead of killing Crash and costing a life. The console says so. Traps and Aku Aku Mask items wait for the next level then too.
- **Un-Bearable: DeathLink strikes on foot** (asked for). Running from the giant polar bear, a DeathLink gets you; it used to wait the whole level as "on a ride". On the polar bear it still waits until you're on foot. If a level ends while one is waiting on a ride (the polar bear, the Crash 2 jet board), it now gets you in the next level you play on foot instead of being dropped. Taking just a life on a ride still isn't possible: the game's lives reward only runs while Crash is on foot.
- **Boxes behind obstacles found since your seed was made no longer show as available** (relaxed Ability Logic, works with your current seed). The client applies the newest move fixes to the seed's own logic: Jungle Rollers' crate wall, Piston It Away, High Time's bonus round, stacks three high, crates under iron-bound ones, and Sunset Vista's Cortex bonus round. Checks behind them show up once you have the move. The items in them stay where the seed put them.
- **Sunset Vista's Cortex bonus round needs Spin** (new seeds): right after its five TNT crates, a stack of three crates fills the way.

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
