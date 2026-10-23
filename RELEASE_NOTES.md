## What's new

- **The falling Nitro really blows up where it lands now.** V1.2.70 sent the crate the wrong message: the "touched" note, which on its own does nothing. Now the client sends the Nitro's own "explode" steps, the same ones the game runs when Crash runs into one. The log says `set it off where it landed (...)` and then whether it went off.
- **Midair Nitro on a jump in place.** If Crash stood still through the countdown and then jumps, the Nitro now appears ahead of him at his height, in the direction he last ran. Before, it fell onto him instead.
- **No more stutter from the Nitro Trap.** Twice a second, all the time in a level, the client went over every object in the level to find a spawner it could borrow. That made the game hitch every half second. Now it does this on its own thread, every few seconds, and only while a Nitro Trap is waiting or on its way.
- **No more freeze when a Nitro appears.** The first two Nitros of a game wrote about 250 lines of old research into the log at once, which froze the game for most of a second. That research, and the line-by-line log of every message a trap Nitro got, are gone.
