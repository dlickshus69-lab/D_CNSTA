## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Roo's fight loads with the Nitro tests now** (V1.4.11, thanks). With test 3 the Nitro itself is in his arena, but none got placed yet.
- **The stray beach crates in his water are gone:** any of N. Sanity Beach's crates that lands inside his arena is moved far below it.
- **Finding out why no Nitro gets placed:** the log now says why the beach crates' spawners weren't used, and where his Big TNT comes from. That gives a second way to make the Nitro if the spawners can't be used.
- To try: `/roo_nitro test3` (if it isn't still on), restart, fight him with harder bosses on, and send the log.
- Same as V1.4.8 otherwise: patterns that grow with each hit, a shorter wait at high Boss Speed, a hop onto you 1 landing in 5.
