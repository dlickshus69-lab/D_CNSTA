## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Papu Papu, round 7** (`/boss_rules on`): landing on him while he's red is now a real hit for Crash. With Aku Aku, Crash loses the mask; without one, he loses a life, just like getting hit by Papu's spin. V1.3.13's log showed the client's copy of the game's hit message did nothing: it carries Papu's attack damage, which only exists while his attack is touching Crash.
  - He **stays red** while he flinches from a landing that didn't count. Before, the red went away as you landed, so it looked like a normal hit.
- From V1.3.13: **`/skip_filler yes`** (every 1-Up, Wumpa Fruit and Aku Aku Mask received so far counts as given), **one Aku Aku Mask in boss fights**, Papu Papu's last hit always counts (no more getting stuck), and his spins keep the game's length with more health.
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
