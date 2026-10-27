## Stable build

This is the **Stable build**, for playing seeds: everything finished so far. What's still being worked on (more of Harder Bosses) is only in the **Testing build**, the other release on this page.

Updating in the middle of a seed is fine: the client fixes work right away. The new options and logic only change seeds generated with this release's `crash_nst.apworld`.

## What's new in V1.5

- **Harder Ripper Roo** (`harder_bosses`): he hops around random loops of platforms that get one platform longer after each hit, never landing on the same one twice in a loop, faster than the game's. Now and then he breaks off to hop right at you, even leaping over a platform to reach you.
- **Nitros on his platforms:** big crates you can't stand next to, moved after every hit (a green outline shows each new spot first). Only you set them off: he goes through them, and his TNT doesn't. They're always placed so you're never cornered. The first time you connect with them on, the client prepares his arena and asks you to restart the game once.
- **New Ripper Roo options:** `ripper_roo_health` (3 to 9 hits), `ripper_roo_speed`, `ripper_roo_pattern`, `ripper_roo_attack_chance`, `ripper_roo_leap`, `ripper_roo_nitros`, `ripper_roo_most_nitros` and `ripper_roo_nitro_size`. The YAML template and the player guide explain each one.
- Papu Papu is as in V1.4: Boss Health and Boss Speed still set him, and Ripper Roo can follow them (`by_boss_health`, `by_boss_speed`).
