## What's new

- **Aku Aku Mask, fifth try.** Your last log caught a real mask pickup. It showed that the game's messages carry their values in a separate list, so the earlier tries were sending Aku Aku empty messages. The client now remembers the exact message Aku Aku sent himself when a crate's mask became yours (fcbn65ha). `/aku_give` replays that same message first, then a few others from the pickup, one per second, and the log says which one worked.
- While a filler mask is being handed over, the Aku Aku Sanity lock no longer blocks the messages that bring it out.
