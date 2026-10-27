## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Fixed the crash after a game over in Roo's fight.** A game over reloads his whole level, and the client kept using the old fight's leftovers. It now starts his fight over from scratch, Nitros included.
- **The TNT shouldn't set off the Nitros anymore:** a Nitro only goes off when you touch it. A blast nearby (or Roo, if he ever lands on one) is kept from setting it off.
- To try: test 3 still on, `/boss ripper1` with harder bosses on, light a TNT next to a Nitro, and once get a game over and fight him again. Send the log and a clip.
- Same as V1.4.14 otherwise: Nitros there as the fight starts, moving after each hit with a green outline first, and Roo keeping off them.
