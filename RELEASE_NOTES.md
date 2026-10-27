## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Koala Kong's swat: the turned rock lands (V1.5.6's run).** It turned and stopped in front of Crash every time, but you couldn't spin it: the spinnable rock only comes with the game's own landing, and after about 2.5 s it sank.
- **Now your spin counts:** spin within reach of where the turned rock stopped (in the 2.5 s before it sinks), and the rock gets the same "spun back" message the game sends, so it flies at Koala again. Then he swats it again or it hits him, by swats left. This needs your seed's movement hooks (Ability Shuffle or Crashsanity styles). The log says if it can't see your spins.
- Rock colors and his gold flash as before. His 8-notch bar unchanged.
- To try: `/invincible on`, then `/boss koala`. After his swat, run up to where the rock stops and spin it. Send the log and the clip.
