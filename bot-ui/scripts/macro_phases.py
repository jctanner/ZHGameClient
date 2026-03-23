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
PHASE_VESTING_NAME = "vesting"
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
    "stash": 0,
    "barracks": 0,
    "arms": 0,
    "black_markets": 4,
    "tunnel_networks": 3,
    "stinger_sites": 2,
}
VESTING_BUILDING_MIX: dict[str, int] = {
    "stash": 0,
    "barracks": 0,
    "arms": 0,
    "black_markets": 1,
    "tunnel_networks": 1,
    "stinger_sites": 1,
}
WARMONGER_INFANTRY_MIX: dict[str, int] = {"soldiers": 1, "rpg": 2}
WARMONGER_VEHICLE_MIX: dict[str, int] = {"radar": 0, "quads": 4, "scorpions": 4}
EXPANSION_TO_VESTING_MONEY = 12000
EXPANSION_TO_WARMONGER_MONEY = 30000
VESTING_TO_EXPANSION_MONEY = 10000
WARMONGER_TO_EXPANSION_MONEY = 22000
MIN_COMPLETED_PALACES_BEFORE_WARMONGER = 1
MIN_BLACK_MARKETS_BEFORE_WARMONGER = 4
MIN_COMPLETED_BLACK_MARKETS_BEFORE_VESTING = 2
MIN_VESTING_NET_INCOME_PER_SEC = 5.0
MAX_VESTING_ETA_SEC = 180.0
MIN_VESTING_DURATION_SEC = 20.0


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

PHASE_VESTING = Phase(
    name=PHASE_VESTING_NAME,
    steps=(
        Step("Build vesting mix", "Script.BuildingMix", VESTING_BUILDING_MIX),
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

PLAN_PHASES: tuple[Phase, ...] = (PHASE_OPENING, PHASE_EXPANSION, PHASE_VESTING, PHASE_WARMONGER)

# Reuse the legacy sustain branch in the macro loop for the vesting phase.
PHASE_SUSTAIN_NAME = PHASE_VESTING_NAME
PHASE_SUSTAIN = PHASE_VESTING


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
    completed_black_markets: int = 0,
    net_income_per_sec: float = 0.0,
    eta_to_warmonger_sec: float | None = None,
) -> tuple[int, float, str]:
    if phase_index < 0 or phase_index >= len(PLAN_PHASES):
        return phase_index, phase_entered_at_monotonic, ""
    current_phase = PLAN_PHASES[phase_index]
    phase_duration_sec = max(0.0, now_monotonic - phase_entered_at_monotonic)
    warmonger_ready = (
        completed_palaces >= MIN_COMPLETED_PALACES_BEFORE_WARMONGER
        and completed_black_markets >= MIN_BLACK_MARKETS_BEFORE_WARMONGER
        and money >= EXPANSION_TO_WARMONGER_MONEY
    )
    vesting_ready = (
        completed_palaces >= MIN_COMPLETED_PALACES_BEFORE_WARMONGER
        and completed_black_markets >= MIN_COMPLETED_BLACK_MARKETS_BEFORE_VESTING
        and money >= EXPANSION_TO_VESTING_MONEY
        and net_income_per_sec >= MIN_VESTING_NET_INCOME_PER_SEC
        and eta_to_warmonger_sec is not None
        and eta_to_warmonger_sec > 0.0
        and eta_to_warmonger_sec <= MAX_VESTING_ETA_SEC
    )
    if current_phase.name == PHASE_EXPANSION_NAME and warmonger_ready:
        next_phase = PLAN_PHASES[3]
        print(
            f"[transition] {current_phase.name}->{next_phase.name} "
            f"reason=money_high cycle={cycle} money={money} "
            f"completed_palaces={completed_palaces} completed_black_markets={completed_black_markets}",
            flush=True,
        )
        return 3, now_monotonic, "money_high"
    if current_phase.name == PHASE_EXPANSION_NAME and vesting_ready:
        next_phase = PLAN_PHASES[2]
        print(
            f"[transition] {current_phase.name}->{next_phase.name} "
            f"reason=income_eta cycle={cycle} money={money} "
            f"net_income_per_sec={net_income_per_sec:.2f} eta_to_warmonger_sec={eta_to_warmonger_sec:.1f}",
            flush=True,
        )
        return 2, now_monotonic, "income_eta"
    if current_phase.name == PHASE_VESTING_NAME and warmonger_ready:
        next_phase = PLAN_PHASES[3]
        print(
            f"[transition] {current_phase.name}->{next_phase.name} "
            f"reason=money_high cycle={cycle} money={money} "
            f"net_income_per_sec={net_income_per_sec:.2f}",
            flush=True,
        )
        return 3, now_monotonic, "money_high"
    if current_phase.name == PHASE_VESTING_NAME:
        should_return_to_expansion = (
            money <= VESTING_TO_EXPANSION_MONEY
            or completed_palaces < MIN_COMPLETED_PALACES_BEFORE_WARMONGER
            or completed_black_markets < MIN_COMPLETED_BLACK_MARKETS_BEFORE_VESTING
            or (
                phase_duration_sec >= MIN_VESTING_DURATION_SEC
                and (
                    net_income_per_sec < MIN_VESTING_NET_INCOME_PER_SEC
                    or (eta_to_warmonger_sec is not None and eta_to_warmonger_sec > MAX_VESTING_ETA_SEC)
                )
            )
        )
        if should_return_to_expansion:
            next_phase = PLAN_PHASES[1]
            print(
                f"[transition] {current_phase.name}->{next_phase.name} "
                f"reason=income_too_low cycle={cycle} money={money} "
                f"net_income_per_sec={net_income_per_sec:.2f} "
                f"eta_to_warmonger_sec={eta_to_warmonger_sec if eta_to_warmonger_sec is not None else -1:.1f}",
                flush=True,
            )
            return 1, now_monotonic, "income_too_low"
    if current_phase.name == PHASE_WARMONGER_NAME and money <= WARMONGER_TO_EXPANSION_MONEY:
        next_phase = PLAN_PHASES[2] if completed_black_markets >= MIN_COMPLETED_BLACK_MARKETS_BEFORE_VESTING else PLAN_PHASES[1]
        print(
            f"[transition] {current_phase.name}->{next_phase.name} "
            f"reason=money_low cycle={cycle} money={money} net_income_per_sec={net_income_per_sec:.2f}",
            flush=True,
        )
        return PLAN_PHASES.index(next_phase), now_monotonic, "money_low"
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
