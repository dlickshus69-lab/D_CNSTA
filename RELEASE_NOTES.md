## What's new

- **Nitro Trap spawns again.** V1.2.66 picked a falling platform's spawner to make the Nitro, as intended, but a safety check only accepted a crate's kind of template, so nothing spawned ("the borrowed spawner's template differs"). The spawner copies whatever it's pointed at, so the check now accepts the game's other kinds of objects too.
- The log names the spawner it borrows before trying (`Nitro spawn: borrowing ...`), so a failure says which one it was.
