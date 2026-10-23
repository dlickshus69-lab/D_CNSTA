## What's new

- **Nitro research, step 3: why a trap's Nitro counts as a box.** A trap's Nitro adds one to the level's box counter when it explodes. Dying resets it, but a Nitro set off another way would leave the count one too high. This build writes to the log:
  - every setting of the trap Nitro's scripts, once per game start
  - where each message the Nitro sends goes (and what it carries)

  That shows which part does the counting, so the next build can switch it off for trap Nitros only, without touching real Nitro crates.
