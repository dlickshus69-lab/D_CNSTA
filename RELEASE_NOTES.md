## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Koala Kong's rally works start to finish (V1.5.8's run):** 2 and 3 swats, every spin of yours caught, and a final spin that took his health from 800 to 700. These changes fix what made it look and play badly:
- **The swat comes at him, not halfway:** the rock now flies until it's just outside where it would hit him (300 out to start, then 50 outside the spot where your last hit landed, which the client measures). He swats it back from there, with the gold flash.
- **Slower comebacks:** the swatted rock comes back at 900 a second, then 1050, then 1200 (before: 1000 to 1400), and lands a little closer to you. It still waits for your spin (up to 5 seconds).
- **No more rallies dying:** the rock's flight only lasts about 2 seconds, and that cut off a 3-swat rally. The game also retired the rock 0.9 s after a spin, so the final hit did nothing. Now the flight's timers are held for the whole rally, and the rock's retire message is kept from it. To try: `/invincible on`, then `/boss koala`, play a few full rallies, and send the log and the clip.
