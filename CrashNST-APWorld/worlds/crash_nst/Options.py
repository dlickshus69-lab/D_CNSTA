"""player options"""

from dataclasses import dataclass

from Options import (
    Visibility,
    Choice,
    OptionGroup,
    DefaultOnToggle,
    OptionDict,
    PerGameCommonOptions,
    NamedRange,
    Range,
    Toggle,
    DeathLink,
)
from .data import LEVELS


class HubAreaLocks(Toggle):
    """Require AP area-unlock items for later islands, elevator floors and gated hub sections.

    Crash 1 has Progressive Island Access items,
    Crash 2 has Progressive Elevator Access items
    Crash 3 sections has Hub Section X Unlock items.
    """

    display_name = "Hub Area Locks"


class LevelAccessLocks(DefaultOnToggle):
    """Require each selected level's Access item. Independent of Hub Area Locks.

    When disabled, selected levels only require their hub area and secret-route requirements.
    """

    display_name = "Level Access Locks"


class Campaigns(Choice):
    """Choose one, two, or all three Crash campaigns."""

    display_name = "Campaigns"
    option_all = 0
    option_crash_1 = 1
    option_crash_2 = 2
    option_crash_3 = 3
    option_crash_1_2 = 4
    option_crash_1_3 = 5
    option_crash_2_3 = 6
    default = option_all


class Crash1Levels(Range):
    """Number of regular Crash 1 levels to randomly select from the level pool.

    Required bosses and source levels for secret stages are added separately.
    """

    display_name = "Crash 1 Levels"
    range_start = 2
    range_end = 27
    default = 27


class Crash2Levels(Range):
    """Number of regular Crash 2 levels to randomly select from the level pool.

    Required bosses and source levels for secret stages are added separately.
    """

    display_name = "Crash 2 Levels"
    range_start = 2
    range_end = 27
    default = 27


class Crash3Levels(Range):
    """Number of regular Crash 3 levels to randomly select from the level pool.

    Required bosses and source levels for secret stages are added separately.
    """

    display_name = "Crash 3 Levels"
    range_start = 2
    range_end = 31
    default = 31


class StartingLevels(Range):
    """Number of starting level entrances per enabled campaign.

    Fills the earliest hub areas available in the selected level pool first,
    then unlocks more if needed. Very tight settings (hub and level locks, few
    check types, no boxsanity) get a few more starting levels automatically, so
    the seed opens with at least 4 checks and can always be generated.
    """

    display_name = "Starting Levels per Campaign"
    range_start = 1
    range_end = 31
    default = 2


class SecretExitChecks(Toggle):
    """Taking a secret exit is a check: Air Crash, Bear Down, Un-Bearable, Hangin' Out and Diggin' It in Crash 2,
    Road Crash and Dino Might! in Crash 3 (in the levels you selected; Crash 1 has none). The exit counts even
    when the level it leads to is still locked. Dino Might!'s needs the Yellow Gem, and with Ability Shuffle
    Diggin' It's needs Body Slam."""

    display_name = "Secret Exit Checks"


class Goal(Choice):
    """Choose what the player must complete to win.

    All Cortex requires defeating Cortex in every enabled campaign.
    Single-Cortex goals require that campaign to be enabled.
    Cortex entrances follow the chosen hub area and level access locks.

    Level clears count distinct regular levels from the selected pool, across all enabled campaigns.

    Token hunt replaces a percentage of filler items with AP Tokens.
    Collecting the required percentage of those tokens wins.
    """

    display_name = "Goal"
    option_all_cortex = 0
    option_cortex_1 = 1
    option_cortex_2 = 2
    option_cortex_3 = 3
    option_level_clears = 4
    option_token_hunt = 5
    default = option_all_cortex


class RequireAllBosses(Toggle):
    """Require every boss, including Cortex, in the campaigns targeted by the goal.

    Applies only to Cortex goals. Required bosses are included even if omitted
    from the level pool. Level-clear and token-hunt goals are unaffected.
    """

    display_name = "Require All Goal Campaign Bosses"


class GoalCount(Range):
    """Distinct regular levels to clear. Used only for the level-clears goal."""

    display_name = "Goal Count"
    range_start = 1
    range_end = 1000
    default = 10


class TokenPercentage(Range):
    """Percentage of filler slots replaced with AP Tokens for token hunt."""

    display_name = "Token Hunt Filler Percentage"
    range_start = 1
    range_end = 100
    default = 20


class TokenRequiredPercentage(Range):
    """Percentage of the available AP Tokens required to win."""

    display_name = "Token Hunt Required Percentage"
    range_start = 1
    range_end = 100
    default = 80


class WhiteGemChecks(Range):
    """Percentage of eligible white-gem checks in selected levels."""

    display_name = "White Gem Checks Percent"
    range_start = 0
    range_end = 100
    default = 100


class ColoredGemChecks(Range):
    """Percentage of eligible colored-gem checks in selected levels."""

    display_name = "Colored Gem Checks Percent"
    range_start = 0
    range_end = 100
    default = 100


class CrystalChecks(Range):
    """Percentage of eligible power-crystal checks in selected levels."""

    display_name = "Power Crystal Checks Percent"
    range_start = 0
    range_end = 100
    default = 100


class RelicChecks(Range):
    """Percentage of eligible relic checks in selected levels."""

    display_name = "Relic Checks Percent"
    range_start = 0
    range_end = 100
    default = 100


class AbilityShuffle(Choice):
    """Find Crash's basic moves as items: Spin, Body Slam, Crouch Jump (crouch and high jump) and Slide.

    One item per move works in all three games, together with Crashsanity and the Crash 3 powers.
    off: normal moves.
    start_with_spin: start with Spin and find the other three.
    shuffle_spin: find all four. Logic only expects your easiest starting level (and gimmick stages)
    without Spin. Spin Placement decides how soon Spin turns up.

    Logic is a cautious draft: level clears need Spin; gems, relics and boxes need Spin in Crash 1
    and every move in Crash 2 and 3. Gimmick stages (hog, polar bear, jetpack and Crash 3 vehicle
    levels) keep vanilla controls and need none, as with Crashsanity. F10 restores normal controls
    if a move you need is missing. Every move and ability is always in the seed to be found. If the
    seed has no spare checks for them, you start with a few relic, crystal or white gem rewards
    instead (their checks still count); a little boxsanity avoids that. Each campaign gets at least
    two starting levels while this is on.
    """

    display_name = "Ability Shuffle"
    option_off = 0
    option_start_with_spin = 1
    option_shuffle_spin = 2
    default = option_off


class SpinPlacement(Choice):
    """Where Spin goes when Ability Shuffle is shuffle_spin.

    anywhere: any check logic says you can reach without Spin (with relaxed Ability Logic that is most
    level clears, crystals and boxes). Some seeds give it in your first checks, others leave you jumping
    for a long while. In a multiworld, progression_balancing pulls it earlier; 0 lets it land anywhere.
    early_to_mid: never in your starting levels. It goes on a random check you can reach without Spin
    (a gimmick stage, so a hog, polar bear, jetpack, vehicle or scuba level) behind unlocks you find
    along the way (two or more when the seed allows), so it usually turns up a few rounds of unlocks in. Logic is the
    same either way, so the seed is always beatable. If the seed has no such check (no gimmick stages
    in your levels), Spin goes in your first checks.
    """

    display_name = "Spin Placement"
    option_early_to_mid = 1
    option_anywhere = 2
    # older YAMLs: "early" was removed and means early_to_mid
    alias_early = 1
    default = option_early_to_mid


class AkuAkuSanity(Toggle):
    """Aku Aku is an item. Until you find it, breaking an Aku Aku crate gives no mask in any of the three
    games, like a move you have not unlocked yet. Once you have it, Aku Aku crates work normally.
    Lights Out and Fumbling in the Dark (Crash 1) are pitch dark without his light, so logic expects the
    Aku Aku item for everything in them.
    The free mask the game gives after a few deaths in a row stays off, Aku Aku item or not: masks come only
    from Aku Aku crates once you have the item, and from Aku Aku Mask items.

    The client uses the game's own "no Aku Aku masks" switch.
    """

    display_name = "Aku Aku Sanity"


class AbilityLogic(Choice):
    """What logic expects you can do without the basic moves when Ability Shuffle is on.

    cautious: level clears need Spin (except your easiest starting level), and gems, relics and boxes
    need Spin in Crash 1 and every move in Crash 2 and 3. Abilities can only be early or in gimmick stages.
    relaxed: level clears, power crystals and boxes need only jumping (enemies can be jumped on, crates
    can be jumped on or hit from below), so abilities can be on almost any of those. Bosses still need
    Spin, and gems and relics keep the cautious needs.
    Either way, levels that can't be cleared by jumping alone need their move for everything past the
    obstacle: The Great Gate, Native Fortress and Tomb Wader (its first door opens with a spun wheel) need Spin, and Jungle Rollers needs it past its crate wall; Crash Crush,
    Bee-Having and Piston It Away need Slide; Sewer or Later needs Spin and Crouch Jump; The Eel Deal's bonus round and
    Sunset Vista's Cortex bonus round need Spin and High Time's needs Crouch Jump. Some crates need a move of their own: arrow crates, crates on a TNT or
    Nitro, crates in stacks three high and crates right under an iron-bound crate need Spin; iron-bound crates and
    Turtle Woods' underground room need Body Slam. Gimmick stages keep vanilla controls, so they need no moves.
    Gimmick stages need no moves either way.
    """

    display_name = "Ability Logic"
    option_cautious = 0
    option_relaxed = 1
    default = option_cautious


class WumpaFruitWeight(Range):
    """How often a filler item is Wumpa Fruit (1 to 10 fruit each) compared to the other fillers. Filler fills
    every slot left after progression, tokens and traps."""

    display_name = "Wumpa Fruit Filler Weight"
    range_start = 0
    range_end = 100
    default = 50


class OneUpWeight(Range):
    """How often a filler item is a 1-Up (an extra life) compared to the other fillers. If every filler weight
    is 0, fillers are an even mix of Wumpa Fruit and 1-Ups."""

    display_name = "1-Up Filler Weight"
    range_start = 0
    range_end = 100
    default = 50


class AkuAkuMaskWeight(Range):
    """How often a filler item is an Aku Aku Mask compared to the other fillers. When it arrives, Crash gets a mask
    as if he had picked one up from an Aku Aku crate (in the next level, once he can move): it takes one hit, and on top of a mask it gives the next one, as a crate's does
    (two masks make Aku gold, three give the invincibility). It works with Aku Aku Sanity too, before you have found the Aku Aku item.
    0 (the default) leaves it out."""

    display_name = "Aku Aku Mask Filler Weight"
    range_start = 0
    range_end = 100
    default = 0


class TrapFillPercentage(Range):
    """Replace a percentage of junk items in the item pool with random traps.

    Traps need the updated client. Each one waits until you are in a level and in control.
    The trap weights below decide how often each kind appears.
    """

    display_name = "Trap Fill Percentage"
    range_start = 0
    range_end = 100
    default = 0


class DeathTrapWeight(Range):
    """No longer used: Death Traps were removed. Kept so older YAMLs still load."""

    display_name = "Death Trap Weight (unused)"
    range_start = 0
    range_end = 100
    default = 0
    visibility = Visibility.none


class LifeLossTrapWeight(Range):
    """How often Life Loss Traps appear compared to other traps. You lose one life.

    Experimental: it runs the game's own extra-life reward with -1."""

    display_name = "Life Loss Trap Weight"
    range_start = 0
    range_end = 100
    default = 20


class SlipperyTrapWeight(Range):
    """How often Slippery Traps appear compared to other traps. For the Trap Duration every floor Crash
    stands on turns into the game's own ice, exactly like the ice in Snow Go, and goes back to normal
    when it ends. No effect in gimmick levels or the Crash 2 jet-board levels."""

    display_name = "Slippery Trap Weight"
    range_start = 0
    range_end = 100
    default = 20


class SlowTrapWeight(Range):
    """How often Slow Traps appear compared to other traps. Crash can only walk for the Trap Duration:
    the stick and d-pad become a steady walking tilt. Controller only (keys cannot be half pressed).
    No effect in gimmick levels (hog, polar bear, jetpack, Crash 3 vehicles), the Crash 2 jet-board
    levels or the chase levels Boulders, Boulder Dash, Crash Dash, Bone Yard and Dino Might!"""

    display_name = "Slow Trap Weight"
    range_start = 0
    range_end = 100
    default = 20


class DarknessTrapWeight(Range):
    """How often Darkness Traps appear compared to other traps. The screen goes dark except for a
    soft spotlight on Crash, like Lights Out, for the Trap Duration. No effect in gimmick levels
    (hog, polar bear, jetpack, Crash 3 vehicles), except underwater, on Coco's jet ski and on the
    Crash 2 jet board."""

    display_name = "Darkness Trap Weight"
    range_start = 0
    range_end = 100
    default = 20


class NitroTrapWeight(Range):
    """How often Nitro Traps appear compared to other traps. Each one starts with "NITRO INCOMING! 3, 2, 1",
    in step with a TNT crate's own countdown and sound. Then a coin flip: a real Nitro crate appears about
    1.3 crates ahead of Crash in the direction he is running (floating in midair if he is jumping), or a
    Nitro falls onto the spot where he stands (in 0.31 seconds) and blows up there. It happens in every normal level, and it
    never counts as a box. It also strikes on the hog, the Crash 2 jet board and polar bear and in the
    chases, and in the scuba levels (only ahead of Crash there, floating in the water), but never in the
    jetpack, motorcycle, plane, Coco's jet ski, Coco's tiger or Baby T levels; until then it waits without
    holding up other traps."""

    display_name = "Nitro Trap Weight"
    range_start = 0
    range_end = 100
    default = 20


class MoveLockTrapWeight(Range):
    """How often Move Lock Traps appear compared to other traps. Spin, slide, body slam and
    crouch stop working for the Trap Duration. No effect in gimmick or Crash 2 jet-board levels.
    With Ability Shuffle, it waits until you have at least one of those moves."""

    display_name = "Move Lock Trap Weight"
    range_start = 0
    range_end = 100
    default = 20


class ReverseControlsTrapWeight(Range):
    """How often Reverse Controls Traps appear compared to other traps. Left/right and
    forward/back swap for the Trap Duration, on the controller and on WASD and the arrow keys."""

    display_name = "Reverse Controls Trap Weight"
    range_start = 0
    range_end = 100
    default = 20


class FreezeTrapWeight(Range):
    """How often Freeze Traps appear compared to other traps. All input stops for 6 seconds."""

    display_name = "Freeze Trap Weight"
    range_start = 0
    range_end = 100
    default = 20


class TrapDuration(Range):
    """Seconds that Move Lock, Reverse Controls, Slippery, Slow and Darkness Traps last. A new trap of the
    same kind that arrives while one runs adds no time. The time only runs while Crash can move: it stops during a level's intro,
    while he dies and respawns, in the pause menu, hubs and loading screens."""

    display_name = "Trap Duration"
    range_start = 10
    range_end = 120
    default = 30


class RelicRequirement(Choice):
    """Minimum relic tier that sends a level's relic check.

    any: sapphire, gold or platinum.
    gold_or_platinum: gold or platinum.
    platinum: platinum only.

    Relic checks stay in logic and can hold important items. Above any, logic expects the
    tools first: Crash 2 relics need Crash 2 Speed Shoes, Crash 3 on-foot relics need
    Crash 3 Speed Shoes (gold) or every Crash 3 power (platinum). Crash 1 and vehicle stages
    have no extra requirement.
    """

    display_name = "Relic Requirement"
    option_any = 0
    option_gold_or_platinum = 1
    option_platinum = 2
    alias_sapphire = option_any
    alias_gold = option_gold_or_platinum
    default = option_any


class LevelShuffle(Choice):
    """Shuffle regular level destinations. Bosses and secret stages keep their entrances.

    Hot Coco, Eggipus Rex, Totally Bear and Totally Fly require their vanilla path
    and their own Access item. Their vanilla path levels are included automatically.
    """

    display_name = "Level Placement Shuffle"
    option_off = 0
    option_within_campaign = 1
    option_across_campaigns = 2
    default = option_off


class Boxsanity(Range):
    """Percentage of eligible breakable boxes in selected levels that become AP checks."""

    display_name = "Boxsanity Percent"
    range_start = 0
    range_end = 100
    default = 0


class Crashsanity(Toggle):
    """Experimental: assign Crash 1/2/3 basic movement rules per regular level.

    Crash 1 controls have no Body Slam, Crouch Jump or Slide (even once found), and only Crash 3 controls double jump.
    A level only gets controls that can do its clear, power crystal, gems and secret exit, and boxes its controls can't
    reach are left out of the box checks. F10 restores vanilla controls as a last resort."""

    display_name = "Crashsanity"


class CollectibleRewards(OptionDict):
    """Reward modes for power_crystals, white_gems, colored_gems and relics (ankhs).

    randomized (default): shuffle selected rewards.
    vanilla: selected checks keep their original local rewards.
    precollected: shuffle selected rewards and grant unselected collectibles at startup.
    """

    display_name = "Collectible Rewards"
    valid_keys = ("power_crystals", "white_gems", "colored_gems", "relics")
    default = dict.fromkeys(valid_keys, "randomized")

    def modes(self):
        if not isinstance(self.value, dict) or any(
            key not in self.valid_keys
            or value not in ("randomized", "vanilla", "precollected")
            for key, value in self.value.items()
        ):
            raise ValueError(
                f"{self.display_name} accepts power_crystals, white_gems, colored_gems "
                "and relics. Each value must be randomized, vanilla or precollected."
            )
        return {key: self.value.get(key, "randomized") for key in self.valid_keys}

    def verify(self, world, player_name, plando_options):
        super().verify(world, player_name, plando_options)
        self.modes()


class BoxItemSpoilers(Toggle):
    """Color selected, reachable AP boxes by their item classification."""

    display_name = "Box Item Classification Spoilers"


class LevelPool(OptionDict):
    """Available levels under crash_1, crash_2 and crash_3.

    Use level titles, full names or level keys. The level count randomly selects
    from this pool using the seed. List order does not affect the selection.
    Omitted campaigns use their full pool.
    """

    display_name = "Available Level Pool"
    valid_keys = ("crash_1", "crash_2", "crash_3")
    default = {
        f"crash_{game}": [level.title for level in LEVELS if level.campaign == game]
        for game in range(1, 4)
    }

    def verify(self, world, player_name, plando_options):
        super().verify(world, player_name, plando_options)
        from .data import level_pools

        level_pools(self.value)


class CrashColor(Choice):
    """default leaves it as the game has it; otherwise one of the colors, or random. The client makes the colored Crash
    from your own game files, keeping his shading, and he wears it from the next time the game starts. The colors
    stay for the whole seed (/colors in the client shows them; only a new seed changes them). bright_white is a
    brighter white that can glow under strong lights."""

    option_default = 0
    option_red = 1
    option_dark_red = 2
    option_orange = 3
    option_yellow = 4
    option_gold = 5
    option_lime = 6
    option_green = 7
    option_dark_green = 8
    option_teal = 9
    option_cyan = 10
    option_light_blue = 11
    option_blue = 12
    option_navy = 13
    option_purple = 14
    option_violet = 15
    option_pink = 16
    option_magenta = 17
    option_brown = 18
    option_tan = 19
    option_white = 20
    option_light_gray = 21
    option_gray = 22
    option_black = 23
    option_bright_white = 24
    default = 0


class CrashFurColor(CrashColor):
    """The color of Crash's fur. default leaves it as the game has it; otherwise one of the colors, or random. The client makes the colored Crash
    from your own game files, keeping his shading, and he wears it from the next time the game starts. The colors
    stay for the whole seed (/colors in the client shows them; only a new seed changes them). bright_white is a
    brighter white that can glow under strong lights."""

    display_name = "Crash Fur Color"


class CrashChestColor(CrashColor):
    """The color of Crash's chest and jaw (normally tan). default leaves it as the game has it; otherwise one of the colors, or random. The client makes the colored Crash
    from your own game files, keeping his shading, and he wears it from the next time the game starts. The colors
    stay for the whole seed (/colors in the client shows them; only a new seed changes them). bright_white is a
    brighter white that can glow under strong lights."""

    display_name = "Crash Chest Color"


class CrashPantsColor(CrashColor):
    """The color of Crash's pants. default leaves it as the game has it; otherwise one of the colors, or random. The client makes the colored Crash
    from your own game files, keeping his shading, and he wears it from the next time the game starts. The colors
    stay for the whole seed (/colors in the client shows them; only a new seed changes them). bright_white is a
    brighter white that can glow under strong lights."""

    display_name = "Crash Pants Color"


class CrashShoesColor(CrashColor):
    """The color of Crash's shoes (the soles and laces stay white). default leaves it as the game has it; otherwise one of the colors, or random. The client makes the colored Crash
    from your own game files, keeping his shading, and he wears it from the next time the game starts. The colors
    stay for the whole seed (/colors in the client shows them; only a new seed changes them). bright_white is a
    brighter white that can glow under strong lights."""

    display_name = "Crash Shoes Color"


class CrashHairColor(CrashColor):
    """The color of Crash's mohawk (his eyebrows stay). default leaves it as the game has it; otherwise one of the colors, or random. The client makes the colored Crash
    from your own game files, keeping his shading, and he wears it from the next time the game starts. The colors
    stay for the whole seed (/colors in the client shows them; only a new seed changes them). bright_white is a
    brighter white that can glow under strong lights."""

    display_name = "Crash Mohawk Color"


class CrashGlovesColor(CrashColor):
    """The color of Crash's gloves (normally dark brown leather). default leaves it as the game has it; otherwise one of the colors, or random. The client makes the colored Crash
    from your own game files, keeping his shading, and he wears it from the next time the game starts. The colors
    stay for the whole seed (/colors in the client shows them; only a new seed changes them). bright_white is a
    brighter white that can glow under strong lights."""

    display_name = "Crash Gloves Color"


class CrashEyebrowsColor(CrashColor):
    """The color of Crash's eyebrows (normally dark brown). default leaves it as the game has it; otherwise one of the colors, or random. The client makes the colored Crash
    from your own game files, keeping his shading, and he wears it from the next time the game starts. The colors
    stay for the whole seed (/colors in the client shows them; only a new seed changes them). bright_white is a
    brighter white that can glow under strong lights."""

    display_name = "Crash Eyebrows Color"


class HarderBosses(Toggle):
    """Harder bosses. For now only Papu Papu (Crash 1's first boss); every other boss stays as the game has him.

    Papu Papu's red rule: he glows red at the start of the fight and after each hit, until the slam that follows his next
    spin. Landing on him while he's red doesn't count as a hit: Crash bounces off and gets hurt (an Aku Aku mask takes it,
    or Crash dies). He also gets the health and speed set below. Without this, those settings do nothing."""

    display_name = "Harder Bosses"


class BossHealth(Range):
    """How much health a harder boss has, in percent of the game's (needs Harder Bosses). One more hit every 25%: Papu
    Papu has 5 hits at 100, 6 at 125, 7 at 150, 8 at 175 and 9 at 200. His health bar gets a notch for each hit."""

    display_name = "Boss Health"
    range_start = 100
    range_end = 200
    default = 150


class BossSpeed(Range):
    """How fast a harder boss fights, in percent of the game's (needs Harder Bosses): Papu Papu spins faster (each hit
    still speeds him up, gentler near the top so the end stays beatable), waits less before his spins, slams sooner after
    he stands (unless Boss Slam Time sets it) and can be hit for less time once his red ends (unless Boss Hit Window sets
    it)."""

    display_name = "Boss Speed"
    range_start = 100
    range_end = 200
    default = 150


class BossSlamTime(NamedRange):
    """How soon a harder boss slams after he stands up, in hundredths of a second (needs Harder Bosses; for now Papu
    Papu). by_speed: from Boss Speed, the game's 0.9 s at 100% down to 0.45 s at 200%. 25 to 90: that time (0.25 to
    0.9 s) at any Boss Speed."""

    display_name = "Boss Slam Time"
    range_start = 25
    range_end = 90
    default = 0
    special_range_names = {"by_speed": 0}


class BossHitWindow(NamedRange):
    """How long a harder boss can be hit once his red ends, in tenths of a second (needs Harder Bosses; for now Papu
    Papu); then he's red again until the slam after his next spin (or his next spin starts). by_speed: from Boss Speed, 3 s
    at 100% down to 1.5 s at 200%. 5 to 100: that time (0.5 to 10 s) at any Boss Speed."""

    display_name = "Boss Hit Window"
    range_start = 5
    range_end = 100
    default = 0
    special_range_names = {"by_speed": 0}


class HarderBossesAkuAku(Toggle):
    """Aku Aku masks in harder boss fights (needs Harder Bosses). Off: Crash has no mask in those fights (any mask he has
    waits, and masks from crates and items there come in the next level). On: masks work there as usual."""

    display_name = "Aku Aku in Harder Boss Fights"


# sections on the options page and in generated YAMLs, in this order. Archipelago puts Progression Balancing and
# Accessibility in "Game Options" first and adds "Item & Location Options" last.
crash_option_groups = [
    OptionGroup("Goal", [
        Goal,
        Campaigns,
        LevelAccessLocks,
        HubAreaLocks,
        RequireAllBosses,
        GoalCount,
        TokenPercentage,
        TokenRequiredPercentage,
    ]),
    OptionGroup("Levels", [
        Crash1Levels,
        Crash2Levels,
        Crash3Levels,
        LevelShuffle,
        LevelPool,
        StartingLevels,
        SecretExitChecks,
    ]),
    OptionGroup("Crystals / Gems / Relics", [
        CollectibleRewards,
        WhiteGemChecks,
        ColoredGemChecks,
        CrystalChecks,
        RelicChecks,
        RelicRequirement,
    ]),
    OptionGroup("Sanity Options", [
        Crashsanity,
        Boxsanity,
        BoxItemSpoilers,
        AbilityShuffle,
        SpinPlacement,
        AbilityLogic,
        AkuAkuSanity,
    ]),
    OptionGroup("Filler / Traps", [
        WumpaFruitWeight,
        OneUpWeight,
        AkuAkuMaskWeight,
        TrapFillPercentage,
        LifeLossTrapWeight,
        MoveLockTrapWeight,
        ReverseControlsTrapWeight,
        FreezeTrapWeight,
        SlipperyTrapWeight,
        SlowTrapWeight,
        DarknessTrapWeight,
        NitroTrapWeight,
        DeathTrapWeight,
        TrapDuration,
        DeathLink,
    ]),
    OptionGroup("Bosses", [
        HarderBosses,
        BossHealth,
        BossSpeed,
        BossSlamTime,
        BossHitWindow,
        HarderBossesAkuAku,
    ]),
    OptionGroup("Cosmetic", [
        CrashFurColor,
        CrashChestColor,
        CrashPantsColor,
        CrashShoesColor,
        CrashHairColor,
        CrashGlovesColor,
        CrashEyebrowsColor,
    ]),
]


@dataclass
class CrashOptions(PerGameCommonOptions):
    campaigns: Campaigns
    hub_area_locks: HubAreaLocks
    level_access_locks: LevelAccessLocks
    goal: Goal
    require_all_bosses: RequireAllBosses
    goal_count: GoalCount
    token_percentage: TokenPercentage
    token_required_percentage: TokenRequiredPercentage
    level_pool: LevelPool
    crash1_levels: Crash1Levels
    crash2_levels: Crash2Levels
    crash3_levels: Crash3Levels
    starting_levels: StartingLevels
    secret_exit_checks: SecretExitChecks
    white_gem_checks: WhiteGemChecks
    colored_gem_checks: ColoredGemChecks
    crystal_checks: CrystalChecks
    relic_checks: RelicChecks
    relic_requirement: RelicRequirement
    level_shuffle: LevelShuffle
    boxsanity: Boxsanity
    collectible_rewards: CollectibleRewards
    box_item_spoilers: BoxItemSpoilers
    crashsanity: Crashsanity
    ability_shuffle: AbilityShuffle
    spin_placement: SpinPlacement
    ability_logic: AbilityLogic
    aku_aku_sanity: AkuAkuSanity
    wumpa_fruit_weight: WumpaFruitWeight
    one_up_weight: OneUpWeight
    aku_aku_mask_weight: AkuAkuMaskWeight
    trap_fill_percentage: TrapFillPercentage
    life_loss_trap_weight: LifeLossTrapWeight
    move_lock_trap_weight: MoveLockTrapWeight
    reverse_controls_trap_weight: ReverseControlsTrapWeight
    freeze_trap_weight: FreezeTrapWeight
    slippery_trap_weight: SlipperyTrapWeight
    slow_trap_weight: SlowTrapWeight
    darkness_trap_weight: DarknessTrapWeight
    nitro_trap_weight: NitroTrapWeight
    death_trap_weight: DeathTrapWeight
    trap_duration: TrapDuration
    death_link: DeathLink
    harder_bosses: HarderBosses
    boss_health: BossHealth
    boss_speed: BossSpeed
    boss_slam_time: BossSlamTime
    boss_hit_window: BossHitWindow
    harder_bosses_aku_aku: HarderBossesAkuAku
    crash_fur_color: CrashFurColor
    crash_chest_color: CrashChestColor
    crash_pants_color: CrashPantsColor
    crash_shoes_color: CrashShoesColor
    crash_hair_color: CrashHairColor
    crash_gloves_color: CrashGlovesColor
    crash_eyebrows_color: CrashEyebrowsColor
