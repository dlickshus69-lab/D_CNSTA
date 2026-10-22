Crash skins
===========

Put skin mods for Crash here. A skin mod is a replacement Crash.pak (the file a mod tells you to copy into the
game's archives folder). You can drop the mod's whole folder in here as it came, or just its Crash.pak renamed:

  skins\Biker Crash\Crash.pak          (or skins\biker.pak)
  skins\PS1 Crash\archives\Crash.pak   (or skins\playstation_1.pak)
  skins\My Skin\Crash.pak              (any other name works too)

Pick one with crash_skin in your YAML's Cosmetic section (default_crash, playstation_1, biker, or the
name of any folder here), or in the client's console with /skin name (/skin on its own lists them, /skin default goes
back to the normal Crash).

When the game starts, the client copies the chosen skin's Crash.pak into the game's archives folder, before the
game reads it, exactly like installing the mod by hand. The game's own Crash.pak is kept in skins\default first,
and goes back when you pick default_crash. So a new choice shows the next time the game starts.

PlayStation 1 Crash: a folder or .pak whose name has playstation, PS1, PSX or low poly in it.
Biker Crash: a folder or .pak whose name has biker in it.

If Steam updates or verifies the game, its new Crash.pak becomes the default and the skin is put back on at the
next start.

Skin mods are other people's work, and their quirks come with them (the same as installing them by hand):
the Biker Crash mod reuses Aku Aku's look files for its jacket, so Aku Aku is invisible while wearing it.
