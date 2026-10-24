## Stable build

This is the **Stable build**, for playing seeds: everything finished so far, plus what's in ongoing testing (levels and boxes that need a move, secret exits and seeds that always generate). Harder bosses, still being worked on, is only in the **Testing build**, the other release on this page.

## Latest changes in this version

- **Glove and eyebrow colors** (new seeds). Two new YAML options in the Cosmetic section: `crash_gloves_color` and `crash_eyebrows_color`, with the same colors as the others (a color name or `"#RRGGBB"`).
- **Crash's colors stay for the whole seed.** They come only from the YAML: `/colors` shows them but can't change them; only a new seed does. `/skin default` puts Crash back in the seed's colors. Connecting to a seed puts on that seed's colors.
- **Better box logic in Crash 3** (new seeds). Boxes, gems and secret exits in Crash 3 levels no longer expect the Crash 3 powers (Double Jump, Super Body Slam and so on). Relics (time trials) still expect them.
- **Aku Aku Mask filler: the second and third masks work like a crate's.** A mask on top of a mask makes Aku gold, and a third gives the invincibility.
