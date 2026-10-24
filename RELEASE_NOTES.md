## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page.

## What's new

- **Papu Papu, round 3** (`/boss_rules on`, `/boss_speed`, `/boss_health`):
  - Landing on him while he's red (spinning) is now **the game's own hit**, the one his spin gives: the hurt animation, Aku Aku takes it if he's there, a life if not, nothing while invincible.
  - That hit on him **doesn't count**: his health goes back (the log says how).
  - **Speed** now changes his real spin speed. Before, the settings changed were only read when the level loads, so nothing changed.
  - The spin-the-other-way experiment is out for now: it stopped his spin speed from going up. The log notes how fast and which way he turns, to try it another way.
- **Health bar** (from V1.3.7): the bar keeps the boss's own notches and starts full; with `/boss_health 200` each hit takes half a notch.
