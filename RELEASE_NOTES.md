## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Ripper Roo's YAML options** (need `harder_bosses`): `ripper_roo_health` (3–9 hits, default 6), `ripper_roo_speed` (100–200%, default 170), `ripper_roo_pattern` (first loop 3–7 platforms, default 3, or `games_own`), `ripper_roo_attack_chance` (0–50% of his landings, default 15), `ripper_roo_leap` (on), `ripper_roo_nitros` (0–4 at the start, default 2), `ripper_roo_most_nitros` (0–4, default 3), `ripper_roo_nitro_size` (100–200%, default 200). Each one explains itself in the YAML template and the player guide.
- **Nitros from the YAML, no test command:** connect with a seed that has Nitros, and the client makes his arena's changed file in the background, then asks you to restart the game once.
- Older seeds play as V1.4.23 did (health and speed from `boss_health` and `boss_speed`).
- To try: install this apworld, make a Testing seed with `harder_bosses: true` and some Roo options changed, connect, restart when asked, and fight him (`/boss ripper1`). Send the log.
