## What's new

- **Crash colors: no more orange at the corner of Crash's grin.** His lips, the band along his mouth and its pink corners, now take a dark shade of your chest color. Before, they kept the game's red, which looked orange in the game's light. The inside of his mouth stays red. If you wear colors, the client makes your colored Crash again the next time you connect. Restart the game once afterward. No new seed needed.
- **Nitro research, step 2.** Every level's archive holds the Nitro crate's files, Crash 1's included, so the next question is whether the game keeps a Nitro ready in memory there.
  - `/nitro_probe` now also searches the game's memory for the Nitro's description and template, without the client's own lists getting in the way.
  - **New test:** `/nitro_drop any` drops a Nitro made by an ordinary crate's spawner instead of a Nitro's. In a level without Nitros, it first looks for the Nitro in memory during the countdown.
- **Colors research:** `/nitro_probe` also lists every archive that holds a copy of Crash's fur, clothes and hair textures, and which copies are loaded. This is for Fumbling in the Dark, where his colors only partly show.
