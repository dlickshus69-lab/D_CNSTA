## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Found why Roo's fight went black with the Nitro tests:** my copy left out half of what N. Sanity Beach's crate files need, the Nitro's own model and explosion among them, so the game waited for them forever. Fixed: the copy now includes all of it.
- **Please run test 2 and test 3 again.** Each one is `/roo_nitro test2` (or `test3`), wait for "Roo Nitro: test N on", restart, then `/boss ripper1`. Use `/roo_nitro off` and a restart between them. Test 1 already loads, so it doesn't need another try.
- **Test 3 is the one that can place real Nitros on two of his platforms** (with harder bosses on). Test 2 only loads the Nitro without placing any, to see if that much works. The beach's camera is never loaded as part of his arena.
- **If either goes black,** use `/roo_nitro off` and a restart. Send the logs either way.
- Same as V1.4.8 otherwise: patterns that grow with each hit, a shorter wait at high Boss Speed, a hop onto you 1 landing in 5.
