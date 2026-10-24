## What's new

- **Harder bosses, first try (test commands only; your seeds play as before).**
  - **`/boss_health 100` to `200`**: the bosses' hits in percent, from the next boss fight (`200` is double). It doesn't change either N. Gin, Cortex in Crash 2, or Brio's first phase; Brio's last form does get it. `/boss_health off` goes back.
  - **`/boss_speed 100` to `200`**: the bosses' pace in percent (`125` is a quarter faster). So far it affects Papu Papu (his spin and the waits before it), N. Gin in Crash 3 (the waits between his attacks) and Brio (the waits before his potions).
  - **`/boss_rules on`**: Papu Papu glows red while he can't be hit (his slam and his spin). Landing on him while he's red costs Crash a life, and the hit doesn't count. When he stops spinning and is dizzy, the red goes away and he can be hit. `/boss_rules off` turns it off.
  - The log notes what each one did, to tune them before they become YAML settings.
