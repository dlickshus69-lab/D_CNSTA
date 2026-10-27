## Testing build

This is the **Testing build**: it has what's still being worked on (Harder bosses). For playing a seed, the **Stable build** is the other release on this page. Its apworld is `crash_nst_testing.apworld`, the game **Crash Bandicoot N. Sane Trilogy (Testing)** (install it next to the Stable `crash_nst.apworld`; a YAML picks one with its `game:` line). This client plays Testing seeds and regular ones.

## What's new

- **Nitros placed around Roo's pattern:** each time he starts a new pattern (fight start, after a hit, after a death), the Nitros are placed knowing exactly where he'll jump that round.
- **Never cornered:** there's always a free platform Roo never lands on that round, and it has two ways out (not a dead end like the back platform). This was the trap in your last clip.
- **Still not easy:** Nitros go on the platforms off his path when they can, the spots you'd rest on, and next to your safe spots, but never so many that you're cut off. At a 6- or 7-platform pattern there's no spot off his path left, so you have to keep moving.
- The log line for each pattern now lists the safe platforms too.
- To try: test 3 still on, `/boss ripper1` with harder bosses on. Play through all the hits and say whether it ever feels impossible or too easy. Send the log.
