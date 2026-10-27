## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Roo's Nitros are there when the fight starts:** both go down while he jumps in, so they're waiting even if you skip the intro. After a death they're back in new spots at once.
- **After each hit they move:** a green Nitro outline shows where each will land, and about 3 s later the Nitro appears and its outline goes. One won't land on you; its outline waits until you step off.
- **Roo keeps off the Nitro platforms:** his pattern is picked around them, so he doesn't set them off. You still have to work around them.
- To try: test 3 still on (`/roo_nitro test3` and a restart if not), then `/boss ripper1` with harder bosses on. Send the log and a clip.
- Same as before otherwise: patterns that grow with each hit, a shorter wait at high Boss Speed, a hop onto you now and then.
