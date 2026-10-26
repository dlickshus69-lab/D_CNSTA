## Stable build

This is the **Stable build**, for playing seeds: everything finished so far. What's still being worked on (more of Harder Bosses) is only in the **Testing build**, the other release on this page.

Updating in the middle of a seed is fine: the client fixes work right away. The new options and logic only change seeds generated with this release's `crash_nst.apworld`.

## What's new in V1.4

- **Harder Papu Papu, all of him** (`harder_bosses`): after a hit he slams 2 to 5 times, each slam aimed at Crash, and his spin can turn around midway. An invisible wall in front of his throne keeps Crash off his chair (drawn in red; F7 hides it). Landing on him while he's red costs a life.
- **Boss Speed now also speeds up his slam** and shortens the time he can be hit after his red ends (4.4 s at 100% down to 2.2 s at 200%).
- **New Bosses options:** `boss_slam_time` (how soon he slams after he stands), `boss_slam_aim` (0–100%, default 55: how far ahead of Crash he aims) and `boss_hit_window` (2 to 10 s, or by speed).
- **Crashsanity in logic** (new seeds): each level only gets controls that can do its clear, crystal, gems and secret exit, and boxes those controls can't reach aren't box checks, so F10 stays a last resort.
- If the game crashes, the log now says where.
