## What's new

- **Test build for Harder bosses.** This build doesn't change the bosses yet. It adds what's needed to study them:
  - **`/boss name`** (test command): the next level entrance you go through loads that boss fight instead. Names: papu, ripper1, koala, pinstripe, brio, cortex1, ripper2, komodo, tiny2, ngin2, cortex2, tiny3, dingodile, tropy, ngin3, cortex3. `/boss off` turns it off. Once the fight loads, entrances go back to normal. **Use a test seed**: the game saves a boss as beaten.
  - **Boss notes in the log**: during a boss fight, the client writes the boss's settings to the log (what counts the hits, the phases, the timers), then every change while you fight. It only reads; nothing about the fight changes.
