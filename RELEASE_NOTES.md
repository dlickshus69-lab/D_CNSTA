## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **The real Nitro in Ripper Roo's fight (a test):** type `/roo_nitro on`, wait for "Roo Nitro: on" (a minute or so), then restart the game. His arena then gets N. Sanity Beach's crate files, and with harder bosses on, a real Nitro sits on two of his platforms.
- What to look for: does Roo go through them, does Crash die on touch, and do any beach crates show up somewhere in his arena. Send the log and a clip either way.
- `/roo_nitro off` and a restart put his own files back exactly. If his fight won't load at all, do that from the hub.
- **Before going back to the Stable build,** run `/roo_nitro off` and restart once on Testing. Stable doesn't know to put his files back.
- Same as V1.4.8 otherwise: patterns that grow with each hit, a shorter wait at high Boss Speed, and a hop onto you 1 landing in 5.
