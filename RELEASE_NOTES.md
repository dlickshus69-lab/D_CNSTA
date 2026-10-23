## What's new

- **A trap's Nitro no longer counts as a box, take two.** The counting comes from the box-count script of the crate whose spawner gets borrowed: it counts whatever that spawner makes. V1.2.64's log showed it plainly ("Crate_AkuAku005 in L123 counted"). Now the trap only borrows spawners whose owner counts nothing toward the level's boxes, like falling platforms or Wumpa fruit, and makes the Nitro from the game's own Nitro template. It never borrows a crate's, an enemy's, a checkpoint's, or one on a moving path.
- **Crash 2 and 3 too:** there the trap used to borrow a real Nitro crate's spawner, so it had the same hidden counting problem. It now works the same way in every level.
- The log names the spawner it borrows and what its owner runs (`Nitro spawn: borrowing ...`), and every crate the box counter counts (`Box observer: ... counted`).
