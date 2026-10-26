## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **First:** if V1.4.9's Nitro left Roo's fight on a black screen, type `/roo_nitro off` in the hub and restart once. That puts his own files back.
- **Three tests to find what stopped his fight loading.** Run one, wait for "Roo Nitro: test N on", restart, then try his fight (`/boss ripper1`). Turn it off the same way before the next test.
- `/roo_nitro test1`: his archive with N. Sanity Beach's files added, nothing else changed (checks the copy itself). `test2`: their crate layer loaded as a plain file. `test3`: as a real layer, with the beach's camera file.
- Tell me which ones load and which go black, and send the logs. The log also lists the templates his fight makes things from, to try making the Nitro from his own TNT drop.
- Same as V1.4.8 otherwise: patterns that grow with each hit, a shorter wait at high Boss Speed, a hop onto you 1 landing in 5.
