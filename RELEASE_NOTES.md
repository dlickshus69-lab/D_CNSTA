## What's new

- **Nitro Trap in Crash 1, second fix.** The first Nitro in Fumbling in the Dark worked, but the second one said "no Nitro template loaded". One of the places in memory the client had found pointing at the Nitro was temporary and got reused. Now the borrowed crate spawner's own link to what it makes points at the Nitro just for the spawn and goes straight back, so only the Nitro itself is needed, and that stays put in the level. If the Nitro ever can't be found again, the client looks again.
- **Less in the log:** spinning near a trap's Nitro no longer adds a "Trap Nitro ..." line on every spin. Each kind of event is logged once per Nitro.
