## Stable build

This is the **Stable build**, for playing seeds: everything finished so far. What's still being worked on (more of Harder Bosses) is only in the **Testing build**, the other release on this page.

To get the new options, install this release's `crash_nst.apworld` in place of the old one. Older YAMLs and seeds still work: without the new options, Papu Papu is as the game has him.

## What's new in V1.3.1

- **Harder Papu Papu**, in a new **Bosses** section of the options. For now only Papu Papu (Crash 1's first boss); every other boss stays as the game has him.
  - `harder_bosses` (default off) turns on his red rule. He glows red at the start of the fight and after each hit, until the slam that follows his next spin. Landing on him while he's red doesn't count as a hit: Crash bounces off and gets hurt (an Aku Aku mask takes it, or Crash dies).
  - `boss_health` (100 to 200, default 150): one more hit every 25%. That's 5 hits at 100, 6 at 125, 7 at 150, 8 at 175 and 9 at 200, and his health bar gets a notch for each.
  - `boss_speed` (100 to 200, default 150): he spins faster and waits less before his spins. Each hit still speeds him up, more gently near the top, so the end stays beatable.
  - `harder_bosses_aku_aku` (default off): with it off, Crash has no Aku Aku mask in that fight (masks from crates and items there come in the next level). Turn it on to keep masks working as usual.
  - Health and speed only count with `harder_bosses` on.
  - Once he's beaten, his spin can't hurt Crash any more.
- **Aku Aku Mask items in boss fights: one is enough.** In any boss fight, a mask item comes only while Crash has no mask, one per fight; the rest wait for the next level.
- **`/skip_filler yes`**: after a seed's goal is done (when every item arrives at once), it counts every 1-Up, Wumpa Fruit and Aku Aku Mask received so far as given, so none of them comes. Ones received later come as usual.

## From V1.3

- The fix for the game closing right at start.
- Colors as a plain list on the options page; glove and eyebrow colors; Crash's colors stay for the whole seed.
- Better box logic in Crash 3, and Aku Aku Mask filler that stacks like a crate's.
