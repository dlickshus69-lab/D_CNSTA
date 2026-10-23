## What's new

- **Crash colors.** Pick colors for Crash's fur, chest and jaw, pants, shoes and mohawk. They recolor the game's own textures and keep his shading. His gloves, eyebrows, eyes, soles and laces stay the same.
  - **YAML** (Cosmetic section, new seeds): `crash_fur_color`, `crash_chest_color`, `crash_pants_color`, `crash_shoes_color` and `crash_hair_color`. Each takes `default`, a color name or `"#RRGGBB"`. The names are red, dark_red, orange, yellow, gold, lime, green, dark_green, teal, cyan, light_blue, blue, navy, purple, violet, pink, magenta, brown, tan, white, light_gray, gray and black.
  - **In the client, any time:** `/colors fur black`, `/colors shoes #ffcc00`, `/colors reset`. `/colors` on its own lists your colors and every color name.
  - **How it works:** the client makes a colored copy of the game's own Crash.pak on your computer and wears it like a skin, starting the next time the game starts. The Crash 1 island map still shows his small orange map model.
- The skin swapper (dinput8.dll) now also compares the end of each Crash.pak, so it can tell one set of colors from another. Copy the new dinput8.dll along with the rest of the zip.
- Old Aku Aku research lines no longer fill the log.
