## What's new

- **Nitro Trap in levels without Nitro crates, Crash 1 included.** When a Nitro Trap is waiting in a level with no Nitro crates, the client finds the game's own Nitro in memory (it's loaded in every level) and has an ordinary crate's spawner make one. Your test in Ruination showed this works: a real Nitro fell and exploded, and no box counted for it. The countdown and the coin flip (ahead of Crash or falling onto him) stay the same. The first Nitro Trap in a level can take about 2 seconds longer while it looks.
- **Crash colors in Lights Out, Fumbling in the Dark and other places with their own copy of Crash.** Those levels, and some bosses and cutscenes, carry their own copy of Crash's textures, so he showed his normal colors there. The client now colors those copies too.
  - At the next game start, `dinput8.dll` adds the colored textures into those archives. When you change the colors or go back to normal, it puts them back exactly as they were.
  - The console says `Crash colors: also in N levels and scenes ...` at startup.
  - The versions of his textures with fruit juice or ash splashed on them take your colors too.
- **This build updates `dinput8.dll`, so copy it along with the `CrashNSTAP` folder.** Connect once (the client makes your colored Crash again), then restart the game.
