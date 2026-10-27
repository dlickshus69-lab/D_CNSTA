## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Roo a bit faster:** 1.7x at 200% Boss Speed (was 1.5x). His hops take 0.34 s, and his pause before each one 0.41 s.
- **Nitros move after every hit:** the back-middle one in your clip stayed put because it kept scoring as the best spot. Now a Nitro only stays where it was if there's truly no other place that keeps the rules.
- **He can leap over a platform to attack you:** when he goes for you and you're two platforms away in a straight line, he can jump over the one in between. A Nitro on the platform he jumps over stays a Nitro.
- To try: test 3 still on, `/boss ripper1` with harder bosses on. Watch for his long leap at you (the log says "over ..." when it happens) and check that the speed feels right. Send the log and a clip.
