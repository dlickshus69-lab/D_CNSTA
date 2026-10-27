## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Koala Kong's rally, second try:** the first try sent the rock's own throw while it flew, and that did nothing. Now his swat is his own throw (the move he uses to throw the boulder at you), fired as your spun rock gets close to him. The rock still flying at him is moved away so it can't land the hit, and if it hits anyway his health goes back.
- **Koala Kong's bar, all 8 notches (try):** your last run showed the bar's fill follows the hit count, not the notches it was built with. So the bar's frame is now laid out for his 8 hits before his first hit. If that works, the bar shows 8 notches from his first hit on (7 left). If the game puts its 4-notch frame back, the bar goes back to 2 hits a notch.
- Gold flash on him at each swat and the rock's color by swats left (red 3, orange 2, yellow 1, green: the next spin hits him) are the same as before.
- To try: `/invincible on`, then `/boss koala`. Spin his big rocks back, and please record from his first big rock (the swat happens right as the rock reaches him). Send the log and the clip.
