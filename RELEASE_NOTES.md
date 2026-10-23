## What's new

- **Aku Aku Mask: the mask should protect now.** In V1.2.35, `/aku_give` brought the mask out, but a hit still killed Crash, because the game's mask count never changed. A real pickup is a longer chain of messages Aku Aku sends himself, and the count changes partway through it. `/aku_give` now sends the whole chain in the game's order. If Crash had no mask before, it also sets the game's mask count to 1.
