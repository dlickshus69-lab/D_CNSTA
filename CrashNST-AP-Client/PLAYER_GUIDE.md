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

HUD counters use **`checked/in logic (total)`**. `C`, `P` and `R` show selected gem, power-crystal and relic checks. 
Campaign totals appear in the game chooser and on hub maps.

Useful local commands:

```text
/help
/status
/boxmarkers tint/bounds/off
/outoflogic on/off
/disconnect
/connect
```

Markers normally show unchecked selected crates that are in logic. `/outoflogic on` also shows out-of-logic crates. 
`box_item_spoilers` in the yaml can color markers by item category. Chat and `!` commands go to the AP server.

When item spoilers are enabled, the client scouts selected locations on connection without creating hints. Item flags are cached per seed and slot for offline play. Boxes use the normal marker color until their flags arrive.

## World options

Secret destinations include required source levels automatically.

- Choose any campaign combination, level pool and level counts.
- `level_shuffle`: `off`, `within_campaign` or `across_campaigns`.
- `boxsanity`: 0–100% of the boxes turn into ap locations. There are 5291 boxes across 85 stages.
- `white_gem_checks`, `colored_gem_checks`, `crystal_checks`, `relic_checks`: percentages of collectible locations to select as AP checks. Unselected collectibles stay vanilla or are overwritten by collectible_rewards.
- `collectible_rewards`: per-category `randomized`, `vanilla` or `precollected`. Randomized: Default shuffle behaviour. Vanilla: selected checks keep their normal items. Precollected: randomizes selected checks and gives the unselected collectibles from the start.
- `goal`: `all_cortex`, `cortex_1`, `cortex_2`, `cortex_3`, `level_clears` or `token_hunt`. `goal_count` applies only to level clears and is capped at the selected regular-level count.
- `require_all_bosses`: off by default. For Cortex goals, require every boss, including Cortex, in the goal campaigns. Required bosses are added even if excluded from the level pool. This does not affect level-clear or token-hunt goals.
- `token_percentage`: replace 1–100% of the remaining filler slots with AP Tokens (default 20%). `token_required_percentage`: collect 1–100% of those tokens to win (default 80%). Both round up. Progression, collectible rewards and useful items keep their slots. Token hunt needs at least one filler slot.
- `death_link` is optional and defaults off. 
- `crashsanity` is experimental and defaults off.

Totally Bear/Fly, Hot Coco and Eggipus Rex require their vanilla secret paths plus destination Access.
CB3 relic-room entrances use AP Access items instead of the vanilla relic requirements.

Filler rewards are Wumpa Fruit and 1-Ups. Rewards received on hubs wait until gameplay and apply once.

## Update, disable and troubleshoot

Replace the package's `dinput8.dll` and merge its `CrashNSTAP` folder into the game folder.

To disable AP, close NST and remove `dinput8.dll` from the game folder.

Local profiles, connection settings, display settings and runtime logs are under `%LOCALAPPDATA%/CrashNSTAP/state`
The game-folder loader log is `CrashNSTAP/loader.log`
