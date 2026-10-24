## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **The angel death, ninth try.** V1.3.25's log showed the hit message's yes/no comes from a different setting (`isCrashHit_metaField`) than the one V1.3.25 filled in, so Crash's damage script ignored it and the falling death took over. Now every setting the message reads is filled in: Papu's death name and the yes.
- **Research for a red landing that only bounces Crash off** (asked for): the log lists Papu's health, deflect and AI messages around each landing, and which settings they read, to find how the game bounces Crash off Papu without hurting him. Nothing in the fight changes from this yet.
- From V1.3.25: the health bar's blue is full from the start of the fight and goes down a step per hit (confirmed). With more than 9 hits the bar can't get longer, so the steps don't line up exactly with the notch marks yet.
- From V1.3.13–24: a red landing with a mask sends the game's own "Aku takes the hit" message, `/aku_give` and `/skip_filler yes`, one Aku Aku Mask item in boss fights, his last hit always counts, his spins keep the game's length, he stays red through the flinch, and the bar grows by default (`/boss_bar keep` keeps the game's 5 notches).
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
