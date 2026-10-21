"""player options"""

from dataclasses import dataclass

from Options import (
    Visibility,
    Choice,
    DefaultOnToggle,
    OptionDict,
    PerGameCommonOptions,
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
    then unlocks more if needed.
    """

    display_name = "Starting Levels per Campaign"
    range_start = 1
    range_end = 31
    default = 2


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
    without Spin, and Spin is placed in those first checks.

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
    """How often Slippery Traps appear compared to other traps. Ice for the Trap Duration: Crash turns
    slowly, so turns and reversals become wide drifting arcs, and he keeps sliding after you let go.
    Strongest on a controller (stick and d-pad)."""

    display_name = "Slippery Trap Weight"
    range_start = 0
    range_end = 100
    default = 20


class SlowTrapWeight(Range):
    """How often Slow Traps appear compared to other traps. Crash can only walk for the Trap Duration:
    the stick and d-pad become a steady walking tilt. Controller only (keys cannot be half pressed).
    No effect in gimmick levels (hog, polar bear, jetpack, Crash 3 vehicles) or the chase levels
    Boulders, Boulder Dash, Bone Yard and Dino Might!"""

    display_name = "Slow Trap Weight"
    range_start = 0
    range_end = 100
    default = 20


class DarknessTrapWeight(Range):
    """How often Darkness Traps appear compared to other traps. The screen goes dark except for a
    soft spotlight on Crash, like Lights Out, for the Trap Duration. No effect in gimmick levels
    (hog, polar bear, jetpack, Crash 3 vehicles)."""

    display_name = "Darkness Trap Weight"
    range_start = 0
    range_end = 100
    default = 20


class NitroTrapWeight(Range):
    """How often Nitro Traps appear compared to other traps. The game spawns a real Nitro crate a few
    steps behind Crash on the path he just took. It only happens in levels that have Nitro crates (it
    borrows one of their spawners) and never in gimmick levels; until then it waits without holding up
    other traps."""

    display_name = "Nitro Trap Weight"
    range_start = 0
    range_end = 100
    default = 20


class MoveLockTrapWeight(Range):
    """How often Move Lock Traps appear compared to other traps. Spin, slide, body slam and
    crouch stop working for the Trap Duration."""

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
    """How often Freeze Traps appear compared to other traps. All input stops for 3 seconds."""

    display_name = "Freeze Trap Weight"
    range_start = 0
    range_end = 100
    default = 20


class TrapDuration(Range):
    """Seconds that Move Lock, Reverse Controls, Slippery, Slow and Darkness Traps last. A new trap of the
    same kind adds its time."""

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

    F10 restores vanilla controls if a style prevents progress."""

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
