## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Game over crash fixed (I think):** after a game over the client could make a Nitro from the old fight's leftovers. It now takes the Nitro straight from the current fight, which also makes them appear sooner.
- **Roo goes through the Nitros:** his patterns use every platform again, and a Nitro he lands on doesn't go off. Only you touching one does.
- **More Nitros as you hit him:** 2 to start, 3 after your 2nd hit, 4 after your 4th. At least 3 platforms always stay clear.
- **Roo is a bit faster at high Boss Speed:** 1.5x at 200% (was 1.35x).
- To try: test 3 still on, `/boss ripper1` with harder bosses on, and get a game over once. Send the log and a clip.
