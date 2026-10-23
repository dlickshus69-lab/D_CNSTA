## What's new in 1.3

- **Nitro Trap in any level.** The Nitro Trap now strikes in every normal level of all three games, including Crash 1, whose levels have no Nitro crates of their own. It also strikes on the hog, the Crash 2 jet board and polar bear, and in the chase levels. In the scuba levels (Under Pressure, Deep Trouble) it only ever appears in front of Crash, floating in the water. It's off in Coco's jet ski levels and the other ride levels. Its Nitro never counts as a box.
  - The Nitro that appears ahead of Crash is a little closer (about 1.3 crates). If he's mid-jump, it appears in midair at his height and floats there, even on a jump in place. It no longer ends up inside walls.
  - The falling Nitro blows up where it lands, even when Crash gets out of the way, so get clear of the spot.
  - No more stutter while a Nitro Trap is on its way.
  - It's never lost. In a level where it's off, it waits for one where it works. If Crash dies during the countdown, the countdown pauses, sound and all, and goes on in step after he respawns (the same in the pause menu). If the level changes before the Nitro comes, it starts over in the next level where it works.
  - The "NITRO INCOMING!" countdown is sharp at any resolution: the game's own font is drawn at the size you see, in a box with a Nitro-green border.
- **Crash colors, in every level.** Set Crash's fur, chest and jaw, pants, shoes and mohawk with the YAML's `crash_*_color` options (a color name or #RRGGBB) or `/colors`. His shading is kept, and `bright_white` gives a brighter white. The colors also show in Lights Out, Fumbling in the Dark, the Crash 1 island map, and the bosses and cutscenes that have their own copy of Crash.
- **Aku Aku Mask filler.** With `aku_aku_mask_weight` (new seeds), a filler item gives Crash a mask, even with Aku Aku Sanity's lock on. It takes one hit like a crate's mask, and on top of a mask it gives a second one.
- **Death counter.** "Deaths: N" is always on screen when you play solo or with DeathLink off. In a group with DeathLink, it shows the total and each player's deaths.
- **Traps start once Crash can really move.** A trap now waits until Crash has been able to move for 1.5 seconds, so none starts while he's still arriving in a level or respawning.
- Logs are named after when the game started (`client-<date>_<time>.log`), and only the newest 10 are kept.
