## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Landing on red Papu Papu with a mask: Aku Aku takes the hit.** V1.3.20's log caught the game doing it when Papu's spin hit a masked Crash: Crash's damage script sends Aku Aku one message, and Aku does the rest (the mask goes, Crash blinks). The client now sends that same message from the same place.
- **The angel death, fifth try.** V1.3.20's log showed Crash's death setting is still empty at the moment the client was waiting for. The guess now is that the game fills in its own death only when the setting is empty, so Papu's spin death goes in while it's empty. The log says what the setting held and whether the death built was the angel's kind.
- **`/boss_bar keep` or `/boss_bar grow`** (test, from the next fight). `keep` (the default): the game's 5 notches, full at the start, each hit takes half a notch. This works. `grow`: 10 notches, but the fill still stops at the old length; that's still being worked on.
- From V1.3.13–20: `/aku_give` and `/skip_filler yes`, one Aku Aku Mask item in boss fights, no more standing on Papu after a red landing, his last hit always counts, his spins keep the game's length, and he stays red through the flinch.
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
