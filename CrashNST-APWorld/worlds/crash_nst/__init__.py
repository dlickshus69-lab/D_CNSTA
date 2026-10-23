"""NST Trilogy world"""

import hashlib
import logging
from collections import Counter

from BaseClasses import Item, ItemClassification, Location, Region
from worlds.AutoWorld import WebWorld, World
from .Options import CrashOptions, crash_option_groups
from .data import (
    AKU_MASK,
    GIMMICK_LEVELS,
    GAME,
    PROTOCOL,
    LEVELS,
    ITEMS,
    LOCATIONS,
    FINALS,
    GEMS,
    ABILITIES,
    FILLER,
    BY_KEY,
    check_category,
    checks,
    requirements,
    CRATES,
    select_crates,
    level_pools,
    entrance_requirements,
    check_requirements,
    VANILLA_REWARDS,
    VANILLA_ITEMS,
    breakable_crate,
    crate_event,
    crate_location,
    TOKEN,
    validate_victory_goal,
    SECRET_SOURCES,
    C3_VEHICLE_LEVELS,
    BASIC_MOVES,
    AKU_AKU,
    AKU_LIT_LEVELS,
    TRAPS,
    move_requirements,
    easiest_start,
    HUB_ITEMS,
    lock_requirements,
    hub_area,
    valid_requirement_counts,
)


# the fewest checks a seed may open with, and the largest share of its locations that may need progression items:
# fill gives up on narrower or denser seeds (see CrashWorld.widen_start)
MIN_OPEN_START = 4
MAX_PROGRESSION_SHARE = 0.65

class CrashItem(Item):
    game = GAME


class CrashLocation(Location):
    game = GAME


CAMPAIGN_SETS = {
    0: (1, 2, 3),
    1: (1,),
    2: (2,),
    3: (3,),
    4: (1, 2),
    5: (1, 3),
    6: (2, 3),
}


class CrashWeb(WebWorld):
    option_groups = crash_option_groups


class CrashWorld(World):
    """NST integration with selectable campaigns and checks"""

    game = GAME
    web = CrashWeb()
    options_dataclass = CrashOptions
    item_name_to_id = ITEMS
    location_name_to_id = LOCATIONS
    ut_can_gen_without_yaml = True
    required_client_version = (1, 0, 2)

    def create_item(self, name):
        classification = (
            ItemClassification.filler
            if name in (*FILLER, AKU_MASK, "Nothing")
            else ItemClassification.progression
        )
        if name in VANILLA_ITEMS:
            classification = ItemClassification.useful
        if name in TRAPS:
            classification = ItemClassification.trap
        return CrashItem(name, classification, ITEMS[name], self.player)

    def get_filler_item_name(self):
        # Filler / Traps weights; all 0 means an even mix
        weights = {
            "1-Up": self.options.one_up_weight.value,
            "Wumpa Fruit": self.options.wumpa_fruit_weight.value,
            AKU_MASK: self.options.aku_aku_mask_weight.value,
        }
        if not any(weights.values()):
            return self.random.choice(FILLER)
        return self.random.choices(list(weights), weights=list(weights.values()))[0]

    @staticmethod
    def interpret_slot_data(slot_data):
        # forward slot_data to Universal Tracker
        return slot_data

    def lock_requirements(self, names):
        return lock_requirements(names, self.level_placements, self.hub_area_locks, self.level_access_locks)

    def relic_requirements(self, level, event):
        """Extra tools logic expects before a gold or platinum relic. Vehicle stages and Crash 1 have none."""
        if event != "relic" or not self.relic_tier:
            return ()
        # on the polar bear, the jetpack and the jet board the Speed Shoes do nothing
        if level.campaign == 2 and level.key not in GIMMICK_LEVELS and level.key not in ("L203", "L211"):
            return ("Crash 2 - Speed Shoes",)
        if level.campaign == 3 and level.key not in C3_VEHICLE_LEVELS:
            # platinum expects the full Crash 3 moveset, gold only the Speed Shoes
            return ABILITIES[1:] if self.relic_tier == 2 else ("Crash 3 - Speed Shoes",)
        return ()

    def move_requirements(self, level, event):
        if not self.ability_shuffle:
            return ()
        jump_only = {easiest_start(self.starting_levels)} if self.ability_shuffle == 2 else set()
        return move_requirements(level, event, self.ability_shuffle == 2, jump_only, self.ability_logic == 1)

    def aku_requirements(self, level, event):
        """With Aku Aku Sanity, Lights Out and Fumbling in the Dark are pitch dark without Aku Aku's light, so logic
        expects the Aku Aku item for everything inside them (getting in stays open)."""
        if getattr(self, "aku_aku_sanity", False) and level.key in AKU_LIT_LEVELS:
            return (AKU_AKU,)
        return ()

    def extra_requirements(self, level, event):
        return (*self.relic_requirements(level, event), *self.move_requirements(level, event), *self.aku_requirements(level, event))

    def requirements(self, level, event):
        return self.lock_requirements((*requirements(level, event), *self.extra_requirements(level, event)))

    def box_requirements(self, level, event="box:"):
        return self.requirements(level, event)

    def check_requirements(self, level, event):
        return self.lock_requirements(
            (*check_requirements(level, event, self.active_levels), *self.extra_requirements(level, event))
        )

    def _load_tracker_slot(self):
        """Rebuild tracker state from the server slot without rerolling the seed."""
        slot = self.tracker_slot
        if slot.get("protocol") != PROTOCOL:
            raise ValueError("Unsupported NST tracker slot protocol")
        self.relic_tier = slot.get("relic_requirement", 0)
        if type(self.relic_tier) is not int or not 0 <= self.relic_tier <= 2:
            raise ValueError("Invalid NST tracker relic requirement")
        self.ability_shuffle = slot.get("ability_shuffle", 0)
        if type(self.ability_shuffle) is not int or not 0 <= self.ability_shuffle <= 2:
            raise ValueError("Invalid NST tracker ability shuffle")
        self.ability_logic = slot.get("ability_logic", 0)
        self.aku_aku_sanity = slot.get("aku_aku_sanity", False) is True
        if type(self.ability_logic) is not int or not 0 <= self.ability_logic <= 1:
            raise ValueError("Invalid NST tracker ability logic")
        self.starting_levels = slot.get("starting_levels", [])
        for name in ("hub_area_locks", "level_access_locks"):
            if type(slot.get(name)) is not bool:
                raise ValueError("Invalid NST tracker lock settings")
            setattr(self, name, slot[name])
        selected = slot["selected_levels"]
        if (
            not isinstance(selected, list)
            or not selected
            or len(set(selected)) != len(selected)
            or any(key not in BY_KEY for key in selected)
        ):
            raise ValueError("Invalid NST tracker level selection")
        self.active_levels = [BY_KEY[key] for key in selected]
        self.goal_levels = slot.get("goal_levels")
        self.victory_goal = validate_victory_goal(
            selected, self.goal_levels, slot.get("victory_goal")
        )
        self.enabled_campaigns = tuple(slot["selected_campaigns"])
        if self.enabled_campaigns not in CAMPAIGN_SETS.values() or any(
            level.campaign not in self.enabled_campaigns for level in self.active_levels
        ):
            raise ValueError("Invalid NST tracker campaigns")
        self.level_placements = slot["level_placements"]
        if set(self.level_placements) != set(selected) or set(
            self.level_placements.values()
        ) != set(selected):
            raise ValueError("Invalid NST tracker level placements")
        if any(
            source not in selected or self.level_placements[key] != key
            for key, source in SECRET_SOURCES.items()
            if key in selected
        ):
            raise ValueError(
                "Secret stages need their original source levels and entrances"
            )
        catalogue = {
            str(address): {"level": level.key, "event": event}
            for level in self.active_levels
            for event, _, address in checks(level)
        }
        crates = {
            str(crate["id"]): crate for crate in CRATES if crate["level"] in selected
        }
        catalogue.update(
            {
                address: {"level": crate["level"], "event": crate_event(crate)}
                for address, crate in crates.items()
            }
        )
        server_checks = slot["checks"]
        if not isinstance(server_checks, dict) or any(
            catalogue.get(address) != check for address, check in server_checks.items()
        ):
            raise ValueError("Invalid NST tracker locations")
        rules = slot["check_requirements"]
        if (
            not isinstance(rules, dict)
            or set(rules) != set(server_checks)
            or any(
                not isinstance(needed, list)
                or not 0 <= len(needed) <= 40
                or any(
                    type(item) is not int or item not in self.item_id_to_name
                    for item in needed
                )
                or not valid_requirement_counts(self.item_id_to_name[item] for item in needed)
                for needed in rules.values()
            )
        ):
            raise ValueError("Invalid NST tracker requirements")
        self.tracker_rules = {
            int(address): tuple(self.item_id_to_name[item] for item in needed)
            for address, needed in rules.items()
        }
        self.active_checks = {int(address) for address in server_checks}
        if self.victory_goal["type"] == "token_hunt" and self.victory_goal[
            "required"
        ] > len(self.active_checks):
            raise ValueError("Token target exceeds selected checks")
        self.active_crates = [
            crate for address, crate in crates.items() if address in server_checks
        ]
        self.locked_collectibles = {}
        self.local_reward_sources = {}
        if not isinstance(slot.get("collectible_locations"), dict) or not isinstance(
            slot.get("local_reward_sources"), dict
        ):
            raise ValueError(
                "Unsupported NST collectible data. Generate a seed with the current APWorld"
            )
        for address, source in slot["local_reward_sources"].items():
            if (
                address not in catalogue
                or address in server_checks
                or int(address) not in VANILLA_REWARDS
                or not isinstance(source, dict)
                or {key: source.get(key) for key in ("level", "event")}
                != catalogue[address]
                or source.get("item") != ITEMS.get(VANILLA_REWARDS.get(int(address)))
                or not isinstance(source.get("requirements"), list)
                or len(source["requirements"]) > 40
                or any(
                    type(item) is not int or item not in self.item_id_to_name
                    for item in source["requirements"]
                )
                or not valid_requirement_counts(self.item_id_to_name[item] for item in source["requirements"])
            ):
                raise ValueError("Invalid NST tracker local collectible")
            self.local_reward_sources[int(address)] = VANILLA_REWARDS[int(address)]

    def generate_early(self):
        self.tracker_slot = getattr(self.multiworld, "re_gen_passthrough", {}).get(GAME)
        if self.tracker_slot is not None:
            self._load_tracker_slot()
            return
        self.hub_area_locks = bool(self.options.hub_area_locks.value)
        self.level_access_locks = bool(self.options.level_access_locks.value)
        self.relic_tier = self.options.relic_requirement.value
        self.ability_shuffle = self.options.ability_shuffle.value
        self.ability_logic = self.options.ability_logic.value
        self.aku_aku_sanity = bool(self.options.aku_aku_sanity.value)
        rewards = self.options.collectible_rewards.modes()
        self.vanilla_collectibles = {
            key: mode == "vanilla" for key, mode in rewards.items()
        }
        self.selected_collectibles = {}
        self.local_reward_sources = {}
        self.shuffled_collectibles = {}
        self.precollected_collectibles = {}
        self.locked_collectibles = {}
        self.enabled_campaigns = CAMPAIGN_SETS[self.options.campaigns.value]
        pools = level_pools(self.options.level_pool.value)
        self.available_level_pool = {
            f"crash_{game}": [
                level.key for level in pools.get(game, LEVELS) if level.campaign == game
            ]
            for game in self.enabled_campaigns
        }
        counts = {
            1: self.options.crash1_levels.value,
            2: self.options.crash2_levels.value,
            3: self.options.crash3_levels.value,
        }
        regular = {
            game: [
                level
                for level in pools.get(game, LEVELS)
                if level.campaign == game and level.key.startswith("L")
            ]
            for game in self.enabled_campaigns
        }
        counts = {
            game: min(counts[game], len(levels)) for game, levels in regular.items()
        }
        chosen = {
            level.key
            for game, levels in regular.items()
            for level in self.random.sample(levels, counts[game])
        }
        chosen.update(source for key, source in SECRET_SOURCES.items() if key in chosen)
        chosen.update(key for key in FINALS if int(key[1]) in self.enabled_campaigns)
        for game, levels in regular.items():
            if game in pools:
                chosen.update(
                    level.key for level in pools[game] if level.key.startswith("B")
                )
            elif counts[game] == len(levels):
                chosen.update(
                    level.key
                    for level in LEVELS
                    if level.campaign == game and level.key.startswith("B")
                )
        goal = self.options.goal.value
        all_bosses = goal <= 3 and bool(self.options.require_all_bosses.value)
        if goal == 0:
            self.goal_levels = [key for key in FINALS if key in chosen]
        elif goal <= 3:
            self.goal_levels = [FINALS[goal - 1]]
        else:
            self.goal_levels = []
        if any(
            BY_KEY[key].campaign not in self.enabled_campaigns
            for key in self.goal_levels
        ):
            raise ValueError("The goal campaign must be enabled")
        if all_bosses:
            goal_campaigns = {BY_KEY[key].campaign for key in self.goal_levels}
            self.goal_levels = [
                level.key
                for level in LEVELS
                if level.key.startswith("B") and level.campaign in goal_campaigns
            ]
            chosen.update(self.goal_levels)
        self.active_levels = [level for level in LEVELS if level.key in chosen]
        if goal == 4:
            self.options.goal_count.value = min(
                self.options.goal_count.value,
                sum(level.key.startswith("L") for level in self.active_levels),
            )
        if goal <= 3:
            victory = {"type": "all_bosses" if all_bosses else "cortex", "required": 0}
        elif goal == 4:
            victory = {
                "type": "level_clears",
                "required": self.options.goal_count.value,
            }
        else:
            # create_items sets the final token target once filler capacity is known.
            victory = {"type": "token_hunt", "required": 1}
        self.victory_goal = validate_victory_goal(chosen, self.goal_levels, victory)
        self.level_placements = {level.key: level.key for level in self.active_levels}
        self.movement_styles = (
            {
                level.key: 1
                + int.from_bytes(
                    hashlib.sha256(
                        f"{self.multiworld.seed}/{self.player}/crashsanity/{level.key}".encode()
                    ).digest()[:4],
                    "big",
                )
                % 3
                for level in self.active_levels
                if level.key.startswith("L")
            }
            if self.options.crashsanity.value
            else {}
        )
        shuffle = self.options.level_shuffle.value
        if shuffle:
            groups = (
                [self.enabled_campaigns]
                if shuffle == 2
                else [(game,) for game in self.enabled_campaigns]
            )
            for campaigns in groups:
                entrances = [
                    level.key
                    for level in self.active_levels
                    if level.key.startswith("L")
                    and level.key not in SECRET_SOURCES
                    and level.campaign in campaigns
                ]
                destinations = sorted(
                    entrances,
                    key=lambda key: hashlib.sha256(
                        f"{self.multiworld.seed}/{self.player}/level_shuffle/{shuffle}/{key}".encode()
                    ).digest(),
                )
                self.level_placements.update(zip(entrances, destinations))
        self.starting_levels = []
        # Ability Shuffle locks most checks behind moves, so one starting level leaves too few early checks
        starting_count = max(self.options.starting_levels.value, 2 if self.ability_shuffle else 1)
        for game in self.enabled_campaigns:
            entrances = [
                level.key
                for level in self.active_levels
                if level.campaign == game
                and level.key.startswith("L")
                and level.key not in SECRET_SOURCES
            ]
            entrances.sort(key=hub_area)
            # fill the earliest hub areas before granting access to later areas
            self.starting_levels.extend(
                self.level_placements[key]
                for key in entrances[:starting_count]
            )
        choices = {
            "white_gem": self.options.white_gem_checks.value,
            "colored_gem": self.options.colored_gem_checks.value,
            "crystal": self.options.crystal_checks.value,
            "relic": self.options.relic_checks.value,
        }
        groups = {category: [] for category in choices}
        self.active_checks = set()
        for level in self.active_levels:
            for event, _, address in checks(level):
                category = check_category(level, event)
                if category == "clear":
                    self.active_checks.add(address)
                elif category == "secret_exit":
                    if self.options.secret_exit_checks:
                        self.active_checks.add(address)
                else:
                    groups[category].append(address)
        for category, addresses in groups.items():
            ranked = sorted(
                addresses,
                key=lambda address: hashlib.sha256(
                    f"{self.multiworld.seed}/{self.player}/{address}".encode()
                ).digest(),
            )
            percentage = choices[category]
            quota = (len(ranked) * percentage + 99) // 100
            selected = ranked[:quota]
            option = {
                "white_gem": "white_gems",
                "colored_gem": "colored_gems",
                "crystal": "power_crystals",
                "relic": "relics",
            }[category]
            self.active_checks.update(selected)
            self.selected_collectibles.update(
                (address, VANILLA_REWARDS[address]) for address in selected
            )
            self.local_reward_sources.update(
                (address, VANILLA_REWARDS[address]) for address in ranked[quota:]
            )
            if rewards[option] == "precollected":
                self.precollected_collectibles.update(
                    (address, VANILLA_REWARDS[address]) for address in ranked[quota:]
                )
            if not self.vanilla_collectibles[option]:
                self.shuffled_collectibles.update(
                    (address, VANILLA_REWARDS[address]) for address in selected
                )
        pool = {
            str(crate["id"]): crate
            for crate in CRATES
            if crate["level"] in chosen and breakable_crate(crate)
        }
        percentage = self.options.boxsanity.value
        sample = select_crates(
            pool, percentage, f"{self.multiworld.seed}/{self.player}/boxes"
        )
        self.active_crates = [pool[key] for key in sample]
        self.active_checks.update(crate["id"] for crate in self.active_crates)

    def create_regions(self):
        menu = Region("Menu", self.player, self.multiworld)
        self.multiworld.regions.append(menu)
        entrances = {
            destination: source for source, destination in self.level_placements.items()
        }
        for level in self.active_levels:
            region = Region(level.name, self.player, self.multiworld)
            self.multiworld.regions.append(region)
            required = self.lock_requirements(entrance_requirements(level, self.active_levels))
            menu.connect(
                region,
                name=f"{BY_KEY[entrances[level.key]].name} Entrance",
                rule=lambda state, names=Counter(required): state.has_all_counts(names, self.player),
            )
            for event, name, address in checks(level):
                if (
                    address in self.local_reward_sources
                    and self.local_reward_sources[address] in GEMS
                ):
                    location = CrashLocation(
                        self.player, f"{name} - Local Pickup Event", None, region
                    )
                    needed = (
                        tuple(
                            self.item_id_to_name[item]
                            for item in self.tracker_slot["local_reward_sources"][
                                str(address)
                            ]["requirements"]
                        )
                        if self.tracker_slot is not None
                        else self.requirements(level, event)
                    )
                    location.access_rule = lambda state, needed=Counter(needed): state.has_all_counts(
                        needed, self.player
                    )
                    location.place_locked_item(
                        CrashItem(
                            self.local_reward_sources[address],
                            ItemClassification.progression,
                            None,
                            self.player,
                        )
                    )
                    region.locations.append(location)
                if address not in self.active_checks:
                    continue
                location = CrashLocation(self.player, name, address, region)
                needed = (
                    self.tracker_rules[address]
                    if self.tracker_slot is not None
                    else self.requirements(level, event)
                )
                location.access_rule = lambda state, needed=Counter(needed): state.has_all_counts(
                    needed, self.player
                )
                region.locations.append(location)
                category = {
                    "crystal": "power_crystals",
                    "white_gem": "white_gems",
                    "colored_gem": "colored_gems",
                    "relic": "relics",
                }.get(check_category(level, event))
                if (
                    self.tracker_slot is None
                    and category
                    and self.vanilla_collectibles[category]
                ):
                    reward = VANILLA_REWARDS[address]
                    location.place_locked_item(self.create_item(reward))
                    self.locked_collectibles[address] = reward
            for crate in self.active_crates:
                if crate["level"] != level.key:
                    continue
                location = CrashLocation(
                    self.player, crate_location(crate), crate["id"], region
                )
                needed = (
                    self.tracker_rules[crate["id"]]
                    if self.tracker_slot is not None
                    else self.box_requirements(level, crate_event(crate))
                )
                location.access_rule = lambda state, needed=Counter(needed): state.has_all_counts(
                    needed, self.player
                )
                region.locations.append(location)
            if level.key in self.goal_levels:
                victory = CrashLocation(
                    self.player, f"{level.name} - Victory Event", None, region
                )
                needed = self.requirements(level, "clear")
                if self.tracker_slot is not None:
                    needed = self.tracker_rules[
                        next(
                            address
                            for event, _, address in checks(level)
                            if event == "clear"
                        )
                    ]
                victory.access_rule = lambda state, needed=Counter(needed): state.has_all_counts(
                    needed, self.player
                )
                victory.place_locked_item(
                    CrashItem(
                        f"{level.name} Defeated",
                        ItemClassification.progression,
                        None,
                        self.player,
                    )
                )
                region.locations.append(victory)
            if self.victory_goal["type"] == "level_clears" and level.key.startswith(
                "L"
            ):
                clear = CrashLocation(
                    self.player, f"{level.name} - Clear Event", None, region
                )
                needed = (
                    self.tracker_rules[
                        next(
                            address
                            for event, _, address in checks(level)
                            if event == "clear"
                        )
                    ]
                    if self.tracker_slot is not None
                    else self.requirements(level, "clear")
                )
                clear.access_rule = lambda state, needed=Counter(needed): state.has_all_counts(
                    needed, self.player
                )
                clear.place_locked_item(
                    CrashItem(
                        "Level Cleared",
                        ItemClassification.progression,
                        None,
                        self.player,
                    )
                )
                region.locations.append(clear)

    def spin_later_location(self, starts):
        """Spin Placement early_to_mid: a random check outside the starting levels that needs no Spin and only
        level or hub access items you do not start with: at least one, and no more than the starting levels
        have open checks. Returns the location, the items it still needs and the open starting checks, or None
        when the seed has no such check."""
        access = set(HUB_ITEMS.values()) | {f"{level.name} - Access" for level in LEVELS}
        starting = set(self.starting_levels)

        def spots(level):
            entrance = Counter(self.lock_requirements(entrance_requirements(level, self.active_levels)))
            for event, name, address in checks(level):
                if address in self.active_checks and address not in self.locked_collectibles:
                    yield name, entrance | Counter(self.requirements(level, event))
            for crate in self.active_crates:
                if crate["level"] == level.key:
                    yield crate_location(crate), entrance | Counter(self.box_requirements(level, crate_event(crate)))

        # empty checks you can reach from the start without Spin
        open_now = sorted(
            name
            for level in self.active_levels
            if level.key in starting
            for name, needed in spots(level)
            if "Spin" not in needed and not needed - starts
        )
        candidates = sorted(
            (name, tuple(sorted((needed - starts).elements())))
            for level in self.active_levels
            if level.key not in starting
            for name, needed in spots(level)
            if "Spin" not in needed
            and all(item in access for item in needed)
            and 1 <= sum((needed - starts).values()) <= len(open_now)
        )
        if not candidates:
            return None
        # prefer spots behind two or more unlocks, so Spin is not found straight after the starting levels
        deeper = [candidate for candidate in candidates if len(candidate[1]) >= 2]
        name, missing = self.random.choice(deeper or candidates)
        return self.multiworld.get_location(name, self.player), Counter(missing), open_now

    def open_at_start(self, starts):
        """how many active checks the starting items reach"""
        count = 0
        for level in self.active_levels:
            entrance = Counter(self.lock_requirements(entrance_requirements(level, self.active_levels)))
            if entrance - starts:
                continue
            for event, _, address in checks(level):
                if address in self.active_checks and address not in self.locked_collectibles:
                    count += not (Counter(self.requirements(level, event)) - starts)
            for crate in self.active_crates:
                if crate["level"] == level.key:
                    count += not (Counter(self.box_requirements(level, crate_event(crate))) - starts)
        return count

    def progression_share(self, starts):
        """the share of the seed's locations that progression items will fill (as create_items counts them)"""
        required = Counter(self.lock_requirements(f"{level.name} - Access" for level in self.active_levels))
        for level in self.active_levels:
            for event, _, address in checks(level):
                if address in self.active_checks or self.local_reward_sources.get(address) in GEMS:
                    required |= Counter(self.requirements(level, event))
        for crate in self.active_crates:
            required |= Counter(self.box_requirements(BY_KEY[crate["level"]], crate_event(crate)))
        required -= starts
        for name in set(self.locked_collectibles.values()) | set(self.local_reward_sources.values()) | set(self.shuffled_collectibles.values()):
            required.pop(name, None)
        gems = {name for name in self.shuffled_collectibles.values() if name in GEMS} - set(starts)
        capacity = len(self.active_checks) - len(self.locked_collectibles)
        return (sum(required.values()) + len(gems)) / max(1, capacity)

    def item_room(self, starts):
        """how many items create_items will have to place and the room it will have (before it hands out spare
        collectible rewards, which it does first when room is short): (items, room, spare rewards)"""
        required = Counter(self.lock_requirements(f"{level.name} - Access" for level in self.active_levels))
        for level in self.active_levels:
            for event, _, address in checks(level):
                if address in self.active_checks or self.local_reward_sources.get(address) in GEMS:
                    required |= Counter(self.requirements(level, event))
        for crate in self.active_crates:
            required |= Counter(self.box_requirements(BY_KEY[crate["level"]], crate_event(crate)))
        required -= starts
        locked = set(self.locked_collectibles.values())
        local_reward_items = set(self.local_reward_sources.values())
        shuffled = set(self.shuffled_collectibles.values()) - set(starts)
        for name in locked | local_reward_items | shuffled:
            required.pop(name, None)
        always = [
            name
            for name in (
                *(BASIC_MOVES if self.ability_shuffle else ()),
                *(name for name in ABILITIES if int(name[6]) in self.enabled_campaigns),
                *((AKU_AKU,) if self.options.aku_aku_sanity.value else ()),
            )
            if name not in required and name not in starts and name not in locked and name not in shuffled
            and name not in local_reward_items
        ]
        room = len(self.active_checks) - len(self.locked_collectibles)
        return sum(required.values()) + len(shuffled) + len(always), room, sum(name not in GEMS for name in shuffled), required

    def short_of_room(self, starts):
        """the seed can't hold its items ("more checks are needed"), or a Token Hunt has no slot left for tokens"""
        items, room, spare, _ = self.item_room(starts)
        token_hunt = self.victory_goal["type"] == "token_hunt"
        return items - spare > room or (token_hunt and items >= room)

    def widen_start(self, starts):
        """Fill fails on narrow or dense seeds (hub and level locks, Ability Shuffle, few check types, no boxes): one
        that opens with only one to three checks, or whose progression items fill most of its locations. Starting
        levels are added, earliest hub area first, until the start reaches MIN_OPEN_START checks and progression
        fills at most MAX_PROGRESSION_SHARE of the locations: only levels that help, at most eight more."""
        open_now, share = self.open_at_start(starts), self.progression_share(starts)
        short = self.short_of_room(starts)
        if open_now >= MIN_OPEN_START and share <= MAX_PROGRESSION_SHARE and not short:
            return starts
        candidates = [
            self.level_placements[level.key]
            for level in sorted(self.active_levels, key=lambda level: (hub_area(level.key), level.key))
            if level.key.startswith("L")
            and level.key not in SECRET_SOURCES
            and self.level_placements[level.key] not in self.starting_levels
        ]
        added = []
        for key in candidates:
            # a seed short of room takes as many starting levels as it needs; a merely narrow or dense one at most 8
            if not short and ((open_now >= MIN_OPEN_START and share <= MAX_PROGRESSION_SHARE) or len(added) >= 8):
                break
            trial = Counter(starts)
            trial |= Counter(self.lock_requirements([f"{BY_KEY[key].name} - Access"]))
            more, less = self.open_at_start(trial), self.progression_share(trial)
            if short or (open_now < MIN_OPEN_START and more > open_now) or (share > MAX_PROGRESSION_SHARE and less < share):
                self.starting_levels.append(key)
                added.append(BY_KEY[key].name)
                starts, open_now, share = trial, more, less
                short = self.short_of_room(starts)
        # levels ran out (a campaign with two or three levels): start with a few of the items checks need instead,
        # boss access, hub unlocks, gems and moves, keeping only ones that open the start or thin the progression
        if open_now < MIN_OPEN_START or share > MAX_PROGRESSION_SHARE:
            ranked = lambda name: hashlib.sha256(f"{self.multiworld.seed}/{self.player}/widen/{name}".encode()).digest()
            needed = self.item_room(starts)[3]
            tried = 0
            for name in sorted(set(needed), key=ranked):
                if (open_now >= MIN_OPEN_START and share <= MAX_PROGRESSION_SHARE) or tried >= 8:
                    break
                trial = Counter(starts)
                trial |= Counter(self.lock_requirements([name]))
                trial[name] = max(trial[name], starts[name] + 1)
                more, less = self.open_at_start(trial), self.progression_share(trial)
                if (open_now < MIN_OPEN_START and more > open_now) or (share > MAX_PROGRESSION_SHARE and less < share):
                    tried += 1
                    added.append(name)
                    starts, open_now, share = trial, more, less
        if added:
            logging.info(
                f"{GAME}: player {self.player} also starts with {', '.join(added)}, so the seed opens with {open_now} checks "
                f"and progression fills {share:.0%} of its locations"
            )
        return starts

    def create_items(self):
        if self.tracker_slot is not None:
            return
        starts = Counter(self.lock_requirements(f"{BY_KEY[key].name} - Access" for key in self.starting_levels))
        starts.update(self.precollected_collectibles.values())
        if self.ability_shuffle == 1:
            starts["Spin"] = 1
        starts = self.widen_start(starts)
        spin_locked = False
        spin_route = Counter()  # unlocks placed on starting checks so Spin stays reachable
        if self.ability_shuffle == 2:
            # only the easiest starting level and gimmick stages are open without Spin. with very few such
            # checks, fill cannot route around it, so Spin goes on one of their clears; otherwise it is placed
            # early at random
            open_checks = [
                (event, name)
                for key in self.starting_levels
                for event, name, address in checks(BY_KEY[key])
                if address in self.active_checks
                and address not in self.locked_collectibles
                and not self.move_requirements(BY_KEY[key], event)
            ]
            placement = self.options.spin_placement.value
            later = (
                self.spin_later_location(starts)
                if placement == self.options.spin_placement.option_early_to_mid
                else None
            )
            if later is not None:
                # early_to_mid: out of the starting levels, behind at least one level you unlock first
                location, missing, open_now = later
                location.place_locked_item(self.create_item("Spin"))
                spin_locked = True
                if len(open_now) < 4:
                    # so few open checks that fill could miss the only route: the unlocks Spin needs go on them
                    spots = self.random.sample(open_now, sum(missing.values()))
                    for item, spot in zip(sorted(missing.elements()), spots):
                        self.multiworld.get_location(spot, self.player).place_locked_item(self.create_item(item))
                    spin_route = missing
            elif len(open_checks) < 4:
                first_checks = sorted(name for event, name in open_checks if event in ("clear", "crystal"))
                location = self.multiworld.get_location(self.random.choice(first_checks), self.player)
                location.place_locked_item(self.create_item("Spin"))
                spin_locked = True
            elif placement != self.options.spin_placement.option_anywhere:
                self.multiworld.early_items[self.player]["Spin"] = 1
            # anywhere: fill places Spin on any check logic reaches without it
        for name in sorted(starts.elements()):
            self.multiworld.push_precollected(self.create_item(name))
        required = Counter(self.lock_requirements(f"{level.name} - Access" for level in self.active_levels))
        for level in self.active_levels:
            for event, _, address in checks(level):
                if address in self.active_checks or self.local_reward_sources.get(address) in GEMS:
                    required |= Counter(self.requirements(level, event))
        for crate in self.active_crates:
            required |= Counter(self.box_requirements(BY_KEY[crate["level"]], crate_event(crate)))
        required -= starts
        if spin_locked:
            required.pop("Spin", None)
        required -= spin_route
        locked = set(self.locked_collectibles.values())
        local_reward_items = set(self.local_reward_sources.values())
        shuffled = set(self.shuffled_collectibles.values()) - set(starts)
        for name in locked | local_reward_items | shuffled:
            required.pop(name, None)
        # every ability of the enabled campaigns and every shuffled move is always in the seed to be found,
        # even when no check needs it (the client only grants abilities as AP items)
        always = [
            name
            for name in (
                *(BASIC_MOVES if self.ability_shuffle else ()),
                *(name for name in ABILITIES if int(name[6]) in self.enabled_campaigns),
                *((AKU_AKU,) if self.options.aku_aku_sanity.value else ()),
            )
            if name not in required
            and name not in starts
            and not (spin_locked and name == "Spin")
            and name not in locked
            and name not in shuffled
            and name not in local_reward_items
        ]
        optional = [
            name
            for name in GEMS
            if name not in required
            and name not in starts
            and name not in locked
            and name not in shuffled
            and name not in local_reward_items
            and int(name[6]) in self.enabled_campaigns
        ]
        capacity = len(self.active_checks) - len(self.locked_collectibles) - spin_locked - sum(spin_route.values())
        progression_count = sum(required.values()) + len(shuffled) + len(always)
        if progression_count > capacity:
            # abilities and moves need room: hand out a few collectible rewards at the start instead of shuffling
            # them, relics first, then crystals and white gems. colored gems unlock routes and stay shuffled.
            # their checks stay checks and hold other items.
            spare = sorted(
                (name for name in shuffled if name not in GEMS),
                key=lambda name: (
                    not name.endswith(" - Relic (Any Tier)"),
                    not name.endswith(" - Power Crystal"),
                    hashlib.sha256(f"{self.multiworld.seed}/{self.player}/move_room/{name}".encode()).digest(),
                ),
            )
            for name in spare[: progression_count - capacity]:
                shuffled.discard(name)
                self.multiworld.push_precollected(self.create_item(name))
            progression_count = sum(required.values()) + len(shuffled) + len(always)
        token_hunt = self.victory_goal["type"] == "token_hunt"
        if progression_count > capacity or (token_hunt and progression_count >= capacity):
            # last resort for the tightest settings (a handful of levels and checks, every level already open): start
            # with items until everything fits, first ones no check needs (moves, abilities, Aku Aku), then ones checks
            # need. Token Hunt keeps a slot for tokens.
            need = progression_count - capacity + token_hunt
            ranked = lambda name: hashlib.sha256(f"{self.multiworld.seed}/{self.player}/room/{name}".encode()).digest()
            given = []
            for name in sorted(always, key=ranked)[:need]:
                always.remove(name)
                given.append(name)
            for name in sorted(required.elements(), key=ranked)[: need - len(given)]:
                required[name] -= 1
                given.append(name)
            required = +required
            for name in given:
                self.multiworld.push_precollected(self.create_item(name))
            logging.info(f"{GAME}: player {self.player} starts with {', '.join(given)}: the seed has too few checks to hold them")
            progression_count = sum(required.values()) + len(shuffled) + len(always)
        if progression_count > capacity:
            raise ValueError(
                f"{progression_count - capacity} more checks are needed to hold the required items, abilities "
                "and moves. Enable box checks (boxsanity) or more gem, crystal or relic checks"
            )
        free = capacity - progression_count
        extra = optional[:free]
        if self.victory_goal["type"] == "token_hunt" and free and len(extra) == free:
            # spare colored gems would take every free slot and leave Token Hunt none: half go to tokens instead
            extra = optional[: free - max(1, free // 2)]
        names = sorted([*required.elements(), *shuffled, *always]) + extra
        pool = [self.create_item(name) for name in names]
        for item in pool:
            if item.name not in required and not (
                item.name in shuffled and item.name in GEMS
            ):
                item.classification = ItemClassification.useful
        tokens = 0
        if self.victory_goal["type"] == "token_hunt":
            filler = capacity - len(pool)
            if not filler:
                raise ValueError(
                    "Token Hunt requires at least one filler slot after progression, "
                    "collectible rewards and useful items. Enable more box checks "
                    "or select more randomized checks."
                )
            tokens = (filler * self.options.token_percentage.value + 99) // 100
            self.victory_goal["required"] = (
                tokens * self.options.token_required_percentage.value + 99
            ) // 100
        pool.extend(self.create_item(TOKEN) for _ in range(tokens))
        # traps replace a share of the remaining junk, picked by their weights
        weights = {
            "Life Loss Trap": self.options.life_loss_trap_weight.value,
            "Move Lock Trap": self.options.move_lock_trap_weight.value,
            "Reverse Controls Trap": self.options.reverse_controls_trap_weight.value,
            "Freeze Trap": self.options.freeze_trap_weight.value,
            "Slippery Trap": self.options.slippery_trap_weight.value,
            "Slow Trap": self.options.slow_trap_weight.value,
            "Darkness Trap": self.options.darkness_trap_weight.value,
            "Nitro Trap": self.options.nitro_trap_weight.value,
        }
        junk = capacity - len(pool)
        traps = (junk * self.options.trap_fill_percentage.value + 99) // 100 if any(weights.values()) else 0
        self.trap_counts = dict.fromkeys(TRAPS, 0)
        for _ in range(traps):
            name = self.random.choices(list(weights), weights=list(weights.values()))[0]
            self.trap_counts[name] += 1
            pool.append(self.create_item(name))
        pool.extend(
            self.create_item(self.get_filler_item_name())
            for _ in range(capacity - len(pool))
        )
        self.multiworld.itempool.extend(pool)

    def set_rules(self):
        kind, count = self.victory_goal["type"], self.victory_goal["required"]
        if kind in ("cortex", "all_bosses"):
            self.multiworld.completion_condition[self.player] = (
                lambda state: state.has_all(
                    tuple(f"{BY_KEY[key].name} Defeated" for key in self.goal_levels),
                    self.player,
                )
            )
        else:
            self.multiworld.completion_condition[self.player] = lambda state: state.has(
                TOKEN if kind == "token_hunt" else "Level Cleared", self.player, count
            )

    def fill_slot_data(self):
        if self.tracker_slot is not None:
            return self.tracker_slot
        result = {
            "protocol": PROTOCOL,
            "hub_area_locks": self.hub_area_locks,
            "level_access_locks": self.level_access_locks,
            "prototype": True,
            "crate_checks": bool(self.active_crates),
            "crates": self.active_crates,
            "starting_levels": self.starting_levels,
            "level_shuffle": self.options.level_shuffle.value,
            "crashsanity": bool(self.options.crashsanity.value),
            "movement_styles": self.movement_styles,
            "death_link": bool(self.options.death_link.value),
            "level_placements": self.level_placements,
            "selected_campaigns": list(self.enabled_campaigns),
            "level_pool": self.available_level_pool,
            "goal_levels": self.goal_levels,
            "victory_goal": self.victory_goal,
            "selected_levels": [level.key for level in self.active_levels],
            "checks": {
                **{
                    str(address): {"level": level.key, "event": event}
                    for level in self.active_levels
                    for event, _, address in checks(level)
                    if address in self.active_checks
                },
                **{
                    str(crate["id"]): {
                        "level": crate["level"],
                        "event": crate_event(crate),
                    }
                    for crate in self.active_crates
                },
            },
        }
        result["check_requirements"] = {
            address: [
                ITEMS[name]
                for name in self.check_requirements(BY_KEY[check["level"]], check["event"])
            ]
            for address, check in result["checks"].items()
        }
        result["vanilla_collectibles"] = self.vanilla_collectibles
        result["collectible_locations"] = {
            str(address): {
                "item": ITEMS[name],
                "mode": (
                    "randomized" if address in self.shuffled_collectibles else "vanilla"
                ),
            }
            for address, name in self.selected_collectibles.items()
        }
        result["local_reward_sources"] = {
            str(address): {
                "level": level.key,
                "event": event,
                "item": ITEMS[self.local_reward_sources[address]],
                "requirements": [
                    ITEMS[name]
                    for name in self.check_requirements(level, event)
                ],
            }
            for level in self.active_levels
            for event, _, address in checks(level)
            if address in self.local_reward_sources
        }
        result["collectible_item_rewards"] = {
            str(ITEMS[name]): str(address)
            for address, name in self.shuffled_collectibles.items()
        }
        result["box_item_spoilers"] = bool(self.options.box_item_spoilers.value)
        # 0 off, 1 start with Spin, 2 Spin shuffled. Move item IDs are BASE + 2400 + index of BASIC_MOVES.
        result["ability_shuffle"] = self.ability_shuffle
        # 0 cautious, 1 relaxed: what logic expects without the basic moves
        result["ability_logic"] = self.ability_logic
        # Aku Aku Sanity: Aku Aku crates give no mask until the Aku Aku item (BASE + 2410) is received
        result["aku_aku_sanity"] = bool(self.options.aku_aku_sanity.value)
        # trap item IDs are BASE + TRAP_IDS. counts tell the client which hooks it needs.
        result["trap_counts"] = getattr(self, "trap_counts", dict.fromkeys(TRAPS, 0))
        result["trap_duration"] = self.options.trap_duration.value
        # the client picks the skin from CrashNSTAP\\skins by this name ("default_crash" is the game's own Crash)
        result["crash_skin"] = str(self.options.crash_skin.current_key)
        # Crash colors: "default", a color name or "#rrggbb" per part; the client recolors the game's own Crash.pak
        result["crash_colors"] = {
            "fur": str(self.options.crash_fur_color.current_key),
            "chest": str(self.options.crash_chest_color.current_key),
            "pants": str(self.options.crash_pants_color.current_key),
            "shoes": str(self.options.crash_shoes_color.current_key),
            "hair": str(self.options.crash_hair_color.current_key),
        }
        # 0 = any tier, 1 = gold or platinum, 2 = platinum only
        result["relic_requirement"] = self.options.relic_requirement.value
        return result
