## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **The angel death, fourth try.** V1.3.19's log showed where the death that plays comes from: Crash's damage script keeps it in a setting (deathClip). After Papu's spin kills you, that setting holds Papu's own "Death_Common_Spin"; after the client's death, it holds a falling one. Now, during the client's death for a red landing, that setting briefly says Papu's death right before the game builds the death, and then goes back to how it was. The log says whether the death that got built was the angel's kind.
- **`/aku_give`** (test command): gives Crash an Aku Aku mask right away, also in boss fights. Each use adds one more, up to three. Use it to let Papu's spin hit you with a mask, so the log shows how the game takes a mask.
- **Health bar research:** V1.3.19's clip shows the frame and notches grew to 10, but the blue fill and the dark track behind it stop halfway, with the world showing through the rest. The log now lists every on-screen box the bar is made of (to find the fill's) and when the client changes Papu's notch count compared to when his health script starts.
- From V1.3.13–19: `/skip_filler yes`, one Aku Aku Mask item in boss fights, no more standing on Papu after a red landing, a red landing without a mask is a death for Crash, his last hit always counts, his spins keep the game's length, and he stays red through the flinch.
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
