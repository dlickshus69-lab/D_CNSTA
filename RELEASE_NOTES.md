## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses, and the glove and eyebrow colors). For playing a seed, the **Stable build** is the other release on this page.

## What's new

- **Boss health bar** (`/boss_health`): the bar keeps the boss's own notches and starts full; with `200`, each hit takes half a notch. Before, the bar grew to twice the notches but its fill still counted the old hits, so it started half full and emptied a hit before the boss went down. The log also notes what the bar does, in case it's still off.
- **Harder bosses, round 2 (test commands).**
  - `/boss_speed` takes effect right away, also in a fight that's already going. Before, changing it in a fight did nothing until the next one.
  - `/boss_rules on`: Papu Papu glows red while he spins (the glow is on his model now). Landing on him then is a hit for Crash, the way a boss hit is: a mask with Aku Aku (none if he's invincible), a life without one. It no longer takes a life straight away. The hit on Papu Papu doesn't count.
  - Experiment: with `/boss_rules on`, his spin turns the other way now and then.
  - The log notes the messages the boss and Crash get in a boss fight, to find the game's own hit (with its hurt animation) for the next round.
