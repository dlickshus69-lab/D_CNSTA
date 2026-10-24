## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Fix: Crash standing on Papu Papu or going through him** after landing on him while he's red. Since V1.3.11 the client held back the messages Papu's scripts send when he's hit while red. That never stopped the hit (his health is put back instead), and it also held back Crash's bounce. It's off now.
- **Landing on red with a mask: no hit for now.** V1.3.14–18 set Crash's mask count one lower. The mask stayed on screen and Crash didn't flinch, and the next hit killed him. That's off until the game's own way of taking a mask is known. Without a mask it's still the falling death.
- **Research for the angel death and the masked hit:** Crash's damage and Aku Aku scripts' settings go to the log at the fight's start, after each death, and after each hit from Papu's spin. V1.3.18's log showed the angel death object is only there during a real death, so it can't be borrowed later.
- **Health bar research:** the bar snapshots now show whole numbers correctly (V1.3.18 showed them as 0.000000).
- From V1.3.13–18: `/skip_filler yes`, one Aku Aku Mask in boss fights, a landing while red without a mask is a death for Crash, his last hit always counts, his spins keep the game's length, he stays red through the flinch, and the bar has the right number of notches.
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
