## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **`/skip_filler yes`** (new command): every 1-Up, Wumpa Fruit and Aku Aku Mask you've received so far counts as already given, so none of them comes. Useful after a seed's goal is done, when every item arrives at once. Ones received later still come as usual. It can't be undone for that seed.
- **Aku Aku Masks in boss fights: one is enough.** A mask item comes during a boss fight only while Crash has none, and only one per fight. The rest wait for the next level. (V1.3.12's log: 57 masks came, one every 3 seconds, so Crash had three masks and was invincible the whole fight. That's why landing on red Papu Papu never hurt him.)
- **Papu Papu, round 6** (`/boss_rules on`, `/boss_health 200`):
  - **Fix: stuck after beating him.** If the last hit landed while he was red, it was taken back after he had already gone down, so the fight never ended. The last hit now always counts.
  - Landing on him while he's red gives Crash **one** hit (V1.3.12 sent two for the same landing).
  - **Spins don't get long with more health.** His spin length and hit count now go up at the game's pace, spread over all his hits, so his longest spin is the game's (6 laps, not 11) and his hit count no longer starts over after his 5th hit.
- **Fix (V1.3.12): the game closed right at start** for some players. Every build is now checked for it.
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
