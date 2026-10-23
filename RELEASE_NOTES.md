## What's new

- **Aku Aku Mask: Aku Aku now knows he has the mask.** Your last log showed what was different: with a crate's mask, Aku Aku's own state switches to "1 mask", and with the filler mask it stayed at "no mask". On a hit he then had nothing to take away, so the model stayed. `/aku_give` (and the filler) now set his state to match, so the hit should take the mask the normal way.
- If Crash already has a mask, the filler gives a second one: two hits to lose them, like a second crate. No extra model appears.
- The xvpkmc2n cleanup try from V1.2.37 is gone; it didn't help.
- Release pages no longer have the install line at the bottom.
