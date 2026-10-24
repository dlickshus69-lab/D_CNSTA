## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Options page: the colors are a plain list** (each color, or Random), without the box to type a value into (no `"#RRGGBB"` colors any more), and the **Crash Skin option is gone** (skins are still picked with `/skin`). New seeds.

- **Papu Papu, round 4** (`/boss_rules on`):
  - **Red after each hit, for one spin cycle**: once he gets up from a hit, he's red until his next spin ends; then he can be hit again. Before any hit he's normal.
  - Landing on him while he's red: the game's own hit for Crash (V1.3.8 couldn't find it: Crash has more scripts than it looked through). The hit on him doesn't count; his health is looked for wider this time (V1.3.8 didn't find where the game keeps it), and the log says where.
- Speed: his spin speed number goes up with `/boss_speed`, but it doesn't make him look faster. Another way is next.
