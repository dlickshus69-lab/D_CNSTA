## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Papu Papu, round 5** (`/boss_rules on`):
  - Landing on him while he's red: the hit is **kept from happening**. The messages his scripts send when he's hit are held back, so he shouldn't flinch or lose health, and Crash isn't bounced off.
  - Crash takes **the game's own hit** instead. V1.3.10 sent it with the values of another copy of that message, so Crash's script ignored it.
  - If the hit still gets through, his health goes back (found in V1.3.10: his health part keeps the damage taken), and so do his spin length and hit count. That's what made his spins so long after a hit while red.
  - A good test is without three masks: with three, Crash is invincible and a hit does nothing (the game's rule).
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
