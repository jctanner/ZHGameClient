from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class Step:
    name: str
    cmd: str
    args: dict[str, Any]
    repeat: int = 1


@dataclass(frozen=True)
class Phase:
    name: str
    steps: tuple[Step, ...]
    exit_when: tuple[str, ...] = ()


PHASE_OPENING_NAME = "opening"
PHASE_EXPANSION_NAME = "expansion"
PHASE_WARMONGER_NAME = "warmonger"
RULE_PALACE_STARTED = "palace_started"
RULE_OPENING_COMPLETE = "opening_complete"

OPENING_BUILDING_MIX: dict[str, int] = {
    "stash": 2,
    "barracks": 1,
    "arms": 1,
    "tunnel_networks": 4,
    "stinger_sites": 4,
}
OPENING_INFANTRY_MIX: dict[str, int] = {"soldiers": 2, "rpg": 4}
OPENING_VEHICLE_MIX: dict[str, int] = {"radar": 0, "quads": 3, "scorpions": 3}

EXPANSION_BUILDING_MIX: dict[str, int] = {
    "stash": 1,
    "barracks": 1,
    "arms": 1,
    "black_markets": 2,
    "tunnel_networks": 6,
    "stinger_sites": 6,
}
WARMONGER_INFANTRY_MIX: dict[str, int] = {"soldiers": 1, "rpg": 2}
WARMONGER_VEHICLE_MIX: dict[str, int] = {"radar": 0, "quads": 4, "scorpions": 4}
EXPANSION_TO_WARMONGER_MONEY = 30000
WARMONGER_TO_EXPANSION_MONEY = 22000
MIN_COMPLETED_PALACES_BEFORE_WARMONGER = 1
MIN_BLACK_MARKETS_BEFORE_WARMONGER = 4


PHASE_OPENING = Phase(
    name=PHASE_OPENING_NAME,
    steps=(
        Step("Build 9 workers", "Game.BuildWorker", {"producer_kind": "command_center"}, repeat=9),
        Step("Build opening mix", "Script.BuildingMix", OPENING_BUILDING_MIX),
        Step("Queue infantry mix opening", "Script.QueueInfantryMix", OPENING_INFANTRY_MIX),
        Step("Queue vehicle mix opening", "Script.QueueVehicleMix", OPENING_VEHICLE_MIX),
    ),
    exit_when=(RULE_PALACE_STARTED, RULE_OPENING_COMPLETE),
)

PHASE_EXPANSION = Phase(
    name=PHASE_EXPANSION_NAME,
    steps=(
        Step("Build expansion mix", "Script.BuildingMix", EXPANSION_BUILDING_MIX),
    ),
    exit_when=(),
)

PHASE_WARMONGER = Phase(
    name=PHASE_WARMONGER_NAME,
    steps=(
        Step("Queue vehicle mix warmonger", "Script.QueueVehicleMix", WARMONGER_VEHICLE_MIX),
        Step("Queue infantry mix warmonger", "Script.QueueInfantryMix", WARMONGER_INFANTRY_MIX),
    ),
    exit_when=(),
)

PLAN_PHASES: tuple[Phase, ...] = (PHASE_OPENING, PHASE_EXPANSION, PHASE_WARMONGER)

# Temporary compatibility aliases while the main loop is migrated off the old sustain branch.
PHASE_SUSTAIN_NAME = PHASE_EXPANSION_NAME
PHASE_SUSTAIN = PHASE_EXPANSION


def maybe_transition_phase(
    phase_index: int,
    opening_step_index: int,
    assets: dict[str, int],
    completed_palaces: int,
    opening_core_ready: bool,
    money: int,
    now_monotonic: float,
    phase_entered_at_monotonic: float,
    cycle: int,
) -> tuple[int, float, str]:
    if phase_index < 0 or phase_index >= len(PLAN_PHASES):
        return phase_index, phase_entered_at_monotonic, ""
    current_phase = PLAN_PHASES[phase_index]
    expansion_eco_ready = (
        completed_palaces >= MIN_COMPLETED_PALACES_BEFORE_WARMONGER
        and assets.get("black_markets", 0) >= MIN_BLACK_MARKETS_BEFORE_WARMONGER
    )
    if current_phase.name == PHASE_EXPANSION_NAME and expansion_eco_ready and money >= EXPANSION_TO_WARMONGER_MONEY:
        next_phase = PLAN_PHASES[2]
        print(
            f"[transition] {current_phase.name}->{next_phase.name} "
            f"reason=money_high cycle={cycle} money={money} "
            f"completed_palaces={completed_palaces} black_markets={assets.get('black_markets', 0)}",
            flush=True,
        )
        return 2, now_monotonic, "money_high"
    if current_phase.name == PHASE_WARMONGER_NAME and money <= WARMONGER_TO_EXPANSION_MONEY:
        next_phase = PLAN_PHASES[1]
        print(
            f"[transition] {current_phase.name}->{next_phase.name} "
            f"reason=money_low cycle={cycle} money={money}",
            flush=True,
        )
        return 1, now_monotonic, "money_low"
    for rule in current_phase.exit_when:
        if rule == RULE_PALACE_STARTED and assets.get("palaces", 0) > 0 and opening_core_ready:
            if phase_index + 1 < len(PLAN_PHASES):
                next_phase = PLAN_PHASES[phase_index + 1]
                print(
                    f"[transition] {current_phase.name}->{next_phase.name} "
                    f"reason={RULE_PALACE_STARTED} cycle={cycle}",
                    flush=True,
                )
                return phase_index + 1, now_monotonic, RULE_PALACE_STARTED
        if rule == RULE_OPENING_COMPLETE and opening_step_index >= len(current_phase.steps):
            if phase_index + 1 < len(PLAN_PHASES):
                next_phase = PLAN_PHASES[phase_index + 1]
                print(
                    f"[transition] {current_phase.name}->{next_phase.name} "
                    f"reason={RULE_OPENING_COMPLETE} cycle={cycle}",
                    flush=True,
                )
                return phase_index + 1, now_monotonic, RULE_OPENING_COMPLETE
    return phase_index, phase_entered_at_monotonic, ""
