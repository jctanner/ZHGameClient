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
PHASE_SUSTAIN_NAME = "sustain"
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

SUSTAIN_BUILDING_MIX: dict[str, int] = {
    "stash": 1,
    "barracks": 1,
    "arms": 1,
    "black_markets": 2,
    "tunnel_networks": 6,
    "stinger_sites": 6,
}
SUSTAIN_INFANTRY_MIX: dict[str, int] = {"soldiers": 1, "rpg": 2}
SUSTAIN_VEHICLE_MIX: dict[str, int] = {"radar": 0, "quads": 3, "scorpions": 3}


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

PHASE_SUSTAIN = Phase(
    name=PHASE_SUSTAIN_NAME,
    steps=(
        Step("Build sustain mix", "Script.BuildingMix", SUSTAIN_BUILDING_MIX),
        Step("Queue vehicle mix sustain", "Script.QueueVehicleMix", SUSTAIN_VEHICLE_MIX),
        Step("Queue infantry mix sustain", "Script.QueueInfantryMix", SUSTAIN_INFANTRY_MIX),
    ),
    exit_when=(),
)

PLAN_PHASES: tuple[Phase, ...] = (PHASE_OPENING, PHASE_SUSTAIN)


def maybe_transition_phase(
    phase_index: int,
    opening_step_index: int,
    assets: dict[str, int],
    opening_core_ready: bool,
    money: int,
    now_monotonic: float,
    phase_entered_at_monotonic: float,
    cycle: int,
) -> tuple[int, float, str]:
    del money
    if phase_index < 0 or phase_index >= len(PLAN_PHASES):
        return phase_index, phase_entered_at_monotonic, ""
    current_phase = PLAN_PHASES[phase_index]
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
