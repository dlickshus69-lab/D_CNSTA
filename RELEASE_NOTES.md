## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **The angel death, sixth try.** V1.3.21's log: the falling death writes its own death into Crash's death setting (it overwrote Papu's), and sometimes it sends the death with its own death text built into the message. So now, while the client's death runs for a red landing, every "Death..." text in Crash's damage script says Papu's spin death, and they go back right after. The log lists which texts were changed and whether the death built was the angel's kind.
- **Health bar research, bigger:** the bar grows by default now (`/boss_bar grow`; `/boss_bar keep` keeps the game's 5 notches, which works). The log lists the bar's pieces by name with their numbers, and which numbers change when Papu takes a real hit (and never otherwise). Every other boss's bar is listed too, so a boss whose bar fits more hits, like N. Gin's, can be compared with Papu's.
- From V1.3.13–21: a red landing with a mask sends the game's own "Aku takes the hit" message, `/aku_give` and `/skip_filler yes`, one Aku Aku Mask item in boss fights, his last hit always counts, his spins keep the game's length, and he stays red through the flinch.
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
