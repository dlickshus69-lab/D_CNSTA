## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Papu Papu, round 8**:
  - **The angel death.** Landing on him while he's red with no mask now gives the death his spin gives (the angel flying up), not DeathLink's falling death. With a mask, Crash loses the mask, as before.
  - **His health bar grows to match** (`/boss_health`). At 200% the bar has 10 notches and starts full, and each hit takes one notch. V1.3.5 grew the bar but left his health at the game's (half full). Since V1.3.6 his health was right but the bar kept 5 notches. Now both.
  - Research for keeping a red landing from counting at all: the log shows what changes in his health part with each state.
- From V1.3.13–14: `/skip_filler yes`, one Aku Aku Mask in boss fights, a landing while red is a real hit for Crash, his last hit always counts, his spins keep the game's length, and he stays red through the flinch.
- **Options page** (from V1.3.10): the colors are a plain list (each color, or Random), no Crash Skin option.
