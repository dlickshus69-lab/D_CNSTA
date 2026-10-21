# Crash N. Sane Trilogy — Archipelago

## Play and configure

| Input | Action |
|---|---|
| F1 / Esc | Open / close the AP console |
| Tab | Complete commands, `!hint` item names and `!hint_location` location names |
| Page Up / Page Down, mouse wheel | Scroll console history |
| F7 | Toggle AP markers, labels and HUD |
| F10 | Restore normal movement if you're stuck in Crashsanity |

Console input supports Left/Right, Home/End, Shift selection and Ctrl+A/C/V.
Automatic connection attempts stop after five failures.  
Correct the connection settings and use `/connect` to start a fresh attempt.

The level panel in the top-left corner spells it out: checks found out of the level's total and how many more are available now (in logic), whether the level clear is done, then the same for gems, the power crystal and the relic. A line is green when your items let you get more of it now, red when they reach none of the rest, and yellow once it is all done. The label over a hub entrance has three lines: the game and level, with "(All Done)" in green once every check there is found; `Checks: found/total` with how many more your items let you get now; then found/total for `Level Clear`, `C` power crystal, `G` gems and `R` relic, colored the same way.   
Campaign totals appear in the game choose menu and on hub maps.  
Approach an unlocked hub entrance to see its destination label and check counts.  
`/hubdistance_c1`, `/hubdistance_c2` and `/hubdistance_c3` show each hub's label
distance.   
Add a number to change it in game units (default 250, maximum 100000).  
Use 0 to hide that hub's labels.  
`/hubdistance 500` sets all three hubs to 500.  
Settings are saved locally and restored next time.

Useful local commands:

```text
/help
/status
/boxmarkers tint/bounds/off
/tint_opacity 0.5
/slow_tilt 0.71
/trap_test slippery
/hubdistance 500
/hubdistance_c1 1000
/hubdistance_c2 250
/hubdistance_c3 500
/outoflogic on/off
/disconnect
/connect
```

Markers normally show unchecked selected crates that are in logic. `/outoflogic on` also shows out-of-logic crates. 
`/tint_opacity` shows the current tint strength.  
Set it from 0 (no tint) to 1 (full tint), for example `/tint_opacity 0.5` for 50%. The default is 0.75.
`/slow_tilt` sets how far the Slow Trap pushes the stick (0.2 to 1, default 0.71). While Slow is on, the client tunes it itself: if Crash runs it goes down at once and never back up to that tilt, if he stands still for a moment while you push it goes up, and the result is saved. Each change is logged as `Slow tuner: ...`. `/trap_test slow` (or `slippery`, `darkness`, `reverse`, `freeze`, `movelock`, `nitro`) turns that trap on for 30 seconds without AP so you can try it; close the console with F1 first, because the game ignores the controller while it is open. The setting is saved on this computer.
`box_item_spoilers` in the yaml can color markers by item category. Chat and `!` commands go to the AP server.

When item spoilers are enabled, the client scouts selected locations on connection without creating hints. Item flags are cached per seed and slot for offline play. Boxes use the normal marker color until their flags arrive.

## World options

On the options page and in generated YAMLs the options are grouped: Game Options, Goal, Levels, Crystals / Gems / Relics, Sanity Options, Traps, then Item & Location Options.

Secret destinations include required source levels automatically.

- Choose any campaign combination, level pool and level counts.
- `starting_levels`: starting entrances per enabled campaign, from 1–31 (default 2), capped at the selected regular entrances. Fill the earliest available hub areas first. Later areas start unlocked only if the requested count needs them or the pool has no earlier entrances. The selected pool is preserved.
- `level_shuffle`: `off`, `within_campaign` or `across_campaigns`. If you ever slip into a level that is still locked (for example by pressing confirm the moment a hub loads), the game sends you straight back to the hub. Starting Crash 1 puts Crash on the map at the earliest open level, not wherever the save last left him.
- `boxsanity`: 0–100% of the boxes turn into ap locations. There are 5291 boxes across 85 stages.
- `white_gem_checks`, `colored_gem_checks`, `crystal_checks`, `relic_checks`: percentages of collectible locations to select as AP checks. Unselected collectibles stay vanilla or are overwritten by collectible_rewards.
- `collectible_rewards`: per-category `randomized`, `vanilla` or `precollected`. Randomized: Default shuffle behaviour. Vanilla: selected checks keep their normal items. Precollected: randomizes selected checks and gives the unselected collectibles from the start.
- `goal`: `all_cortex`, `cortex_1`, `cortex_2`, `cortex_3`, `level_clears` or `token_hunt`. `goal_count` applies only to level clears and is capped at the selected regular-level count.
- `require_all_bosses`: off by default. For Cortex goals, require every boss, including Cortex, in the goal campaigns. Required bosses are added even if excluded from the level pool. This does not affect level-clear or token-hunt goals.
- `token_percentage`: replace 1–100% of the remaining filler slots with AP Tokens (default 20%). `token_required_percentage`: collect 1–100% of those tokens to win (default 80%). Both round up. Progression, collectible rewards and useful items keep their slots. Token hunt needs at least one filler slot.
- `death_link` is optional and defaults off. 
- `crashsanity` is experimental and defaults off.
- `relic_requirement`: `any` (default), `gold_or_platinum` or `platinum`. The minimum relic tier that sends a relic check. A lower tier is not awarded, so you can replay the time trial. Relic checks stay in logic; above `any`, Crash 2 relics expect Crash 2 Speed Shoes and Crash 3 on-foot relics expect Crash 3 Speed Shoes (gold) or every Crash 3 power (platinum).
- `trap_fill_percentage`: 0–100% (default 0) of junk items (Wumpa/1-Up) become traps. `life_loss_trap_weight`, `move_lock_trap_weight`, `reverse_controls_trap_weight`, `freeze_trap_weight`, `slippery_trap_weight`, `slow_trap_weight`, `darkness_trap_weight` and `nitro_trap_weight` (0–100) decide the mix. **Life Loss Trap** (experimental): you lose one life through the game's own extra-life reward. **Move Lock Trap**: no spin, slide, body slam or crouch. **Reverse Controls Trap**: left/right and forward/back swap on the stick, d-pad, arrows and WASD. **Freeze Trap**: all input stops for 6 seconds. **Slippery Trap**: ice, at the speeds of the game's own ice: Crash builds from a walk to a run over about a second, slides on for about a second and a half after you let go, turns in wide slides, and a reversal slides to a halt before he walks back (stick and d-pad; movement keys stay held briefly); no effect in gimmick levels (the HUD shows "Slippery (off here)"). **Slow Trap**: Crash can only walk (stick and d-pad become a steady walking tilt, tuned with `/slow_tilt`; controller only), with no effect in gimmick levels or the chase levels Boulders, Boulder Dash, Bone Yard and Dino Might! (the HUD shows "Slow (off here)"). **Darkness Trap**: the screen goes dark except a soft spotlight that follows Crash, like Lights Out; no effect in gimmick levels, except underwater (Under Pressure, Deep Trouble), on Coco's jet ski (Makin' Waves, Tell No Tales, Ski Crazed) and on the Crash 2 jet board (the HUD shows "Darkness (off here)"). **Nitro Trap**: the game spawns a real Nitro crate about 1.5 crates ahead of Crash in the direction he is running, close enough to surprise you but far enough to jump it or stop. It uses floor he already stood on when it can (an earlier try at that spot), otherwise straight ahead along the line he is running, so it waits while he is in the air, turning or standing still. It needs a level that has Nitro crates (it borrows the spawner nearest the spot, whose own crate stays normal) and never happens in gimmick levels; until then it waits ("Nitro Trap incoming!") without holding up other traps. `trap_duration` (10–120 s, default 30) sets the timed traps; another trap of the same kind adds time. Traps wait until you are in a level, fire one at a time at least 10 seconds apart, show a timer on the HUD, and never repeat after reconnecting. Running traps pause in hubs, maps, the pause menu, other menus and loading screens (the HUD shows "Paused:") and continue when you play again; they never affect menu controls. F10 does not cancel them. For traps, the Crash 2 jet-board levels (Hang Eight, Air Crash, Plant Food) count as gimmick levels too. Gimmick stages ignore Move Lock.
- `ability_shuffle`: `off` (default), `start_with_spin` or `shuffle_spin`. Find **Spin**, **Body Slam**, **Crouch Jump** (crouch and high jump) and **Slide** as items; each works in all three games and alongside Crashsanity. Logic is a cautious draft: clears need Spin (with Spin shuffled, only your easiest starting level is expected by jumping), gems/relics/boxes need Spin in Crash 1 and every move in Crash 2 and 3. Gimmick stages (hog, polar bear, jetpack and Crash 3 vehicles) keep vanilla controls, as with Crashsanity. Logic follows the level you actually play, so it works with level shuffle across campaigns. Each campaign gets at least two starting levels. Every move and every ability of your campaigns (including both Speed Shoes) is always in the seed to be found; if the seed is full you start with a few relic, crystal or white gem rewards to make room, which a little `boxsanity` avoids. A newly found Crouch Jump applies after re-entering the level. Until you have Crouch Jump or Slide, the crouch/slide button does nothing. Press **F10** if a move you need is missing. The console prints each attack name once (`Ability Shuffle: attack ...`); please report what Spin shows as.
- `spin_placement` (with `shuffle_spin`): `early_to_mid` (default) keeps Spin out of your starting levels and puts it on a random check you can reach without Spin, in a gimmick stage you unlock along the way, so it turns up in the early-to-mid game. `early` puts it in your first checks. Logic is the same either way, so seeds stay beatable; with no gimmick stages in your levels Spin is placed early.

Totally Bear/Fly, Hot Coco and Eggipus Rex require their usual secret paths and the chosen access locks.  
CB3 relic-room entrances follow AP access locks instead of vanilla relic requirements.

With Hub Area Locks enabled, each **Crash 1 - Progressive Island Access** opens the next island (two copies total).  
Each **Crash 2 - Progressive Elevator Access** opens the next warp-room floor (four copies total).  
Starting entrances fill the earliest selected hub areas first. Any required later-area copies are granted from the start.
Crash 3 uses a separate **Hub Section Unlock** for each gated section.  
Boss defeats do not open hub areas.

Leaving a level returns to the physical entrance used, including shuffled levels in another campaign.
Starting a campaign without entrance history uses its default hub spawn.

Filler rewards are Wumpa Fruit and 1-Ups. Rewards received on hubs wait until gameplay and apply once.

## Update, disable and troubleshoot

Replace the package's `dinput8.dll` and merge its `CrashNSTAP` folder into the game folder.

To disable AP, close NST and remove `dinput8.dll` from the game folder.

Local profiles, connection settings, display settings and runtime logs are under `%LOCALAPPDATA%/CrashNSTAP/state`
The game-folder loader log is `CrashNSTAP/loader.log`
