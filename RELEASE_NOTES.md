## What's new

- **Fixed the game not starting in V1.2.74.** The new cleanup of old logs stopped the game before it opened. It now runs after the client's log is open, using the same plain Windows calls the client uses everywhere else.
- **Fixed the game crashing when a falling Nitro landed.** V1.2.72 and V1.2.73 tried to set the Nitro off as it landed, and that crashed the game. A real Nitro Trap's falling Nitro now just lands again, as it did before V1.2.70.
- **`/nitro_drop boom` tests the landing explosion a new, safer way.** Each step goes to the log before it's sent, so if the game still crashes, the log shows which step did it. Once it works without trouble, the real trap will use it.
- **Midair Nitro on a jump in place, also right after a respawn.** While Crash runs, the client learns which way he faces (the log says `learned which way Crash faces`). A jump in place then has a direction even when he hasn't run since he respawned.
- **Logs are easier to find.** Each game's log is now named after when the game started, like `client-2026-10-23_16-01-05.log`, and only the newest 10 are kept. The logs are only ever written, never read back, so you can delete them any time. Your seeds aren't stored there.
- Still in this build: Nitro Trap off in Coco's jet ski levels, and only ahead of Crash in the scuba levels (Under Pressure, Deep Trouble).
