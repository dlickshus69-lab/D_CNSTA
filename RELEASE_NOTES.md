## What's new

- **A trap's Nitro no longer counts as a box.** When a crate's spawner is borrowed to make the Nitro, the Nitro used to tell that spawner "the thing you made is gone" when it exploded. That crate's box count then took the explosion for its own crate breaking (your test: 0/18 went to 1/18). Now trap Nitros skip that one message. They still explode and disappear as usual, and the borrowed crate stays normal.
- **To check it:** the log now names every crate the box counter counts (`Box observer: Crate_... in L123 counted`), once each, so a trap Nitro going off should add no such line.
