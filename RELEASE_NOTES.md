## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Koala Kong's bar shows all 8 notches** (from V1.5.3, confirmed in your run): the frame is laid out for his 8 hits and goes down one notch a hit.
- **Koala Kong's swat, third try:** V1.5.3 never sent his throw, because the client couldn't look the throw up. Now the client keeps the throw exactly as the game sends it on his first boulder, and replays it as your spun rock gets close to him. The rest is as before: the rock flying at him is moved away, a gold flash on him, and the rock's color by swats left (red 3, orange 2, yellow 1, green: the next spin hits him).
- To try: `/invincible on`, then `/boss koala`. Record from his first big rock through your spin back, and send the log and the clip.
