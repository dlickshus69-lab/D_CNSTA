## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Bigger Nitros, second try:** last build's 2x look only stuck in the pause menu, because the game redrew the Nitros at normal size every frame. This build makes the Nitro's physics body twice the size, and the game copies that to the Nitro's look itself.
- **If the bigger look still doesn't stick,** the client notices within a second, keeps the Nitros normal size, and draws the big green box around each one so you can see the hitbox.
- **Fixed the Nitro that "clips onto me but doesn't do anything":** a Nitro that already went off was being put back and reused. Now a new one comes in its place 3 s later.
- To try: test 3 still on, `/boss ripper1` with harder bosses on. Check whether the Nitros look big while playing (not just paused), and walk onto a Nitro's platform edge. Send the log and a clip.
