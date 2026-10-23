## What's new

- **Nitro Trap in Crash 1, fixed.** In a level without Nitro crates, every Nitro Trap (and `/nitro_drop`) now starts looking for the game's Nitro in memory as its countdown begins. If the countdown ends first, the Nitro waits a few seconds for the search instead of giving up. Before, only `/nitro_drop any` or an AP Nitro Trap that was already waiting started the search, so a plain `/nitro_drop` in Fumbling in the Dark said "still looking for the Nitro in memory" and nothing fell.
