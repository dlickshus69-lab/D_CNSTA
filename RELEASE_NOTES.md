## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Health bar: the blue stretched the way your V1.3.24 test showed.** Stretching the bar's blue picture sideways made it reach the end of the bar, and it still went down one step per hit. That's now done automatically, and it goes into the bar's setting before Papu's bar is made, so the bar should be full from the start of the fight. The game only redraws the bar at a fight's start, a hit or a death, so a change shows from then on. `/bar_try none` turns the stretch off to compare, `/bar_try scale` is the old whole-bar test, and `/bar_try off` ends a test.
- **The angel death, eighth try.** V1.3.24's log showed what the game's hit message carries: only the name of the death (`Death_Common_Spin`) and a yes/no. V1.3.24 tried to resend Papu's whole hit, but the damage in it is made fresh for each hit and gone right after, so it never got sent. Now a red landing without a mask sends the game's hit message with Papu's death filled in, so his spin no longer has to hit Crash first. If it doesn't kill Crash, he gets the falling death instead, and the log says which death happened.
- From V1.3.13–24: a red landing with a mask sends the game's own "Aku takes the hit" message, `/aku_give` and `/skip_filler yes`, one Aku Aku Mask item in boss fights, his last hit always counts, his spins keep the game's length, he stays red through the flinch, and the bar grows by default (`/boss_bar keep` keeps the game's 5 notches).
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
