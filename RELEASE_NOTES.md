## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **No more constant outline:** the green box around every Nitro is gone. Outlines show only where a Nitro is about to appear, or while Roo is on one.
- **Bigger Nitros, third try:** doubling the Nitro's physics body didn't change how it's drawn. This build first looks at Roo's Big TNT, the game's own big crate, to learn where its size is kept. Then it tries each place a Nitro's size might come from, and puts back any that don't work.
- **The hitbox works:** last run, every time you walked onto a Nitro's platform it went off and you died. That stays the same.
- To try: test 3 still on, `/boss ripper1` with harder bosses on. Say whether the Nitros look big. Either way, send the log: it lists every place tried and what each did.
