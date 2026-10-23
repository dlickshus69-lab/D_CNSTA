"""level data, archipelago IDs and item requirements"""

from dataclasses import dataclass
from collections import Counter
import hashlib
import json
from pkgutil import get_data

GAME = "Crash Bandicoot N. Sane Trilogy"
BASE = 0xCB0000
PROTOCOL = 4


@dataclass(frozen=True)
class Level:
    key: str
    title: str

    @property
    def campaign(self):
        return int(self.key[1])

    @property
    def name(self):
        return f"Crash {self.campaign} - {self.title}"

    @property
    def number(self):
        # bosses use a separate ID range from regular levels in the same campaign
        return int(self.key[1:]) + (1000 if self.key.startswith("B") else 0)


_LEVELS = """
L101|N. Sanity Beach
L102|Jungle Rollers
L103|The Great Gate
L104|Boulders
L105|Upstream
L106|Rolling Stones
L107|Hog Wild
L108|Native Fortress
L109|Up the Creek
L110|The Lost City
L111|Temple Ruins
L112|Road to Nowhere
L113|Boulder Dash
L114|Whole Hog
L115|Sunset Vista
L116|Heavy Machinery
L117|Cortex Power
L118|Generator Room
L119|Toxic Waste
L120|The High Road
L121|Slippery Climb
L122|Lights Out
L123|Fumbling in the Dark
L124|Jaws of Darkness
L125|Castle Machinery
L126|The Lab
L128|Stormy Ascent
B101|Papu Papu
B102|Ripper Roo
B103|Koala Kong
B104|Pinstripe Potoroo
B105|Dr. Nitrus Brio
B106|Dr. Neo Cortex
L201|Turtle Woods
L202|Snow Go
L203|Hang Eight
L204|The Pits
L205|Crash Dash
L206|Snow Biz
L207|Air Crash
L208|Bear It
L209|Crash Crush
L210|The Eel Deal
L211|Plant Food
L212|Sewer or Later
L213|Bear Down
L214|Road to Ruin
L215|Un-Bearable
L216|Hangin' Out
L217|Diggin' It
L218|Cold Hard Crash
L219|Ruination
L220|Bee-Having
L221|Piston It Away
L222|Rock It
L223|Night Fight
L224|Pack Attack
L225|Spaced Out
L226|Totally Bear
L227|Totally Fly
B201|Ripper Roo
B202|Komodo Brothers
B203|Tiny Tiger
B204|Dr. N. Gin
B205|Dr. Neo Cortex
L301|Toad Village
L302|Under Pressure
L303|Orient Express
L304|Bone Yard
L305|Makin' Waves
L306|Gee Wiz
L307|Hang 'em High
L308|Hog Ride
L309|Tomb Time
L310|Midnight Run
L311|Dino Might!
L312|Deep Trouble
L313|High Time
L314|Road Crash
L315|Double Header
L316|Sphynxinator
L317|Bye Bye Blimps
L318|Tell No Tales
L319|Future Frenzy
L320|Tomb Wader
L321|Gone Tomorrow
L322|Orange Asphalt
L323|Flaming Passion
L324|Mad Bombers
L325|Bug Lite
L326|Ski Crazed
L328|Area 51?
L330|Rings of Power
L331|Hot Coco
L332|Eggipus Rex
L333|Future Tense
B301|Tiny Tiger
B302|Dingodile
B303|N. Tropy
B304|N. Gin
B305|Neo Cortex
"""
LEVELS = tuple(Level(*line.split("|")) for line in _LEVELS.strip().splitlines())
BY_KEY = {level.key: level for level in LEVELS}
STARTS = ("L101", "L102", "L201", "L202", "L301", "L302")
FINALS = ("B106", "B205", "B305")
# these stages keep their entrances when regular hub levels are shuffled
SECRET_SOURCES = {"L226": "L215", "L227": "L216", "L331": "L314", "L332": "L311"}
COLORS = {
    1: ("Blue", "Green", "Orange", "Purple", "Red", "Yellow"),
    2: ("Blue", "Green", "Purple", "Red", "Yellow"),
    3: ("Blue", "Green", "Purple", "Red", "Yellow"),
}
GEMS = tuple(
    f"Crash {game} - {color} Gem" for game, colors in COLORS.items() for color in colors
)
ABILITIES = (
    "Crash 2 - Speed Shoes",
    "Crash 3 - Super Body Slam",
    "Crash 3 - Double Jump",
    "Crash 3 - Death Tornado Spin",
    "Crash 3 - Fruit Bazooka",
    "Crash 3 - Speed Shoes",
)
ITEMS = {f"{level.name} - Access": BASE + level.number for level in LEVELS}
ITEMS.update({name: BASE + 2000 + index for index, name in enumerate(GEMS)})
ITEMS.update({name: BASE + 2100 + index for index, name in enumerate(ABILITIES)})
ITEMS["Nothing"] = BASE + 2200
FILLER = ("1-Up", "Wumpa Fruit")
ITEMS.update({name: BASE + 2201 + index for index, name in enumerate(FILLER)})
TOKEN = "AP Token"
ITEMS[TOKEN] = BASE + 2203
# filler: an Aku Aku mask for Crash right away (experimental; the client hands it over like an Aku Aku crate's pickup)
AKU_MASK = "Aku Aku Mask"
ITEMS[AKU_MASK] = BASE + 2204
HUB_ITEMS = {
    (1, 2): "Crash 1 - Progressive Island Access",
    (2, 2): "Crash 2 - Progressive Elevator Access",
    **{(3, area): f"Crash 3 - Hub Section {area} Unlock" for area in range(2, 6)},
}
ITEMS.update(
    {name: BASE + 2300 + game * 10 + area for (game, area), name in HUB_ITEMS.items()}
)
HUB_ITEM_COUNTS = {HUB_ITEMS[1, 2]: 2, HUB_ITEMS[2, 2]: 4}
# Ability Shuffle: one item per basic move, shared by all three campaigns
BASIC_MOVES = ("Spin", "Body Slam", "Crouch Jump", "Slide")
ITEMS.update({name: BASE + 2400 + index for index, name in enumerate(BASIC_MOVES)})
# Aku Aku Sanity: Aku Aku masks are an item; until it is found, Aku Aku crates give no mask
AKU_AKU = "Aku Aku"
ITEMS[AKU_AKU] = BASE + 2410
# Crash 1's dark levels, where Aku Aku's glow is the light (Lights Out, Fumbling in the Dark)
AKU_LIT_LEVELS = frozenset({"L122", "L123"})
# traps replace junk items; the client applies them once you are in a level and in control.
# IDs never change. Death Trap is retired: it stays in the table for older seeds but is never placed.
TRAP_IDS = {
    "Death Trap": 2500,
    "Move Lock Trap": 2501,
    "Reverse Controls Trap": 2502,
    "Freeze Trap": 2503,
    "Life Loss Trap": 2504,
    "Slippery Trap": 2505,
    "Slow Trap": 2506,
    "Darkness Trap": 2507,
    "Nitro Trap": 2508,
}
TRAPS = tuple(TRAP_IDS)
ITEMS.update({name: BASE + offset for name, offset in TRAP_IDS.items()})


def valid_requirement_counts(items):
    return all(
        count <= HUB_ITEM_COUNTS.get(name, 1) for name, count in Counter(items).items()
    )


def hub_area(key):
    """area of a physical entrance. secret stages use their source entrance."""
    key = SECRET_SOURCES.get(key, key)
    game, number = int(key[1]), int(key[2:])
    if game == 1:
        if key[0] == "B":
            return 1 if number == 1 else 2 if number <= 3 else 3
        return 1 + (number >= 9) + (number >= 16)
    if key[0] == "B":
        return number
    # crash 3's bonus/DLC buttons are outside the four electric gates
    if game == 3 and number > 25:
        return 1
    return (number - 1) // 5 + 1


def hub_requirements(key):
    game, area = int(key[1]), hub_area(key)
    if area == 1:
        return ()
    if game != 3:
        return (HUB_ITEMS[game, 2],) * (area - 1)
    return (HUB_ITEMS[game, area],)


def lock_requirements(required, placements, hub_locks, level_locks):
    entrances = {destination: source for source, destination in placements.items()}
    access_levels = {f"{level.name} - Access": level.key for level in LEVELS}
    result = Counter()
    for name in required:
        key = access_levels.get(name)
        if key is None:
            result[name] = 1
            continue
        if level_locks:
            result[name] = 1
        if hub_locks:
            parent = SECRET_SOURCES.get(key, key)
            result |= Counter(hub_requirements(entrances.get(parent, parent)))
    return tuple(result.elements())


def validate_victory_goal(selected, bosses, config):
    """validate the victory condition against the selected pool"""
    finals = {key for key in selected if key in FINALS}
    regular = sum(key.startswith("L") for key in selected)
    if (
        type(config) is not dict
        or set(config) != {"type", "required"}
        or config["type"] not in ("cortex", "all_bosses", "level_clears", "token_hunt")
        or type(config["required"]) is not int
        or type(bosses) is not list
        or any(type(key) is not str for key in bosses)
        or len(set(bosses)) != len(bosses)
    ):
        raise ValueError("Invalid victory goal")
    if config["type"] == "all_bosses":
        campaigns = {BY_KEY[key].campaign for key in bosses if key in BY_KEY}
        expected = {
            level.key
            for level in LEVELS
            if level.key.startswith("B") and level.campaign in campaigns
        }
        if (
            config["required"] != 0
            or not bosses
            or set(bosses) != expected
            or not expected <= set(selected)
        ):
            raise ValueError(
                "All-bosses goal requires every boss in its goal campaigns"
            )
    elif config["type"] == "cortex":
        if config["required"] != 0 or not bosses or not set(bosses) <= finals:
            raise ValueError("Cortex goal requires selected final bosses")
    else:
        maximum = regular if config["type"] == "level_clears" else len(LOCATIONS)
        if bosses or not 1 <= config["required"] <= maximum:
            raise ValueError(
                "Goal Count exceeds selected regular levels, or token count is invalid"
            )
    return dict(config)


# separate gem events, in addition to a normal all-boxes gem
# Crash 1 colors replace the normal box gem
C1_COLORS = {
    "L110": "Green",
    "L118": "Orange",
    "L119": "Blue",
    "L121": "Red",
    "L122": "Purple",
    "L126": "Yellow",
}
EXTRA_GEMS = {
    "L201": "Blue Gem",
    "L202": "Red Gem",
    "L203": "Timed Gem",
    "L207": "Death Route Gem",
    "L210": "Green Gem",
    "L211": "Yellow Gem",
    "L212": "Gem Route Gem",
    "L214": "Death Route Gem",
    "L217": "Death Route Gem",
    "L218": "Death Route Gem",
    "L219": "Death Route Gem",
    "L220": "Purple Gem",
    "L221": "Death Route Gem",
    "L223": "Death Route Gem",
    "L225": "Gem Route Gem",
    "L304": "Gem Route Gem",
    "L307": "Yellow Gem",
    "L309": "Gem Route Gem",
    "L311": "Gem Route Gem",
    "L312": "Red Gem",
    "L313": "Purple Gem",
    "L316": "Gem Route Gem",
    "L319": "Secret Route Gem",
    "L320": "Blue Gem",
    "L321": "Gem Route Gem",
    "L323": "Green Gem",
    "L325": "Gem Route Gem",
    "L328": "Race Gem",
    "L330": "Race Gem",
    "L333": "Death Route Gem",
}

# levels with colored-gem routes
GEM_PATHS = {
    "L102": ("Green",),
    "L103": ("Yellow",),
    "L105": ("Orange",),
    "L106": ("Blue",),
    "L108": ("Red",),
    "L112": ("Red",),
    "L113": ("Purple",),
    "L117": ("Blue",),
    "L122": ("Yellow",),
    "L124": ("Blue",),
    "L125": ("Green",),
    "L203": ("Blue",),
    "L206": ("Red",),
    "L212": ("Yellow",),
    "L219": ("Green",),
    "L225": COLORS[2],
    "L304": ("Red",),
    "L309": ("Purple",),
    "L311": ("Yellow",),
    "L316": ("Blue",),
    "L321": ("Green",),
    "L325": COLORS[3],
}
# route gems and all-box rewards have different prerequisites from ordinary clears.
BOX_GEM_PATHS = {
    key: colors
    for key, colors in GEM_PATHS.items()
    if key.startswith("L1") and key != "L125"
}
BOX_GEM_PATHS.update(
    {key: GEM_PATHS[key] for key in ("L203", "L206", "L309", "L311", "L321")}
)
EXTRA_GEM_PATHS = {
    key: GEM_PATHS[key]
    for key in ("L212", "L219", "L225", "L304", "L309", "L311", "L316", "L321", "L325")
}
# Secret Exit Checks: levels with a secret exit, and where it leads. Crash 2's go through the secret warp room;
# Crash 1 has none. Taking the exit is the check, whether or not its destination is unlocked.
SECRET_EXITS = {
    "L207": "Snow Go's secret entrance",
    "L213": "Air Crash's secret entrance",
    "L215": "Totally Bear",
    "L216": "Totally Fly",
    "L217": "Road to Ruin's secret entrance",
    "L311": "Eggipus Rex",
    "L314": "Hot Coco",
}
# the way to a secret exit: Dino Might!'s is on its yellow gem path, and Diggin' It's plant takes a body slam
SECRET_EXIT_GEMS = {"L311": ("Yellow",)}
SECRET_EXIT_MOVES = {"L217": ("Body Slam",)}
# levels that can't be cleared by jumping alone (Ability Shuffle): the move the way through needs, for the clear, the
# crystal and the boxes, in both Ability Logic modes. From play reports; more are added as they turn up.
CLEAR_MOVES = {
    "L103": ("Spin",),                  # The Great Gate: a ? crate stacked between iron crates blocks the path
    "L108": ("Spin",),                  # Native Fortress: the spinning log wheels
    "L209": ("Slide",),                 # Crash Crush: slide under the electric gates
    "L212": ("Spin", "Crouch Jump"),    # Sewer or Later: break the crate walls, crawl under the low pipes
    "L220": ("Slide",),                 # Bee-Having: slide under the electric fence, slide-jump the long gaps
}
# boxes in a level's bonus round that need a move the rest of the level doesn't
BONUS_MOVES = {
    "L210": ("Spin",),                  # The Eel Deal's bonus round (Spin or Slide; logic expects Spin)
}
# first chamber AFTER each boss's award
C3_POWER_STAGES = (6, 11, 16, 21)
# vehicle and riding stages do not use Crash's on-foot powers.
C3_VEHICLE_LEVELS = frozenset(
    {
        "L302",
        "L303",
        "L305",
        "L308",
        "L310",
        "L312",
        "L314",
        "L317",
        "L318",
        "L322",
        "L324",
        "L326",
        "L328",
        "L330",
        "L331",
        "L332",
    }
)
# gimmick stages keep vanilla controls under Crashsanity and Ability Shuffle.
# must match special_level() in the client's game_runtime.cpp
GIMMICK_LEVELS = C3_VEHICLE_LEVELS | frozenset(
    {"L107", "L114", "L208", "L213", "L215", "L222", "L224", "L226"}
)
# vanilla source: 5 relics, 15 relics, 25 relics, Road Crash, Dino Might.
C3_SECRET_POWER_COUNTS = {
    "L326": 0,
    "L328": 2,
    "L330": 4,
    "L331": 2,
    "L332": 2,
    "L333": 4,
}
CRYSTAL_LEVELS = frozenset(
    f"L{game}{index:02}" for game in (2, 3) for index in range(1, 26)
)


def check_category(level, event):
    if event == "secret_exit":
        return "secret_exit"
    if event == "crystal":
        return "crystal"
    if event == "relic":
        return "relic"
    if event == "gem" and level.key in C1_COLORS:
        return "colored_gem"
    if event == "extra_gem" and EXTRA_GEMS[level.key] in {
        f"{color} Gem" for colors in COLORS.values() for color in colors
    }:
        return "colored_gem"
    if event in ("gem", "extra_gem"):
        return "white_gem"
    return "clear"


def checks(level):
    """(event key, display name, stable location ID)."""
    address = BASE + 10000 + level.number * 10
    yield "clear", f"{level.name} - Clear", address
    if level.key.startswith("B"):
        return
    label = "Gem" if level.key == "L332" else "Box Gem"
    if level.key in C1_COLORS:
        label = f"{C1_COLORS[level.key]} Gem (All Boxes, No Death)"
    yield "gem", f"{level.name} - {label}", address + 1
    if level.key in EXTRA_GEMS:
        yield "extra_gem", f"{level.name} - {EXTRA_GEMS[level.key]}", address + 2
    yield "relic", f"{level.name} - Relic (Any Tier)", address + 3
    if level.key in CRYSTAL_LEVELS:
        yield "crystal", f"{level.name} - Power Crystal", address + 4
    if level.key in SECRET_EXITS:
        yield "secret_exit", f"{level.name} - Secret Exit", address + 5


LOCATIONS = {name: address for level in LEVELS for _, name, address in checks(level)}
EVENT_IDS = {
    (level.key, event): address
    for level in LEVELS
    for event, _, address in checks(level)
}
# colored gems reuse their route-unlock item
VANILLA_REWARDS = {
    address: (
        (
            f"Crash {level.campaign} - {C1_COLORS[level.key]} Gem"
            if level.key in C1_COLORS
            else f"Crash {level.campaign} - {EXTRA_GEMS[level.key]}"
        )
        if check_category(level, event) == "colored_gem"
        else name
    )
    for level in LEVELS
    for event, name, address in checks(level)
    if event not in ("clear", "secret_exit")
}
VANILLA_ITEMS = {
    name: address - 7000
    for address, name in VANILLA_REWARDS.items()
    if name not in GEMS
}
ITEMS.update(VANILLA_ITEMS)
CRATES = json.loads(get_data(__name__, "crates.json"))


def crate_event(crate):
    if crate.get("map_member"):
        return "box:" + crate["name"] + "@map:" + crate["map_stage"]
    if crate.get("bonus_member"):
        return "box:" + crate["name"] + "@bonus:" + crate["bonus_stage"]
    return "box:" + crate["name"] + ("@gem_route" if crate.get("route_member") else "")


def _crate_moves():
    """Crates logic can't expect to break by jumping alone, from their kind and where they sit (relaxed Ability Logic
    expects only jumping for boxes; cautious already expects these moves):
    - arrow crates bounce Crash when he jumps on them: Spin breaks them
    - iron-bound crates (Crash 2 and 3) only break under a Body Slam
    - a crate sitting on a TNT or Nitro crate (often with an iron crate on top, as in The High Road and Heavy
      Machinery) and every crate in a stack four or more high (Cold Hard Crash): Spin from the side
    - Turtle Woods' underground room, below the floor Crash body slams through: Body Slam"""
    import struct
    from collections import defaultdict
    def spot(crate):
        return struct.unpack("<3f", bytes.fromhex(crate["position_bits"]))
    def explosive(crate):
        return "TNT" in crate["name"] or "Nitro" in crate["name"]
    moves = defaultdict(set)
    groups = defaultdict(list)
    for crate in CRATES:
        groups[(crate["level"], crate.get("bonus_member") or crate.get("map_member") or "")].append((crate, spot(crate)))
        name, key = crate["name"], (crate["level"], crate_event(crate))
        if "Arrow" in name and "Iron" not in name:
            moves[key].add("Spin")
        if "IronBound" in name:
            moves[key].add("Body Slam")
        if crate["level"] == "L201" and spot(crate)[2] < -1000:
            moves[key].add("Body Slam")
    for members in groups.values():
        for crate, (x, y, z) in members:
            if explosive(crate):
                continue
            column = sorted({round(pz): pz for _, (px, py, pz) in members if abs(px - x) < 30 and abs(py - y) < 30}.values())
            at = min(range(len(column)), key=lambda i: abs(column[i] - z))
            low = high = at
            while low > 0 and 55 < column[low] - column[low - 1] < 105:
                low -= 1
            while high + 1 < len(column) and 55 < column[high + 1] - column[high] < 105:
                high += 1
            on_explosive = any(explosive(other) and abs(px - x) < 30 and abs(py - y) < 30 and 55 < z - pz < 105
                               for other, (px, py, pz) in members)
            if high - low + 1 >= 4 or on_explosive:
                moves[(crate["level"], crate_event(crate))].add("Spin")
    return {key: tuple(sorted(value)) for key, value in moves.items()}


CRATE_MOVES = _crate_moves()


def crate_location(crate):
    name = f"{BY_KEY[crate['level']].name} - Box {crate['name']}"
    if crate.get("map_member"):
        route = "Death Route" if crate["map_stage"] == "Death" else "Main Map"
        return f"{name} [{route}]"
    if crate.get("bonus_member"):
        return f"{name} [Bonus: {crate['bonus_stage']}]"
    return name + (" [Gem Route]" if crate.get("route_member") else "")


for crate in CRATES:
    LOCATIONS[crate_location(crate)] = crate["id"]
CRATE_GEM_REQUIREMENTS = {
    (crate["level"], crate_event(crate)): tuple(crate["gem_requirements"])
    for crate in CRATES
    if "gem_requirements" in crate
}


def requirements(level, event):
    """return required items"""
    required = [f"{level.name} - Access"]
    if level.key in SECRET_SOURCES:
        required.extend(requirements(BY_KEY[SECRET_SOURCES[level.key]], "clear"))
        if level.key == "L332":
            required.append("Crash 3 - Yellow Gem")
    if event.startswith("box:"):
        colors = GEM_PATHS.get(level.key, ())
    elif event == "gem":
        colors = BOX_GEM_PATHS.get(level.key, ())
    elif event == "extra_gem":
        colors = EXTRA_GEM_PATHS.get(level.key, ())
    elif event == "secret_exit":
        colors = SECRET_EXIT_GEMS.get(level.key, ())
    else:
        colors = ()
    # a verified crate rule overrides the level-wide estimate, even when it is empty.
    colors = CRATE_GEM_REQUIREMENTS.get((level.key, event), colors)
    required.extend(f"Crash {level.campaign} - {color} Gem" for color in colors)
    # main-path clears and crystals must not inherit optional box/bonus power gates
    # keep conservative power requirements for the remaining on-foot objectives
    if (
        level.campaign == 3
        and level.key not in C3_VEHICLE_LEVELS
        and event not in ("clear", "crystal")
    ):
        if level.key.startswith("B"):
            count = int(level.key[2:]) - 1
        elif level.key in C3_SECRET_POWER_COUNTS:
            count = C3_SECRET_POWER_COUNTS[level.key]
        else:
            count = sum(int(level.key[2:]) >= first for first in C3_POWER_STAGES)
        required.extend(ABILITIES[1 : 1 + count])
    return tuple(dict.fromkeys(required))


# Ability Shuffle logic. This is a cautious draft, not verified level by level.
# Loosen a level once it is known to need fewer moves, e.g.
#   MOVE_OVERRIDES[("L203", "gem")] = ("Spin", "Slide")
# keys are (level key, event); "box:" covers every box in that level without its own entry.
MOVE_OVERRIDES = {}


def easiest_start(starting_levels):
    """with Spin shuffled, the one starting level logic expects you to clear by jumping alone.
    level shuffle can start you anywhere, so pick the earliest level by its own position in its game."""
    regular = [key for key in starting_levels if key not in GIMMICK_LEVELS and key not in CLEAR_MOVES] or [
        key for key in starting_levels if key not in GIMMICK_LEVELS
    ] or list(starting_levels)
    return min(regular, key=lambda key: (int(key[2:]), key[1]))


def move_requirements(level, event, spin_shuffled, jump_only_levels, relaxed=False):
    """basic moves logic expects for a check while Ability Shuffle is on. relaxed (Ability Logic): level clears,
    crystals and boxes expect only jumping (enemies are jumped on, crates are jumped on or hit from below);
    bosses still expect Spin, gems and relics keep the cautious needs."""
    if event == "secret_exit":
        # what the level clear needs, and what the way to the exit adds
        needed = move_requirements(level, "clear", spin_shuffled, jump_only_levels, relaxed)
        if level.key not in GIMMICK_LEVELS:
            needed = (*needed, *SECRET_EXIT_MOVES.get(level.key, ()))
        return tuple(dict.fromkeys(needed))
    override = MOVE_OVERRIDES.get((level.key, event))
    if override is None and event.startswith("box:"):
        override = MOVE_OVERRIDES.get((level.key, "box:"))
    if override is not None:
        return tuple(override)
    # hog, polar bear, jetpack and Crash 3 vehicle stages keep vanilla controls (Crashsanity too), so they need no moves
    if level.key in GIMMICK_LEVELS:
        return ()
    # a level that can't be cleared by jumping alone needs its move for everything past the obstacle; some bonus rounds
    # and some crates (arrow crates, iron-bound crates, tall stacks, ...) need one of their own
    way = CLEAR_MOVES.get(level.key, ())
    if event.startswith("box:"):
        extra = (*BONUS_MOVES.get(level.key, ()),) if "@bonus:" in event else ()
        way = tuple(dict.fromkeys((*way, *extra, *CRATE_MOVES.get((level.key, event), ()))))
    if relaxed:
        if event in ("clear", "crystal"):
            return tuple(dict.fromkeys((*(("Spin",) if level.key.startswith("B") else ()), *way)))
        if event.startswith("box:"):
            return way
    if event in ("clear", "crystal"):
        # with Spin shuffled, your easiest starting level is assumed clearable by jumping alone
        base = () if spin_shuffled and level.key in jump_only_levels and not way else ("Spin",)
        return tuple(dict.fromkeys((*base, *way)))
    # gems, relics and boxes: Crash 1 levels were built for jump and spin only
    if level.campaign == 1:
        return ("Spin",)
    return BASIC_MOVES


def box_requirements(level, event="box:"):
    return requirements(level, event)


def entrance_requirements(level, active_levels):
    return (
        (f"{level.name} - Access",)
        if level.key in FINALS
        else requirements(level, "clear")
    )


def check_requirements(level, event, active_levels):
    needed = requirements(level, event)
    return tuple(dict.fromkeys((*entrance_requirements(level, active_levels), *needed)))


def level_pools(value):
    """Resolve level names or keys and return each campaign's pool in catalogue order."""
    if type(value) is not dict or set(value) - {"crash_1", "crash_2", "crash_3"}:
        raise ValueError(
            "level_pool must be a dictionary with crash_1/crash_2/crash_3 keys"
        )
    result = {}
    for campaign, names in value.items():
        game = int(campaign[-1])
        if type(names) is not list or not names:
            raise ValueError(f"level_pool.{campaign} must be a nonempty list of levels")
        levels = [level for level in LEVELS if level.campaign == game]
        aliases = {
            alias.casefold(): level.key
            for level in levels
            for alias in (level.key, level.title, level.name)
        }
        keys = set()
        for name in names:
            key = (
                aliases.get(name.strip().casefold()) if isinstance(name, str) else None
            )
            if key is None:
                raise ValueError(f"Unknown level in level_pool.{campaign}: {name!r}")
            if key in keys:
                raise ValueError(f"Duplicate level in level_pool.{campaign}: {name!r}")
            keys.add(key)
        if not any(key.startswith("L") for key in keys):
            raise ValueError(f"level_pool.{campaign} needs at least one regular level")
        result[game] = [level for level in levels if level.key in keys]
    return result


def select_crates(stable_ids, percentage, seed):
    """Select a repeatable percentage of boxes, rounding the count up."""
    if type(percentage) is not int or not 0 <= percentage <= 100:
        raise ValueError("percentage must be an integer from 0 through 100")
    ids = list(stable_ids)
    if any(not isinstance(key, str) or not key or len(key) > 512 for key in ids):
        raise ValueError("Crate IDs must be nonempty strings of at most 512 characters")
    if len(set(ids)) != len(ids):
        raise ValueError("Duplicate crate identities")
    ranked = sorted(
        ids, key=lambda key: (hashlib.sha256(f"{seed}\0{key}".encode()).digest(), key)
    )
    return sorted(ranked[: (len(ids) * percentage + 99) // 100])


# disabled map leftovers: only the root object list references these five crates
INACTIVE_CRATE_IDS = frozenset((13717209, 13717210, 13717211, 13731535, 13731568))


def breakable_crate(crate):
    # excluding unbreakables, switches, debug and inactive objects
    name = crate["name"]
    return (
        crate.get("id") not in INACTIVE_CRATE_IDS
        and "DEBUG" not in name.upper()
        and not name.startswith("Crate_Switch_Nitro")
        and not ("Iron" in name and "IronBound" not in name)
    )
