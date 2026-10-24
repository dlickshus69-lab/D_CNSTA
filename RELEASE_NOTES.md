## What's new

- **Glove and eyebrow colors** (new seeds). Two new YAML options in the Cosmetic section: `crash_gloves_color` and `crash_eyebrows_color`. They take the same colors as the others: a color name or `"#RRGGBB"`.
- **Crash's colors stay for the whole seed.** They come only from the YAML. `/colors` shows them but can't change them; only a new seed does. `/skin default` puts Crash back in the seed's colors. Connecting to a seed puts on that seed's colors, so a seed where you changed colors with `/colors` goes back to its YAML colors.
- **Boss test, round 2.** The log now also shows the bosses' shared scripts: the one that counts hits, and the one that bounces Crash off a boss. Things a boss throws or spawns (mine carts, TNT, rocks) are logged once instead of every time. `/boss name` works as before (use a test seed).
