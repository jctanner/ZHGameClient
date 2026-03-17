from __future__ import annotations

import argparse
import random
import sys
import time
from pathlib import Path
from typing import Any, TextIO

# Reuse bot-ui protocol code without duplicating transport/message logic.
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from protocol.client import PipeClient
from protocol.messages import hello_message, session_command_message
from macro_phases import (
    PHASE_OPENING,
    PHASE_OPENING_NAME,
    PHASE_SUSTAIN,
    PLAN_PHASES,
    Step,
    maybe_transition_phase,
)


class TeeWriter:
    def __init__(self, primary: TextIO, secondary: TextIO) -> None:
        self._primary = primary
        self._secondary = secondary

    def write(self, data: str) -> int:
        self._primary.write(data)
        self._secondary.write(data)
        return len(data)

    def flush(self) -> None:
        self._primary.flush()
        self._secondary.flush()


STASH_WORKER_TARGET = 9
COMMAND_CENTER_WORKER_TARGET = 2
PALACE_MARKET_RATIO = 8
MIN_MONEY_FOR_PALACE_ATTEMPT = 7000
DEFAULT_ZONE_RADIUS = 420.0
DEFAULT_ZONE_STEP = 900.0
DEFAULT_ZONE_COUNT = 64
SUSTAIN_MARKET_ATTEMPT_EVERY = 3
SUSTAIN_PALACE_ATTEMPT_EVERY = 8
SUSTAIN_STASH_EVERY_CYCLES = 6
SUSTAIN_STASH_MIN_MONEY = 3500
SUSTAIN_BLACK_MARKET_MIN_MONEY = 2500
DEFAULT_PENDING_COOLDOWN_SEC = 20.0
BUDGET_RESERVE_CASH = 5000
BUDGET_INFANTRY_MIX_MIN = 1200
BUDGET_VEHICLE_MIX_MIN = 2600
OBJECTS_FULL_REFRESH_EVERY = 0
OBJECTS_CACHE_REFRESH_EVERY_CYCLES = 6
OBJECTS_UNITS_QUERY_EVERY_CYCLES = 0
OBJECTS_BUILDINGS_QUERY_EVERY_CYCLES = 3
HEAVY_QUERY_BACKOFF_SEC = 20.0
IDLE_WORKERS_QUERY_EVERY_CYCLES = 2
UNIT_COMPOSITION_QUERY_EVERY_CYCLES = 1
DEFAULT_RAID_MIN_UNITS = 50
DEFAULT_RAID_GROUP_SIZE = 50
DEFAULT_RAID_EVERY_CYCLES = 4
DEFAULT_RAID_DISTANCE = 3000.0
DEFAULT_RAID_COOLDOWN_SEC = 20.0
DEFAULT_GUARD_IDLE_EVERY_CYCLES = 3
DEFAULT_GUARD_IDLE_COOLDOWN_SEC = 10.0
WORKER_TRICKLE_PER_PRODUCER_PER_TICK = 2
DEFAULT_WORKER_TOPUP_COOLDOWN_SEC = 1.0
DEFAULT_WORKER_TOPUP_MIN_MONEY = 3000
DEFAULT_IDLE_WORKERS_SKIP_TOPUP_THRESHOLD = 1
RADAR_KEEPALIVE_INFLIGHT_SEC = 90.0
RADAR_KEEPALIVE_WATCHDOG_SEC = 240.0
DEFAULT_LOW_MONEY_THRESHOLD = 3500
DEFAULT_LOW_MONEY_SKIP_CYCLES = 3
ZONE_SINGLETON_BUILD_RULES: dict[str, tuple[str, ...]] = {
    "Game.BuildCommandCenterSmart": ("commandcenter",),
    "Game.BuildSupplyStashSmart": ("supplystash", "supplycenter"),
    "Game.BuildPalaceSmart": ("palace",),
}


def send_request(client: PipeClient, payload: dict[str, Any], timeout_ms: int) -> dict[str, Any]:
    return client.request_once(payload, timeout_ms=timeout_ms)


def send_session_command(
    client: PipeClient,
    cmd: str,
    args: dict[str, Any],
    timeout_ms: int,
) -> dict[str, Any]:
    payload = session_command_message(cmd, args)
    return send_request(client, payload, timeout_ms)


def try_send_session_command(
    client: PipeClient,
    cmd: str,
    args: dict[str, Any],
    timeout_ms: int,
) -> dict[str, Any] | None:
    try:
        return send_session_command(client, cmd, args, timeout_ms)
    except TimeoutError:
        path_note = f" path={args.get('path')}" if isinstance(args, dict) and isinstance(args.get("path"), str) else ""
        print(f"timeout waiting for {cmd} reply{path_note}; will retry", flush=True)
        return None
    except Exception as exc:  # noqa: BLE001
        print(f"request failed cmd={cmd}: {exc}", flush=True)
        return None


def extract_payload(msg: dict[str, Any]) -> Any:
    for key in ("result", "data", "payload", "value"):
        if key not in msg:
            continue
        value = msg[key]
        if isinstance(value, dict):
            for inner in ("result", "data", "payload", "value"):
                nested = value.get(inner)
                if isinstance(nested, (dict, list)):
                    return nested
        return value
    return msg


def query_money(client: PipeClient, timeout_ms: int, previous_money: int) -> int:
    resp = try_send_session_command(client, "Game.Query", {"path": "game.resources"}, timeout_ms)
    if resp is None:
        return previous_money
    if resp.get("ok") is False:
        return previous_money

    node = extract_payload(resp)
    if isinstance(node, dict):
        resources = node.get("resources")
        if isinstance(resources, dict) and isinstance(resources.get("money"), int):
            return int(resources["money"])
        if isinstance(node.get("money"), int):
            return int(node["money"])
    return previous_money


def query_objects_full(client: PipeClient, timeout_ms: int) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    resp = try_send_session_command(client, "Game.Query", {"path": "game.objects"}, timeout_ms)
    if resp is None:
        return [], []
    if resp.get("ok") is False:
        return [], []

    payload = extract_payload(resp)
    if not isinstance(payload, dict):
        return [], []

    units = payload.get("units")
    buildings = payload.get("buildings")
    return (
        units if isinstance(units, list) else [],
        buildings if isinstance(buildings, list) else [],
    )


def query_objects_compact(
    client: PipeClient,
    timeout_ms: int,
    include_units: bool = True,
    include_buildings: bool = True,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    units_resp = (
        try_send_session_command(client, "Game.Query", {"path": "game.objects_units_map"}, timeout_ms)
        if include_units
        else None
    )
    buildings_resp = (
        try_send_session_command(client, "Game.Query", {"path": "game.objects_buildings_map"}, timeout_ms)
        if include_buildings
        else None
    )

    units: list[dict[str, Any]] = []
    buildings: list[dict[str, Any]] = []

    if units_resp is not None and units_resp.get("ok") is not False:
        units_payload = extract_payload(units_resp)
        if isinstance(units_payload, dict):
            rows = units_payload.get("units")
            if isinstance(rows, list):
                units = rows
        elif isinstance(units_payload, list):
            units = units_payload

    if buildings_resp is not None and buildings_resp.get("ok") is not False:
        buildings_payload = extract_payload(buildings_resp)
        if isinstance(buildings_payload, dict):
            rows = buildings_payload.get("buildings")
            if isinstance(rows, list):
                buildings = rows
        elif isinstance(buildings_payload, list):
            buildings = buildings_payload

    return units, buildings


def refresh_objects_cache(client: PipeClient, timeout_ms: int) -> bool:
    resp = try_send_session_command(client, "Game.Query", {"path": "game.objects_cache_refresh"}, timeout_ms)
    if resp is None:
        return False
    if resp.get("ok") is False:
        return False
    payload = extract_payload(resp)
    cache_version = payload.get("cache_version") if isinstance(payload, dict) else None
    units_total = payload.get("units_total") if isinstance(payload, dict) else None
    buildings_total = payload.get("buildings_total") if isinstance(payload, dict) else None
    print(
        f"[cache] refresh ok=True version={cache_version} units_total={units_total} buildings_total={buildings_total}",
        flush=True,
    )
    return True


def query_objects_cache_status(client: PipeClient, timeout_ms: int) -> dict[str, Any] | None:
    resp = try_send_session_command(client, "Game.Query", {"path": "game.objects_cache_status"}, timeout_ms)
    if resp is None or resp.get("ok") is False:
        return None
    payload = extract_payload(resp)
    if not isinstance(payload, dict):
        return None
    return payload


def query_zone_counts(
    client: PipeClient,
    timeout_ms: int,
    zone_center: tuple[float, float] | None = None,
    zone_radius: float | None = None,
) -> dict[str, Any] | None:
    args: dict[str, Any] = {"path": "game.zone_counts"}
    if zone_center is not None:
        args["zone_center"] = {"x": float(zone_center[0]), "y": float(zone_center[1])}
    if zone_radius is not None:
        args["zone_radius"] = float(zone_radius)
    resp = try_send_session_command(client, "Game.Query", args, timeout_ms)
    if resp is None or resp.get("ok") is False:
        return None
    payload = extract_payload(resp)
    if not isinstance(payload, dict):
        return None
    counts = payload.get("counts")
    return counts if isinstance(counts, dict) else None


def query_idle_workers_count(client: PipeClient, timeout_ms: int, previous_count: int) -> int:
    resp = try_send_session_command(client, "Game.Query", {"path": "game.idle_workers"}, timeout_ms)
    if resp is None or resp.get("ok") is False:
        return previous_count
    payload = extract_payload(resp)
    if isinstance(payload, dict):
        total = payload.get("idle_workers_total")
        if isinstance(total, int):
            return max(0, total)
        workers = payload.get("workers")
        if isinstance(workers, list):
            return max(0, len(workers))
    return previous_count


def query_unit_composition(client: PipeClient, timeout_ms: int, previous_counts: dict[str, int] | None = None) -> dict[str, int]:
    resp = try_send_session_command(client, "Game.Query", {"path": "game.unit_composition"}, timeout_ms)
    if resp is None or resp.get("ok") is False:
        return dict(previous_counts or {})
    payload = extract_payload(resp)
    if not isinstance(payload, dict):
        return dict(previous_counts or {})
    keys = (
        "workers",
        "rebels",
        "rpg",
        "radar_vans",
        "quads",
        "scorpions",
        "combat_units",
        "ground_combat_units",
        "units_total",
    )
    counts = dict(previous_counts or {})
    for key in keys:
        value = payload.get(key)
        if isinstance(value, int):
            counts[key] = max(0, int(value))
    return counts


def merge_compact_rows(
    full_rows: list[dict[str, Any]],
    compact_rows: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    if not compact_rows:
        return full_rows
    by_id: dict[int, dict[str, Any]] = {}
    for row in full_rows:
        if not isinstance(row, dict):
            continue
        object_id = parse_object_id(row)
        if object_id is None:
            continue
        by_id[object_id] = row

    merged: list[dict[str, Any]] = []
    for compact in compact_rows:
        if not isinstance(compact, dict):
            continue
        object_id = parse_object_id(compact)
        if object_id is None:
            continue
        base = dict(by_id.get(object_id, {}))
        for key in (
            "id",
            "object_id",
            "class",
            "x",
            "y",
            "z",
            "under_construction",
            "idle",
            "template",
            "template_name",
        ):
            if key in compact:
                base[key] = compact[key]
        if "template" not in base and "template_name" not in base:
            base["template"] = ""
        merged.append(base)
    return merged if merged else full_rows


def camera_set_top_down(client: PipeClient, timeout_ms: int, height_multiplier: float) -> None:
    base_height: float | None = None
    camera_get_resp = try_send_session_command(client, "Game.Camera.Get", {}, timeout_ms)
    if camera_get_resp is not None and camera_get_resp.get("ok") is not False:
        payload = extract_payload(camera_get_resp)
        if isinstance(payload, dict):
            default_height = payload.get("default_height")
            current_height = payload.get("height_above_ground")
            if isinstance(default_height, (int, float)) and float(default_height) > 0.0:
                base_height = float(default_height)
            elif isinstance(current_height, (int, float)) and float(current_height) > 0.0:
                base_height = float(current_height)

    if height_multiplier > 1.0:
        zoom_limit_resp = try_send_session_command(
            client,
            "Game.Camera.SetZoomLimited",
            {"enabled": False},
            timeout_ms,
        )
        if zoom_limit_resp is not None:
            print(
                f"[camera] set_zoom_limited enabled=false ok={zoom_limit_resp.get('ok')} "
                f"code={zoom_limit_resp.get('code')} reason={zoom_limit_resp.get('reason')}",
                flush=True,
            )

    camera_set_args: dict[str, Any] = {
        "top_down": True,
        "angle": 0.0,
    }
    if base_height is not None:
        camera_set_args["height"] = base_height * height_multiplier
    else:
        camera_set_args["height_multiplier"] = height_multiplier

    resp = try_send_session_command(client, "Game.Camera.Set", camera_set_args, timeout_ms)
    if resp is None:
        return
    print(
        f"[camera] set top_down base_height={base_height} multiplier={height_multiplier} "
        f"ok={resp.get('ok')} code={resp.get('code')} reason={resp.get('reason')}",
        flush=True,
    )


def camera_look_at(client: PipeClient, timeout_ms: int, x: float, y: float) -> None:
    resp = try_send_session_command(client, "Game.Camera.LookAt", {"x": x, "y": y}, timeout_ms)
    if resp is None:
        return
    print(
        f"[camera] look_at x={x:.1f} y={y:.1f} ok={resp.get('ok')} code={resp.get('code')} reason={resp.get('reason')}",
        flush=True,
    )


def parse_object_id(row: dict[str, Any]) -> int | None:
    value = row.get("id", row.get("object_id"))
    if not isinstance(value, int):
        return None
    return value


def is_supply_stash_template(template_name: str) -> bool:
    text = template_name.lower()
    return ("supplystash" in text) or ("supplycenter" in text)


def is_command_center_template(template_name: str) -> bool:
    return "commandcenter" in template_name.lower()


def template_text(row: dict[str, Any]) -> str:
    return str(row.get("template", row.get("template_name", ""))).lower()


def count_matching_templates(rows: list[dict[str, Any]], needles: tuple[str, ...]) -> int:
    total = 0
    for row in rows:
        if not isinstance(row, dict):
            continue
        text = template_text(row)
        if any(needle in text for needle in needles):
            total += 1
    return total


def count_complete_matching_templates(rows: list[dict[str, Any]], needles: tuple[str, ...]) -> int:
    total = 0
    for row in rows:
        if not isinstance(row, dict):
            continue
        if bool(row.get("under_construction", False)):
            continue
        text = template_text(row)
        if any(needle in text for needle in needles):
            total += 1
    return total


def resolve_primary_anchor_xy(buildings: list[dict[str, Any]]) -> tuple[float, float]:
    for row in buildings:
        if not isinstance(row, dict):
            continue
        text = template_text(row)
        if "commandcenter" not in text:
            continue
        x = row.get("x")
        y = row.get("y")
        if isinstance(x, (int, float)) and isinstance(y, (int, float)):
            return float(x), float(y)

    for row in buildings:
        if not isinstance(row, dict):
            continue
        x = row.get("x")
        y = row.get("y")
        if isinstance(x, (int, float)) and isinstance(y, (int, float)):
            return float(x), float(y)

    return 512.0, 512.0


def build_zone_centers(anchor_x: float, anchor_y: float, step: float, count: int) -> list[tuple[float, float]]:
    centers: list[tuple[float, float]] = [(anchor_x, anchor_y)]
    if count <= 1:
        return centers
    row = 0
    col = 1
    direction = 1
    while len(centers) < count:
        x = anchor_x + (col * step)
        y = anchor_y + (row * step)
        centers.append((x, y))
        col += direction
        if col > 2:
            direction = -1
            row += 1
            col = 2
        elif col < -2:
            direction = 1
            row += 1
            col = -2
    return centers[:count]


def collect_producer_ids(buildings: list[dict[str, Any]]) -> tuple[list[int], list[int]]:
    command_centers: list[int] = []
    stashes: list[int] = []
    for row in buildings:
        if not isinstance(row, dict):
            continue
        text = template_text(row)
        object_id = parse_object_id(row)
        if object_id is None:
            continue
        if is_command_center_template(text):
            command_centers.append(object_id)
        if is_supply_stash_template(text):
            stashes.append(object_id)
    command_centers = sorted(set(command_centers))
    stashes = sorted(set(stashes))
    return command_centers, stashes


def distribute_existing_workers(
    producer_ids: list[int],
    producer_target: int,
    available_workers: int,
) -> dict[int, int]:
    issued: dict[int, int] = {}
    remaining = max(0, available_workers)
    for producer_id in producer_ids:
        if remaining <= 0:
            issued[producer_id] = 0
            continue
        used = min(producer_target, remaining)
        issued[producer_id] = used
        remaining -= used
    return issued


def initialize_plan_progress(unit_counts: dict[str, int], buildings: list[dict[str, Any]]) -> tuple[int, int]:
    workers = int(unit_counts.get("workers", 0))
    rebels = int(unit_counts.get("rebels", 0))
    rpg = int(unit_counts.get("rpg", 0))
    radar_vans = int(unit_counts.get("radar_vans", 0))
    quads = int(unit_counts.get("quads", 0))
    scorpions = int(unit_counts.get("scorpions", 0))
    stashes = count_complete_matching_templates(buildings, ("supplystash", "supplycenter"))
    barracks = count_complete_matching_templates(buildings, ("barracks",))
    arms = count_complete_matching_templates(buildings, ("armsdealer", "warfactory"))

    progress_by_step: list[int] = [
        min(workers, 9),
        1 if (stashes >= 2 and barracks >= 1 and arms >= 1) else 0,
        1 if (rebels >= 3 and rpg >= 6) else 0,
        1 if (radar_vans >= 1 and quads >= 3 and scorpions >= 3) else 0,
    ]

    step_index = 0
    while step_index < len(PHASE_OPENING.steps) and progress_by_step[step_index] >= PHASE_OPENING.steps[step_index].repeat:
        step_index += 1
    step_attempt_count = 0
    if step_index < len(PHASE_OPENING.steps):
        step_attempt_count = progress_by_step[step_index]
    return step_index, step_attempt_count


def opening_building_mix_ready(buildings: list[dict[str, Any]]) -> bool:
    stashes = count_complete_matching_templates(buildings, ("supplystash", "supplycenter"))
    barracks = count_complete_matching_templates(buildings, ("barracks",))
    arms = count_complete_matching_templates(buildings, ("armsdealer", "warfactory"))
    return stashes >= 2 and barracks >= 1 and arms >= 1


def sync_producers(
    buildings: list[dict[str, Any]],
    command_center_worker_issued: dict[int, int],
    stash_worker_issued: dict[int, int],
) -> None:
    for row in buildings:
        if not isinstance(row, dict):
            continue
        template = str(row.get("template", row.get("template_name", "")))
        object_id = parse_object_id(row)
        if object_id is None:
            continue
        if is_command_center_template(template):
            command_center_worker_issued.setdefault(object_id, 0)
        if is_supply_stash_template(template):
            stash_worker_issued.setdefault(object_id, 0)


def recompute_producer_worker_issued(
    buildings: list[dict[str, Any]],
    existing_workers: int,
) -> tuple[dict[int, int], dict[int, int], int]:
    command_center_ids, stash_ids = collect_producer_ids(buildings)
    command_center_worker_issued = distribute_existing_workers(
        command_center_ids,
        COMMAND_CENTER_WORKER_TARGET,
        existing_workers,
    )
    consumed = sum(command_center_worker_issued.values())
    stash_worker_issued = distribute_existing_workers(
        stash_ids,
        STASH_WORKER_TARGET,
        max(0, existing_workers - consumed),
    )
    return command_center_worker_issued, stash_worker_issued, existing_workers


def pick_producer_needing_workers(worker_issued: dict[int, int], target: int) -> int | None:
    candidates = [producer_id for producer_id, issued in worker_issued.items() if issued < target]
    if not candidates:
        return None
    return sorted(candidates)[0]


def pick_stash_needing_workers(stash_worker_issued: dict[int, int]) -> int | None:
    return pick_producer_needing_workers(stash_worker_issued, STASH_WORKER_TARGET)


def pick_command_center_needing_workers(command_center_worker_issued: dict[int, int]) -> int | None:
    return pick_producer_needing_workers(command_center_worker_issued, COMMAND_CENTER_WORKER_TARGET)


def issue_worker_for_producer(
    client: PipeClient,
    timeout_ms: int,
    cycle: int,
    producer_label: str,
    producer_id: int,
    producer_target: int,
    producer_worker_issued: dict[int, int],
) -> int:
    remaining = max(0, producer_target - producer_worker_issued.get(producer_id, 0))
    if remaining <= 0:
        return 0
    remaining = min(remaining, WORKER_TRICKLE_PER_PRODUCER_PER_TICK)

    sent_ok = 0
    last_code: Any = None
    last_reason: Any = None
    for _ in range(remaining):
        resp = try_send_session_command(
            client, "Game.BuildWorker", {"producer_kind": "any", "producer_object_id": producer_id}, timeout_ms
        )
        if resp is None:
            break
        ok = bool(resp.get("ok", False))
        last_code = resp.get("code")
        last_reason = resp.get("reason")
        if not ok:
            break
        sent_ok += 1

    if sent_ok > 0:
        producer_worker_issued[producer_id] = producer_worker_issued.get(producer_id, 0) + sent_ok

    print(
        f"[loop {cycle}] {producer_label}_workers producer_id={producer_id} "
        f"added={sent_ok} issued={producer_worker_issued.get(producer_id, 0)}/{producer_target} "
        f"last_code={last_code} last_reason={last_reason}",
        flush=True,
    )
    return sent_ok


def top_up_workers_for_producers(
    client: PipeClient,
    timeout_ms: int,
    cycle: int,
    producer_label: str,
    producer_target: int,
    producer_worker_issued: dict[int, int],
    pending_until_by_key: dict[str, float],
    now: float,
    pending_cooldown_sec: float,
) -> int:
    total_added = 0
    for producer_id in sorted(producer_worker_issued):
        if producer_worker_issued.get(producer_id, 0) >= producer_target:
            continue
        pending_key = f"Game.BuildWorker:{producer_id}"
        if is_pending(pending_until_by_key, pending_key, now):
            continue
        added = issue_worker_for_producer(
            client=client,
            timeout_ms=timeout_ms,
            cycle=cycle,
            producer_label=producer_label,
            producer_id=producer_id,
            producer_target=producer_target,
            producer_worker_issued=producer_worker_issued,
        )
        if added > 0:
            mark_pending(
                pending_until_by_key,
                pending_key,
                now,
                pending_cooldown_sec,
            )
            total_added += added
    return total_added


def summarize_assets(unit_counts: dict[str, int], buildings: list[dict[str, Any]]) -> dict[str, int]:
    return {
        "palaces": count_matching_templates(buildings, ("palace",)),
        "black_markets": count_matching_templates(buildings, ("blackmarket", "black_market")),
        "arms": count_matching_templates(buildings, ("armsdealer", "warfactory")),
        "barracks": count_matching_templates(buildings, ("barracks",)),
        "stashes": count_matching_templates(buildings, ("supplystash", "supplycenter")),
        "workers": int(unit_counts.get("workers", 0)),
        "rebels": int(unit_counts.get("rebels", 0)),
        "rpg": int(unit_counts.get("rpg", 0)),
        "quads": int(unit_counts.get("quads", 0)),
        "scorpions": int(unit_counts.get("scorpions", 0)),
        "radar_vans": int(unit_counts.get("radar_vans", 0)),
        "combat_units": int(unit_counts.get("combat_units", 0)),
        "ground_combat_units": int(unit_counts.get("ground_combat_units", 0)),
        "units_total": int(unit_counts.get("units_total", 0)),
    }


def try_burst_step(client: PipeClient, step: Step, timeout_ms: int) -> tuple[int, Any, Any]:
    sent_ok = 0
    last_code: Any = None
    last_reason: Any = None
    for _ in range(max(1, step.repeat)):
        resp = try_send_session_command(client, step.cmd, step.args, timeout_ms)
        if resp is None:
            break
        ok = bool(resp.get("ok", False))
        last_code = resp.get("code")
        last_reason = resp.get("reason")
        if not ok:
            break
        sent_ok += 1
    return sent_ok, last_code, last_reason


def with_zone_args(step: Step, zone_center: tuple[float, float], zone_radius: float) -> Step:
    if not step.cmd.startswith("Game.Build"):
        return step
    args = dict(step.args)
    args["zone_center"] = {"x": zone_center[0], "y": zone_center[1]}
    args["zone_radius"] = zone_radius
    args["strict_zone"] = False
    return Step(step.name, step.cmd, args, repeat=step.repeat)


def has_palace_started(buildings: list[dict[str, Any]]) -> bool:
    return count_matching_templates(buildings, ("palace",)) > 0


def is_row_in_zone(row: dict[str, Any], zone_center: tuple[float, float], zone_radius: float) -> bool:
    x = row.get("x")
    y = row.get("y")
    if not isinstance(x, (int, float)) or not isinstance(y, (int, float)):
        return False
    dx = float(x) - zone_center[0]
    dy = float(y) - zone_center[1]
    return (dx * dx + dy * dy) <= (zone_radius * zone_radius)


def zone_building_state_counts(
    buildings: list[dict[str, Any]],
    zone_center: tuple[float, float],
    zone_radius: float,
    template_needles: tuple[str, ...],
) -> tuple[int, int]:
    complete = 0
    under_construction = 0
    for row in buildings:
        if not isinstance(row, dict):
            continue
        if not is_row_in_zone(row, zone_center, zone_radius):
            continue
        text = template_text(row)
        if not any(needle in text for needle in template_needles):
            continue
        if bool(row.get("under_construction", False)):
            under_construction += 1
        else:
            complete += 1
    return complete, under_construction


def collect_combat_unit_ids(units: list[dict[str, Any]]) -> list[int]:
    ids: list[int] = []
    for row in units:
        if not isinstance(row, dict):
            continue
        object_id = parse_object_id(row)
        if object_id is None:
            continue
        text = template_text(row)
        if "worker" in text or "dozer" in text:
            continue
        ids.append(object_id)
    return ids


def choose_raid_target(anchor_x: float, anchor_y: float, distance: float) -> tuple[float, float]:
    directions: tuple[tuple[float, float], ...] = (
        (1.0, 0.0),
        (-1.0, 0.0),
        (0.0, 1.0),
        (0.0, -1.0),
    )
    dx, dy = random.choice(directions)
    return anchor_x + (dx * distance), anchor_y + (dy * distance)


def queue_infantry_mix(
    client: PipeClient,
    timeout_ms: int,
    cycle: int,
    soldiers_count: int,
    rpg_count: int,
) -> tuple[int, Any, Any]:
    sent_ok = 0
    last_code: Any = None
    last_reason: Any = None

    requests: tuple[tuple[str, dict[str, Any]], ...] = (
        ("Game.QueueSoldiersAllBarracks", {"count": max(1, int(soldiers_count))}),
        ("Game.QueueRpgTroopersAllBarracks", {"count": max(1, int(rpg_count))}),
    )
    for cmd, args in requests:
        resp = try_send_session_command(client, cmd, args, timeout_ms)
        if resp is None:
            continue
        ok = bool(resp.get("ok", False))
        last_code = resp.get("code")
        last_reason = resp.get("reason")
        print(
            f"[loop {cycle}] infantry_mix cmd={cmd} ok={ok} code={last_code} reason={last_reason}",
            flush=True,
        )
        if ok:
            sent_ok += 1

    return sent_ok, last_code, last_reason


def queue_vehicle_mix(
    client: PipeClient,
    timeout_ms: int,
    cycle: int,
    radar_count: int,
    quads_count: int,
    scorpions_count: int,
) -> tuple[int, Any, Any]:
    sent_ok = 0
    last_code: Any = None
    last_reason: Any = None

    for _ in range(max(0, int(radar_count))):
        resp = try_send_session_command(client, "Game.QueueRadarVan", {}, timeout_ms)
        if resp is None:
            continue
        ok = bool(resp.get("ok", False))
        last_code = resp.get("code")
        last_reason = resp.get("reason")
        print(
            f"[loop {cycle}] vehicle_mix cmd=Game.QueueRadarVan ok={ok} code={last_code} reason={last_reason}",
            flush=True,
        )
        if ok:
            sent_ok += 1
            continue
        fallback = try_send_session_command(
            client,
            "Game.QueueRadarVansAllWarFactories",
            {"count": 1},
            timeout_ms,
        )
        if fallback is not None and bool(fallback.get("ok", False)):
            sent_ok += 1
            last_code = fallback.get("code")
            last_reason = fallback.get("reason")

    requests: tuple[tuple[str, dict[str, Any]], ...] = (
        ("Game.QueueQuadsAllWarFactories", {"count": max(1, int(quads_count))}),
        ("Game.QueueScorpionsAllWarFactories", {"count": max(1, int(scorpions_count))}),
    )
    for cmd, args in requests:
        resp = try_send_session_command(client, cmd, args, timeout_ms)
        if resp is None:
            continue
        ok = bool(resp.get("ok", False))
        last_code = resp.get("code")
        last_reason = resp.get("reason")
        print(
            f"[loop {cycle}] vehicle_mix cmd={cmd} ok={ok} code={last_code} reason={last_reason}",
            flush=True,
        )
        if ok:
            sent_ok += 1

    return sent_ok, last_code, last_reason


def queue_building_mix(
    client: PipeClient,
    timeout_ms: int,
    cycle: int,
    stash_count: int,
    barracks_count: int,
    arms_count: int,
    black_markets_count: int,
    tunnel_networks_count: int,
    stinger_sites_count: int,
    zone_center: tuple[float, float] | None = None,
    zone_radius: float | None = None,
    buildings: list[dict[str, Any]] | None = None,
    stash_after_primary: bool = False,
) -> tuple[int, Any, Any]:
    sent_ok = 0
    last_code: Any = None
    last_reason: Any = None

    def make_args() -> dict[str, Any]:
        out: dict[str, Any] = {}
        if zone_center is not None and zone_radius is not None:
            out["zone_center"] = {"x": zone_center[0], "y": zone_center[1]}
            out["zone_radius"] = float(zone_radius)
            out["strict_zone"] = False
        return out

    primary_requests: list[tuple[str, int, dict[str, Any]]] = [
        ("Game.BuildBlackMarketSmart", max(0, int(black_markets_count)), {}),
        ("Game.BuildBarracksSmart", max(0, int(barracks_count)), {}),
        ("Game.BuildArmsDealerSmart", max(0, int(arms_count)), {}),
        ("Game.BuildBarracksSmart", max(0, int(tunnel_networks_count)), {"building_template": "GLATunnelNetwork"}),
        ("Game.BuildBarracksSmart", max(0, int(stinger_sites_count)), {"building_template": "GLAStingerSite"}),
    ]
    secondary_requests: list[tuple[str, int, dict[str, Any]]] = [
        ("Game.BuildSupplyStashSmart", max(0, int(stash_count)), {}),
    ]
    requests: list[tuple[str, int, dict[str, Any]]] = (
        primary_requests + secondary_requests if stash_after_primary else
        [("Game.BuildSupplyStashSmart", max(0, int(stash_count)), {})] + primary_requests
    )

    def resolve_build_target(cmd_name: str, args_map: dict[str, Any]) -> str:
        explicit = args_map.get("building_template")
        if isinstance(explicit, str) and explicit:
            return explicit
        fallback: dict[str, str] = {
            "Game.BuildBlackMarketSmart": "GLABlackMarket",
            "Game.BuildSupplyStashSmart": "GLASupplyStash",
            "Game.BuildBarracksSmart": "GLABarracks",
            "Game.BuildArmsDealerSmart": "GLAArmsDealer",
            "Game.BuildPalaceSmart": "GLAPalace",
            "Game.BuildCommandCenterSmart": "GLACommandCenter",
        }
        return fallback.get(cmd_name, cmd_name)

    for cmd, count, extra_args in requests:
        if stash_after_primary and cmd == "Game.BuildSupplyStashSmart" and sent_ok <= 0:
            print(
                f"[loop {cycle}] building_mix cmd={cmd} target=GLASupplyStash skip=no_primary_progress",
                flush=True,
            )
            continue
        for _ in range(count):
            if zone_center is not None and zone_radius is not None and buildings is not None:
                singleton_needles = ZONE_SINGLETON_BUILD_RULES.get(cmd)
                if singleton_needles is not None:
                    complete, under_construction = zone_building_state_counts(
                        buildings,
                        zone_center,
                        float(zone_radius),
                        singleton_needles,
                    )
                    if (complete + under_construction) > 0:
                        print(
                            f"[loop {cycle}] building_mix cmd={cmd} skip=zone_singleton_present "
                            f"complete={complete} under_construction={under_construction}",
                            flush=True,
                        )
                        break

            command_args = make_args()
            command_args.update(extra_args)
            build_target = resolve_build_target(cmd, command_args)
            resp = try_send_session_command(client, cmd, command_args, timeout_ms)
            if resp is None:
                continue
            ok = bool(resp.get("ok", False))
            last_code = resp.get("code")
            last_reason = resp.get("reason")
            print(
                f"[loop {cycle}] building_mix cmd={cmd} target={build_target} "
                f"ok={ok} code={last_code} reason={last_reason}",
                flush=True,
            )
            if ok:
                sent_ok += 1

    return sent_ok, last_code, last_reason


def is_pending(pending_until: dict[str, float], key: str, now_monotonic: float) -> bool:
    return pending_until.get(key, 0.0) > now_monotonic


def mark_pending(pending_until: dict[str, float], key: str, now_monotonic: float, cooldown_sec: float) -> None:
    pending_until[key] = now_monotonic + max(0.0, cooldown_sec)


def configure_file_logging(log_path: Path) -> TextIO:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    file_handle = log_path.open("w", encoding="utf-8", buffering=1)
    sys.stdout = TeeWriter(sys.stdout, file_handle)
    return file_handle


def main() -> int:
    parser = argparse.ArgumentParser(description="Starter GLA macro loop for the AI adapter.")
    parser.add_argument("--pipe", default="zh_ai_control", help="Named pipe name (default: zh_ai_control).")
    parser.add_argument("--tick-sec", type=float, default=5.0, help="Loop tick interval in seconds (default: 5.0).")
    parser.add_argument(
        "--retry-cooldown-sec",
        type=float,
        default=3.0,
        help="Minimum delay before retrying a failed step (default: 3.0).",
    )
    parser.add_argument(
        "--palace-market-ratio",
        type=int,
        default=PALACE_MARKET_RATIO,
        help="Required Black Markets per Palace before attempting another Palace (default: 8).",
    )
    parser.add_argument(
        "--timeout-ms",
        type=int,
        default=10000,
        help="Request timeout in milliseconds (default: 10000).",
    )
    parser.add_argument("--zone-radius", type=float, default=DEFAULT_ZONE_RADIUS, help="Zone radius for build placement hints.")
    parser.add_argument("--zone-step", type=float, default=DEFAULT_ZONE_STEP, help="Distance between sustain template zones.")
    parser.add_argument("--zone-count", type=int, default=DEFAULT_ZONE_COUNT, help="Number of generated zones.")
    parser.add_argument(
        "--camera-height-multiplier",
        type=float,
        default=3.0,
        help="Startup camera height multiplier relative to current camera height.",
    )
    parser.add_argument(
        "--pending-cooldown-sec",
        type=float,
        default=DEFAULT_PENDING_COOLDOWN_SEC,
        help="Per-command cooldown after a successful queue/build to avoid duplicate spam (default: 20.0).",
    )
    parser.add_argument(
        "--reserve-cash",
        type=int,
        default=BUDGET_RESERVE_CASH,
        help="Cash reserve target for sustain; unit mixes are skipped when below this reserve (default: 5000).",
    )
    parser.add_argument(
        "--raid-min-units",
        type=int,
        default=DEFAULT_RAID_MIN_UNITS,
        help="Minimum combat units before issuing periodic raid attack-moves (default: 20).",
    )
    parser.add_argument(
        "--raid-group-size",
        type=int,
        default=DEFAULT_RAID_GROUP_SIZE,
        help="How many combat units to send per raid attack-move (default: 20).",
    )
    parser.add_argument(
        "--raid-every-cycles",
        type=int,
        default=DEFAULT_RAID_EVERY_CYCLES,
        help="Issue a raid attack-move every N sustain cycles when ready (default: 4).",
    )
    parser.add_argument(
        "--raid-distance",
        type=float,
        default=DEFAULT_RAID_DISTANCE,
        help="Distance from anchor for raid destinations (default: 3000).",
    )
    parser.add_argument(
        "--raid-cooldown-sec",
        type=float,
        default=DEFAULT_RAID_COOLDOWN_SEC,
        help="Cooldown between raid attack-move commands (default: 20).",
    )
    parser.add_argument(
        "--guard-idle-every-cycles",
        type=int,
        default=DEFAULT_GUARD_IDLE_EVERY_CYCLES,
        help="Issue a guard-current-position sweep for idle ground combat every N sustain cycles (default: 3).",
    )
    parser.add_argument(
        "--guard-idle-cooldown-sec",
        type=float,
        default=DEFAULT_GUARD_IDLE_COOLDOWN_SEC,
        help="Cooldown between idle-guard sweeps in sustain (default: 10).",
    )
    parser.add_argument(
        "--low-money-threshold",
        type=int,
        default=DEFAULT_LOW_MONEY_THRESHOLD,
        help="When sustain money is below this, enter low-money hold mode (default: 3500).",
    )
    parser.add_argument(
        "--low-money-skip-cycles",
        type=int,
        default=DEFAULT_LOW_MONEY_SKIP_CYCLES,
        help="How many sustain cycles to skip non-raid evaluation in low-money hold mode (default: 3).",
    )
    parser.add_argument(
        "--worker-topup-min-money",
        type=int,
        default=DEFAULT_WORKER_TOPUP_MIN_MONEY,
        help="Do not queue worker top-ups in sustain below this money threshold (default: 7000).",
    )
    parser.add_argument(
        "--idle-workers-skip-topup-threshold",
        type=int,
        default=DEFAULT_IDLE_WORKERS_SKIP_TOPUP_THRESHOLD,
        help="Skip worker top-up when idle workers are at/above this threshold (default: 1).",
    )
    parser.add_argument(
        "--log-file",
        default=str(ROOT / "scripts" / "logs" / "gla_opening_starter.log"),
        help="Path to log file (default: bot-ui/scripts/logs/gla_opening_starter.log).",
    )
    args = parser.parse_args()

    log_handle = configure_file_logging(Path(args.log_file))
    print(f"Logging to {Path(args.log_file).resolve()}", flush=True)

    client = PipeClient(pipe_name=args.pipe, timeout_ms=args.timeout_ms)

    hello_resp: dict[str, Any] | None = None
    for attempt in range(1, 7):
        try:
            hello_resp = send_request(client, hello_message(client_name="bot-ui-macro", client_version="0.1.0"), args.timeout_ms)
            break
        except TimeoutError:
            print(
                f"Hello timeout attempt {attempt}/6. "
                "If Bot UI is connected at the same time, close it and retry.",
                flush=True,
            )
            time.sleep(2.0)
    if hello_resp is None or not hello_resp.get("ok", False):
        print(f"Hello failed: {hello_resp}", flush=True)
        return 1

    print("Connected to adapter. Starting starter macro loop.", flush=True)
    print(
        f"Plan phases: opening={len(PHASE_OPENING.steps)} sustain={len(PHASE_SUSTAIN.steps)} "
        f"| cc_worker_target={COMMAND_CENTER_WORKER_TARGET} | stash_worker_target={STASH_WORKER_TARGET}",
        flush=True,
    )

    refresh_objects_cache(client, args.timeout_ms)
    query_objects_cache_status(client, args.timeout_ms)
    startup_units, startup_buildings = query_objects_compact(
        client,
        args.timeout_ms,
        include_units=False,
        include_buildings=True,
    )
    startup_counts = query_zone_counts(client, args.timeout_ms)
    startup_unit_counts = query_unit_composition(client, args.timeout_ms, previous_counts={})
    command_center_ids, stash_ids = collect_producer_ids(startup_buildings)
    existing_workers = int(startup_unit_counts.get("workers", 0))
    startup_has_palace = has_palace_started(startup_buildings) or (
        isinstance(startup_counts, dict) and int(startup_counts.get("palaces", 0)) > 0
    )
    anchor_x, anchor_y = resolve_primary_anchor_xy(startup_buildings)
    zone_centers = build_zone_centers(anchor_x, anchor_y, max(128.0, float(args.zone_step)), max(1, int(args.zone_count)))
    zone_index = 0

    # Seed per-producer worker issuance from observed current worker count so restarts
    # do not assume a fresh game state.
    command_center_worker_issued, stash_worker_issued, existing_workers = recompute_producer_worker_issued(
        startup_buildings,
        existing_workers,
    )

    opening_step_index, opening_step_attempt_count = initialize_plan_progress(startup_unit_counts, startup_buildings)
    phase_index = 0
    startup_assets = summarize_assets(startup_unit_counts, startup_buildings)
    startup_now = time.monotonic()
    phase_entered_at_monotonic = startup_now
    phase_last_transition_reason = ""
    phase_index, phase_entered_at_monotonic, phase_last_transition_reason = maybe_transition_phase(
        phase_index=phase_index,
        opening_step_index=opening_step_index,
        assets=startup_assets,
        opening_core_ready=opening_building_mix_ready(startup_buildings) or (
            isinstance(startup_counts, dict)
            and int(startup_counts.get("stashes", 0)) >= 2
            and int(startup_counts.get("barracks", 0)) >= 1
            and int(startup_counts.get("arms_dealers", 0)) >= 1
        ),
        money=0,
        now_monotonic=startup_now,
        phase_entered_at_monotonic=phase_entered_at_monotonic,
        cycle=0,
    )
    sustain_step_index = 0
    last_attempt_at = 0.0
    last_worker_topup_at = 0.0
    heavy_query_backoff_until = 0.0
    radar_keepalive_inflight_until = 0.0
    radar_keepalive_queued = False
    radar_keepalive_queued_at = 0.0
    low_money_skip_until_cycle = 0
    cycle = 1
    pending_until_by_key: dict[str, float] = {}
    cached_units = startup_units
    cached_buildings = startup_buildings
    cached_unit_counts = startup_unit_counts
    cached_money = query_money(client, args.timeout_ms, previous_money=0)
    cached_idle_workers = query_idle_workers_count(client, args.timeout_ms, previous_count=0)

    camera_set_top_down(client, args.timeout_ms, height_multiplier=max(0.25, float(args.camera_height_multiplier)))
    camera_look_at(client, args.timeout_ms, zone_centers[zone_index][0], zone_centers[zone_index][1])

    print(
        "Startup inventory: "
        f"workers={existing_workers} command_centers={len(command_center_ids)} stashes={len(stash_ids)} "
        f"opening_step={opening_step_index + 1 if opening_step_index < len(PHASE_OPENING.steps) else len(PHASE_OPENING.steps)}/{len(PHASE_OPENING.steps)} "
        f"opening_progress={opening_step_attempt_count if opening_step_index < len(PHASE_OPENING.steps) else 0} "
        f"phase={PLAN_PHASES[phase_index].name} palace_seen={startup_has_palace} transition_reason={phase_last_transition_reason or '-'} "
        f"zone_anchor=({anchor_x:.1f},{anchor_y:.1f}) zone_count={len(zone_centers)}",
        flush=True,
    )

    try:
        while True:
            if OBJECTS_FULL_REFRESH_EVERY > 0 and cycle % max(1, OBJECTS_FULL_REFRESH_EVERY) == 1:
                full_units, full_buildings = query_objects_full(client, args.timeout_ms)
                if full_units or full_buildings:
                    cached_units = full_units
                    cached_buildings = full_buildings
            else:
                cache_status = query_objects_cache_status(client, args.timeout_ms) or {}
                cache_units_total = int(cache_status.get("units_total", 0)) if isinstance(cache_status.get("units_total"), int) else 0
                cache_buildings_total = (
                    int(cache_status.get("buildings_total", 0)) if isinstance(cache_status.get("buildings_total"), int) else 0
                )
                if cycle % max(1, OBJECTS_CACHE_REFRESH_EVERY_CYCLES) == 1 and time.monotonic() >= heavy_query_backoff_until:
                    refresh_objects_cache(client, args.timeout_ms)
                include_units = (
                    OBJECTS_UNITS_QUERY_EVERY_CYCLES > 0
                    and (cycle % max(1, OBJECTS_UNITS_QUERY_EVERY_CYCLES) == 1)
                )
                include_buildings = (cycle % max(1, OBJECTS_BUILDINGS_QUERY_EVERY_CYCLES) == 1)
                compact_units: list[dict[str, Any]] = []
                compact_buildings: list[dict[str, Any]] = []
                if (include_units or include_buildings) and time.monotonic() >= heavy_query_backoff_until:
                    compact_units, compact_buildings = query_objects_compact(
                        client,
                        args.timeout_ms,
                        include_units=include_units,
                        include_buildings=include_buildings,
                    )
                    if include_units and not compact_units and cache_units_total > 0:
                        heavy_query_backoff_until = time.monotonic() + HEAVY_QUERY_BACKOFF_SEC
                        print(
                            f"[query_backoff] units_map timeout suspected; backoff_sec={HEAVY_QUERY_BACKOFF_SEC}",
                            flush=True,
                        )
                    if include_buildings and not compact_buildings and cache_buildings_total > 0:
                        heavy_query_backoff_until = time.monotonic() + HEAVY_QUERY_BACKOFF_SEC
                        print(
                            f"[query_backoff] buildings_map timeout suspected; backoff_sec={HEAVY_QUERY_BACKOFF_SEC}",
                            flush=True,
                        )
                if compact_units:
                    cached_units = merge_compact_rows(cached_units, compact_units)
                if compact_buildings:
                    cached_buildings = merge_compact_rows(cached_buildings, compact_buildings)

            if cycle % max(1, UNIT_COMPOSITION_QUERY_EVERY_CYCLES) == 1:
                cached_unit_counts = query_unit_composition(
                    client,
                    args.timeout_ms,
                    previous_counts=cached_unit_counts,
                )

            units, buildings = cached_units, cached_buildings
            existing_workers = int(cached_unit_counts.get("workers", 0))
            command_center_worker_issued, stash_worker_issued, existing_workers = recompute_producer_worker_issued(
                buildings,
                existing_workers,
            )
            assets = summarize_assets(cached_unit_counts, buildings)
            money = query_money(client, args.timeout_ms, previous_money=cached_money)
            cached_money = money
            if cycle % max(1, IDLE_WORKERS_QUERY_EVERY_CYCLES) == 1:
                cached_idle_workers = query_idle_workers_count(
                    client,
                    args.timeout_ms,
                    previous_count=cached_idle_workers,
                )
            now = time.monotonic()

            phase_index, phase_entered_at_monotonic, phase_last_transition_reason = maybe_transition_phase(
                phase_index=phase_index,
                opening_step_index=opening_step_index,
                assets=assets,
                opening_core_ready=opening_building_mix_ready(buildings),
                money=money,
                now_monotonic=now,
                phase_entered_at_monotonic=phase_entered_at_monotonic,
                cycle=cycle,
            )

            if now - last_attempt_at < args.retry_cooldown_sec:
                time.sleep(args.tick_sec)
                continue

            radar_keepalive_key = "Game.QueueRadarVan.Keepalive"
            if assets.get("radar_vans", 0) > 0:
                radar_keepalive_inflight_until = 0.0
                radar_keepalive_queued = False
                radar_keepalive_queued_at = 0.0
            if radar_keepalive_queued and (now - radar_keepalive_queued_at) >= RADAR_KEEPALIVE_WATCHDOG_SEC:
                radar_keepalive_queued = False
                radar_keepalive_queued_at = 0.0
            radar_keepalive_inflight = radar_keepalive_inflight_until > now
            radar_ready_or_pending = (
                assets.get("radar_vans", 0) > 0
                or is_pending(pending_until_by_key, radar_keepalive_key, now)
                or radar_keepalive_inflight
                or radar_keepalive_queued
            )
            if assets.get("arms", 0) > 0 and not radar_ready_or_pending:
                radar_resp = try_send_session_command(client, "Game.QueueRadarVan", {}, args.timeout_ms)
                radar_ok = False
                radar_code: Any = None
                radar_reason: Any = None
                if radar_resp is not None:
                    radar_ok = bool(radar_resp.get("ok", False))
                    radar_code = radar_resp.get("code")
                    radar_reason = radar_resp.get("reason")
                if not radar_ok:
                    fallback_resp = try_send_session_command(
                        client,
                        "Game.QueueRadarVansAllWarFactories",
                        {"count": 1},
                        args.timeout_ms,
                    )
                    if fallback_resp is not None:
                        radar_ok = bool(fallback_resp.get("ok", False))
                        radar_code = fallback_resp.get("code")
                        radar_reason = fallback_resp.get("reason")
                print(
                    f"[loop {cycle}] radar_keepalive arms={assets.get('arms', 0)} radar_vans={assets.get('radar_vans', 0)} "
                    f"ok={radar_ok} code={radar_code} reason={radar_reason}",
                    flush=True,
                )
                if radar_ok:
                    mark_pending(
                        pending_until_by_key,
                        radar_keepalive_key,
                        now,
                        max(12.0, float(args.pending_cooldown_sec)),
                    )
                    radar_keepalive_inflight_until = now + RADAR_KEEPALIVE_INFLIGHT_SEC
                    radar_keepalive_queued = True
                    radar_keepalive_queued_at = now
                last_attempt_at = now
                cycle += 1
                time.sleep(args.tick_sec)
                continue

            # After we are partway into opening (or in sustain), keep worker producers filled.
            if phase_index > 0 or opening_step_index >= 2:
                allow_cc_topup = True
                allow_stash_topup = True
                idle_skip_threshold = max(0, int(args.idle_workers_skip_topup_threshold))
                if idle_skip_threshold > 0 and cached_idle_workers >= idle_skip_threshold:
                    allow_cc_topup = False
                    print(
                        f"[loop {cycle}] worker_topup_skip cc_idle_workers={cached_idle_workers} "
                        f"threshold={idle_skip_threshold}",
                        flush=True,
                    )
                if phase_index > 0:
                    worker_money_gate = max(0, int(args.worker_topup_min_money))
                    if money < worker_money_gate:
                        allow_cc_topup = False
                        print(
                            f"[loop {cycle}] worker_topup_skip cc_low_cash money={money} required={worker_money_gate}",
                            flush=True,
                        )
                if (allow_cc_topup or allow_stash_topup) and (now - last_worker_topup_at) >= DEFAULT_WORKER_TOPUP_COOLDOWN_SEC:
                    worker_topup_done = False
                    prefer_stash_topup = phase_index > 0
                    worker_pending_cooldown = 1.0
                    if prefer_stash_topup and not worker_topup_done and allow_stash_topup:
                        stash_added = top_up_workers_for_producers(
                            client=client,
                            timeout_ms=args.timeout_ms,
                            cycle=cycle,
                            producer_label="stash",
                            producer_target=STASH_WORKER_TARGET,
                            producer_worker_issued=stash_worker_issued,
                            pending_until_by_key=pending_until_by_key,
                            now=now,
                            pending_cooldown_sec=worker_pending_cooldown,
                        )
                        if stash_added > 0:
                            worker_topup_done = True
                    if not worker_topup_done and allow_cc_topup:
                        cc_added = top_up_workers_for_producers(
                            client=client,
                            timeout_ms=args.timeout_ms,
                            cycle=cycle,
                            producer_label="command_center",
                            producer_target=COMMAND_CENTER_WORKER_TARGET,
                            producer_worker_issued=command_center_worker_issued,
                            pending_until_by_key=pending_until_by_key,
                            now=now,
                            pending_cooldown_sec=worker_pending_cooldown,
                        )
                        if cc_added > 0:
                            worker_topup_done = True
                    if not worker_topup_done and allow_stash_topup:
                        stash_added = top_up_workers_for_producers(
                            client=client,
                            timeout_ms=args.timeout_ms,
                            cycle=cycle,
                            producer_label="stash",
                            producer_target=STASH_WORKER_TARGET,
                            producer_worker_issued=stash_worker_issued,
                            pending_until_by_key=pending_until_by_key,
                            now=now,
                            pending_cooldown_sec=worker_pending_cooldown,
                        )
                        if stash_added > 0:
                            worker_topup_done = True
                    if worker_topup_done:
                        last_worker_topup_at = now

            current_phase = PLAN_PHASES[phase_index]
            if current_phase.name == PHASE_OPENING_NAME:
                opening_core_ready_now = opening_building_mix_ready(buildings)
                if opening_core_ready_now:
                    if is_pending(pending_until_by_key, "Game.BuildPalaceSmart", now):
                        cycle += 1
                        time.sleep(args.tick_sec)
                        continue
                    palace_added, palace_code, palace_reason = try_burst_step(
                        client,
                        Step("Build palace priority", "Game.BuildPalaceSmart", {}),
                        args.timeout_ms,
                    )
                    if palace_added > 0:
                        mark_pending(
                            pending_until_by_key,
                            "Game.BuildPalaceSmart",
                            now,
                            float(args.pending_cooldown_sec),
                        )
                        assets["palaces"] = max(1, assets.get("palaces", 0) + palace_added)
                        phase_index, phase_entered_at_monotonic, phase_last_transition_reason = maybe_transition_phase(
                            phase_index=phase_index,
                            opening_step_index=opening_step_index,
                            assets=assets,
                            opening_core_ready=opening_building_mix_ready(buildings),
                            money=money,
                            now_monotonic=now,
                            phase_entered_at_monotonic=phase_entered_at_monotonic,
                            cycle=cycle,
                        )
                        print(f"[phase] palace_priority added={palace_added} code={palace_code} reason={palace_reason}", flush=True)
                        last_attempt_at = now
                        cycle += 1
                        time.sleep(args.tick_sec)
                        continue

                current = PHASE_OPENING.steps[opening_step_index]
                if current.cmd == "Script.BuildingMix" and cached_idle_workers <= 0:
                    mark_pending(
                        pending_until_by_key,
                        current.cmd,
                        now,
                        max(8.0, float(args.pending_cooldown_sec)),
                    )
                    print(
                        f"[loop {cycle}] phase=open step={opening_step_index + 1}/{len(PHASE_OPENING.steps)} "
                        f"name='{current.name}' skip=no_idle_workers idle_workers={cached_idle_workers}",
                        flush=True,
                    )
                    cycle += 1
                    time.sleep(args.tick_sec)
                    continue
                if is_pending(pending_until_by_key, current.cmd, now):
                    cycle += 1
                    time.sleep(args.tick_sec)
                    continue
                sent_ok = 0
                last_code: Any = None
                last_reason: Any = None
                remaining_attempts = max(0, current.repeat - opening_step_attempt_count)
                for _ in range(remaining_attempts):
                    if current.cmd == "Script.BuildingMix":
                        mix_added, mix_code, mix_reason = queue_building_mix(
                            client,
                            args.timeout_ms,
                            cycle,
                            stash_count=int(current.args.get("stash", 1)),
                            barracks_count=int(current.args.get("barracks", 1)),
                            arms_count=int(current.args.get("arms", 1)),
                            black_markets_count=int(current.args.get("black_markets", 0)),
                            tunnel_networks_count=int(current.args.get("tunnel_networks", 0)),
                            stinger_sites_count=int(current.args.get("stinger_sites", 0)),
                        )
                        last_code = mix_code
                        last_reason = mix_reason
                        if mix_added <= 0:
                            break
                        sent_ok += 1
                        opening_step_attempt_count += 1
                        continue
                    if current.cmd == "Script.QueueInfantryMix":
                        mix_added, mix_code, mix_reason = queue_infantry_mix(
                            client,
                            args.timeout_ms,
                            cycle,
                            soldiers_count=int(current.args.get("soldiers", 2)),
                            rpg_count=int(current.args.get("rpg", 2)),
                        )
                        last_code = mix_code
                        last_reason = mix_reason
                        if mix_added <= 0:
                            break
                        sent_ok += 1
                        opening_step_attempt_count += 1
                        continue
                    if current.cmd == "Script.QueueVehicleMix":
                        mix_added, mix_code, mix_reason = queue_vehicle_mix(
                            client,
                            args.timeout_ms,
                            cycle,
                            radar_count=int(current.args.get("radar", 1)),
                            quads_count=int(current.args.get("quads", 3)),
                            scorpions_count=int(current.args.get("scorpions", 3)),
                        )
                        last_code = mix_code
                        last_reason = mix_reason
                        if mix_added <= 0:
                            break
                        sent_ok += 1
                        opening_step_attempt_count += 1
                        continue

                    resp = try_send_session_command(client, current.cmd, current.args, args.timeout_ms)
                    if resp is None:
                        break
                    ok = bool(resp.get("ok", False))
                    last_code = resp.get("code")
                    last_reason = resp.get("reason")
                    if not ok:
                        break
                    sent_ok += 1
                    opening_step_attempt_count += 1

                print(
                    f"[loop {cycle}] phase=open step={opening_step_index + 1}/{len(PHASE_OPENING.steps)} "
                    f"name='{current.name}' added={sent_ok} "
                    f"progress={opening_step_attempt_count}/{current.repeat} "
                    f"last_code={last_code} last_reason={last_reason}",
                    flush=True,
                )
                if sent_ok > 0:
                    mark_pending(
                        pending_until_by_key,
                        current.cmd,
                        now,
                        float(args.pending_cooldown_sec),
                    )
                elif current.cmd == "Script.BuildingMix" and last_reason == "idle_worker_not_found":
                    mark_pending(
                        pending_until_by_key,
                        current.cmd,
                        now,
                        max(10.0, float(args.pending_cooldown_sec)),
                    )
                last_attempt_at = now

                if current.cmd == "Script.BuildingMix":
                    refreshed_units, refreshed_buildings = query_objects_compact(
                        client,
                        args.timeout_ms,
                        include_units=False,
                        include_buildings=True,
                    )
                    if refreshed_units:
                        cached_units = merge_compact_rows(cached_units, refreshed_units)
                    if refreshed_buildings:
                        cached_buildings = merge_compact_rows(cached_buildings, refreshed_buildings)
                    opening_ready = opening_building_mix_ready(cached_buildings)
                    print(
                        f"[opening_status] building_mix_ready={opening_ready} "
                        f"stashes={count_complete_matching_templates(cached_buildings, ('supplystash', 'supplycenter'))} "
                        f"barracks={count_complete_matching_templates(cached_buildings, ('barracks',))} "
                        f"arms={count_complete_matching_templates(cached_buildings, ('armsdealer', 'warfactory'))}",
                        flush=True,
                    )
                    if opening_ready:
                        opening_step_index += 1
                        opening_step_attempt_count = 0
                    else:
                        opening_step_attempt_count = 0
                elif opening_step_attempt_count >= current.repeat:
                    opening_step_index += 1
                    opening_step_attempt_count = 0

                # Recovery: if opening mixes are blocked by missing production buildings,
                # return to opening building mix instead of looping forever on queue steps.
                if current.cmd == "Script.QueueInfantryMix" and last_reason in ("no_barracks_found", "queue_failed"):
                    opening_step_index = 1
                    opening_step_attempt_count = 0
                    pending_until_by_key.pop(current.cmd, None)
                    pending_until_by_key.pop("Script.BuildingMix", None)
                    print("[recovery] opening_infantry_blocked -> back_to_building_mix", flush=True)
                if current.cmd == "Script.QueueVehicleMix" and last_reason in ("no_war_factory_found", "queue_failed"):
                    opening_step_index = 1
                    opening_step_attempt_count = 0
                    pending_until_by_key.pop(current.cmd, None)
                    pending_until_by_key.pop("Script.BuildingMix", None)
                    print("[recovery] opening_vehicle_blocked -> back_to_building_mix", flush=True)
                phase_index, phase_entered_at_monotonic, phase_last_transition_reason = maybe_transition_phase(
                    phase_index=phase_index,
                    opening_step_index=opening_step_index,
                    assets=assets,
                    opening_core_ready=opening_building_mix_ready(cached_buildings),
                    money=money,
                    now_monotonic=now,
                    phase_entered_at_monotonic=phase_entered_at_monotonic,
                    cycle=cycle,
                )

                cycle += 1
                time.sleep(args.tick_sec)
                continue

            # Phase 2 (sustain): black-market-heavy economy with periodic extra palaces.
            raid_every = max(1, int(args.raid_every_cycles))
            raid_min_units = max(1, int(args.raid_min_units))
            raid_group_size = max(1, int(args.raid_group_size))
            raid_distance = max(256.0, float(args.raid_distance))
            raid_key = "Game.AttackMove.RaidSmart"
            guard_idle_every = max(1, int(args.guard_idle_every_cycles))
            guard_idle_key = "Game.GuardAllIdleGroundCombat"
            low_money_threshold = max(0, int(args.low_money_threshold))
            low_money_skip_cycles = max(1, int(args.low_money_skip_cycles))
            if money < low_money_threshold:
                low_money_skip_until_cycle = max(low_money_skip_until_cycle, cycle + low_money_skip_cycles)
            in_low_money_hold = (money < low_money_threshold) and (cycle < low_money_skip_until_cycle)

            raid_cycle_gate = (cycle % raid_every == 0) and not in_low_money_hold
            raid_cooldown_gate = not is_pending(pending_until_by_key, raid_key, now)
            if raid_cycle_gate and raid_cooldown_gate:
                raid_resp = try_send_session_command(
                    client,
                    "Game.AttackMove.RaidSmart",
                    {
                        "min_units": raid_min_units,
                        "group_size": raid_group_size,
                        "distance": raid_distance,
                    },
                    args.timeout_ms,
                )
                if raid_resp is not None:
                    raid_ok = bool(raid_resp.get("ok", False))
                    print(
                        f"[loop {cycle}] raid_smart ok={raid_ok} "
                        f"code={raid_resp.get('code')} reason={raid_resp.get('reason')}",
                        flush=True,
                    )
                    if raid_ok:
                        mark_pending(
                            pending_until_by_key,
                            raid_key,
                            now,
                            max(1.0, float(args.raid_cooldown_sec)),
                        )
            else:
                if in_low_money_hold:
                    raid_reason = f"low_money_hold money={money} threshold={low_money_threshold}"
                elif not raid_cycle_gate:
                    raid_reason = f"cycle_gate cycle={cycle} every={raid_every}"
                else:
                    pending_until = pending_until_by_key.get(raid_key, 0.0)
                    remaining = max(0.0, pending_until - now)
                    raid_reason = f"cooldown_gate remaining_sec={remaining:.1f}"
                print(f"[loop {cycle}] raid_skip {raid_reason}", flush=True)

            guard_idle_cycle_gate = (cycle % guard_idle_every == 0)
            guard_idle_cooldown_gate = not is_pending(pending_until_by_key, guard_idle_key, now)
            if guard_idle_cycle_gate and guard_idle_cooldown_gate:
                guard_idle_resp = try_send_session_command(
                    client,
                    guard_idle_key,
                    {},
                    args.timeout_ms,
                )
                if guard_idle_resp is not None:
                    guard_idle_ok = bool(guard_idle_resp.get("ok", False))
                    print(
                        f"[loop {cycle}] guard_idle_ground ok={guard_idle_ok} "
                        f"code={guard_idle_resp.get('code')} reason={guard_idle_resp.get('reason')}",
                        flush=True,
                    )
                    if guard_idle_ok:
                        mark_pending(
                            pending_until_by_key,
                            guard_idle_key,
                            now,
                            max(1.0, float(args.guard_idle_cooldown_sec)),
                        )
            else:
                if not guard_idle_cycle_gate:
                    guard_idle_reason = f"cycle_gate cycle={cycle} every={guard_idle_every}"
                else:
                    pending_until = pending_until_by_key.get(guard_idle_key, 0.0)
                    remaining = max(0.0, pending_until - now)
                    guard_idle_reason = f"cooldown_gate remaining_sec={remaining:.1f}"
                print(f"[loop {cycle}] guard_idle_skip {guard_idle_reason}", flush=True)

            black_markets = assets["black_markets"]
            palaces = assets["palaces"]
            completed_palaces = count_complete_matching_templates(buildings, ("palace",))
            desired_markets_for_next_palace = max(4, (palaces + 1) * max(1, int(args.palace_market_ratio)))

            should_try_market = (
                completed_palaces > 0
                and
                black_markets < desired_markets_for_next_palace
                and money >= SUSTAIN_BLACK_MARKET_MIN_MONEY
                and (cycle % SUSTAIN_MARKET_ATTEMPT_EVERY == 0)
            )
            if should_try_market:
                if not is_pending(pending_until_by_key, "Game.BuildBlackMarketSmart", now):
                    market_added, market_code, market_reason = try_burst_step(
                        client,
                        with_zone_args(
                            Step("Build black market sustain", "Game.BuildBlackMarketSmart", {}),
                            zone_centers[zone_index],
                            float(args.zone_radius),
                        ),
                        args.timeout_ms,
                    )
                    print(
                        f"[loop {cycle}] phase=sustain econ=black_market "
                        f"bm={black_markets} palaces={palaces} completed_palaces={completed_palaces} "
                        f"target_bm={desired_markets_for_next_palace} "
                        f"added={market_added} last_code={market_code} last_reason={market_reason}",
                        flush=True,
                    )
                    if market_added > 0:
                        mark_pending(
                            pending_until_by_key,
                            "Game.BuildBlackMarketSmart",
                            now,
                            float(args.pending_cooldown_sec),
                        )
            elif (cycle % SUSTAIN_MARKET_ATTEMPT_EVERY == 0) and completed_palaces <= 0:
                print(
                    f"[loop {cycle}] phase=sustain econ=black_market skip=no_completed_palace "
                    f"palaces={palaces} completed_palaces={completed_palaces}",
                    flush=True,
                )
            elif (cycle % SUSTAIN_MARKET_ATTEMPT_EVERY == 0) and money < SUSTAIN_BLACK_MARKET_MIN_MONEY:
                print(
                    f"[loop {cycle}] phase=sustain econ=black_market skip=low_cash "
                    f"money={money} required={SUSTAIN_BLACK_MARKET_MIN_MONEY}",
                    flush=True,
                )

            should_try_palace = (
                money >= MIN_MONEY_FOR_PALACE_ATTEMPT
                and black_markets >= desired_markets_for_next_palace
                and (cycle % SUSTAIN_PALACE_ATTEMPT_EVERY == 0)
            )
            if should_try_palace:
                palace_complete, palace_uc = zone_building_state_counts(
                    buildings,
                    zone_centers[zone_index],
                    float(args.zone_radius),
                    ("palace",),
                )
                if (palace_complete + palace_uc) > 0:
                    print(
                        f"[loop {cycle}] phase=sustain econ=palace skip=zone_has_palace "
                        f"zone_index={zone_index} complete={palace_complete} under_construction={palace_uc}",
                        flush=True,
                    )
                elif not is_pending(pending_until_by_key, "Game.BuildPalaceSmart", now):
                    palace_added, palace_code, palace_reason = try_burst_step(
                        client,
                        with_zone_args(
                            Step("Build palace sustain", "Game.BuildPalaceSmart", {}),
                            zone_centers[zone_index],
                            float(args.zone_radius),
                        ),
                        args.timeout_ms,
                    )
                    print(
                        f"[loop {cycle}] phase=sustain econ=palace "
                        f"money={money} bm={black_markets} palaces={palaces} "
                        f"added={palace_added} last_code={palace_code} last_reason={palace_reason}",
                        flush=True,
                    )
                    if palace_added > 0:
                        mark_pending(
                            pending_until_by_key,
                            "Game.BuildPalaceSmart",
                            now,
                            float(args.pending_cooldown_sec),
                        )

            sustain_step = with_zone_args(PHASE_SUSTAIN.steps[sustain_step_index], zone_centers[zone_index], float(args.zone_radius))
            singleton_needles = ZONE_SINGLETON_BUILD_RULES.get(sustain_step.cmd)
            if singleton_needles is not None:
                singleton_complete, singleton_uc = zone_building_state_counts(
                    buildings,
                    zone_centers[zone_index],
                    float(args.zone_radius),
                    singleton_needles,
                )
                if (singleton_complete + singleton_uc) > 0:
                    print(
                        f"[loop {cycle}] phase=sustain step={sustain_step_index + 1}/{len(PHASE_SUSTAIN.steps)} "
                        f"name='{sustain_step.name}' skip=zone_singleton_present "
                        f"zone_index={zone_index} complete={singleton_complete} under_construction={singleton_uc}",
                        flush=True,
                    )
                    sustain_step_index = (sustain_step_index + 1) % len(PHASE_SUSTAIN.steps)
                    cycle += 1
                    time.sleep(args.tick_sec)
                    continue
            if is_pending(pending_until_by_key, sustain_step.cmd, now):
                sustain_step_index = (sustain_step_index + 1) % len(PHASE_SUSTAIN.steps)
                cycle += 1
                time.sleep(args.tick_sec)
                continue
            if sustain_step.cmd == "Script.BuildingMix" and cached_idle_workers <= 0:
                print(
                    f"[loop {cycle}] phase=sustain step={sustain_step_index + 1}/{len(PHASE_SUSTAIN.steps)} "
                    f"name='{sustain_step.name}' skip=no_idle_workers idle_workers={cached_idle_workers}",
                    flush=True,
                )
                sustain_step_index = (sustain_step_index + 1) % len(PHASE_SUSTAIN.steps)
                cycle += 1
                time.sleep(args.tick_sec)
                continue

            # Economy guardrail: when cash is tight, let building mix recover economy first.
            reserve_cash = max(0, int(args.reserve_cash))
            if sustain_step.cmd == "Script.QueueInfantryMix":
                if in_low_money_hold:
                    print(
                        f"[loop {cycle}] phase=sustain step={sustain_step_index + 1}/{len(PHASE_SUSTAIN.steps)} "
                        f"name='{sustain_step.name}' skip=low_money_hold money={money} "
                        f"threshold={low_money_threshold} resume_cycle={low_money_skip_until_cycle}",
                        flush=True,
                    )
                    sustain_step_index = (sustain_step_index + 1) % len(PHASE_SUSTAIN.steps)
                    cycle += 1
                    time.sleep(args.tick_sec)
                    continue
                required = reserve_cash + BUDGET_INFANTRY_MIX_MIN
                if money < required:
                    print(
                        f"[loop {cycle}] phase=sustain step={sustain_step_index + 1}/{len(PHASE_SUSTAIN.steps)} "
                        f"name='{sustain_step.name}' skip=low_cash money={money} required={required}",
                        flush=True,
                    )
                    sustain_step_index = (sustain_step_index + 1) % len(PHASE_SUSTAIN.steps)
                    cycle += 1
                    time.sleep(args.tick_sec)
                    continue
            elif sustain_step.cmd == "Script.QueueVehicleMix":
                if in_low_money_hold:
                    print(
                        f"[loop {cycle}] phase=sustain step={sustain_step_index + 1}/{len(PHASE_SUSTAIN.steps)} "
                        f"name='{sustain_step.name}' skip=low_money_hold money={money} "
                        f"threshold={low_money_threshold} resume_cycle={low_money_skip_until_cycle}",
                        flush=True,
                    )
                    sustain_step_index = (sustain_step_index + 1) % len(PHASE_SUSTAIN.steps)
                    cycle += 1
                    time.sleep(args.tick_sec)
                    continue
                required = reserve_cash + BUDGET_VEHICLE_MIX_MIN
                if money < required:
                    print(
                        f"[loop {cycle}] phase=sustain step={sustain_step_index + 1}/{len(PHASE_SUSTAIN.steps)} "
                        f"name='{sustain_step.name}' skip=low_cash money={money} required={required}",
                        flush=True,
                    )
                    sustain_step_index = (sustain_step_index + 1) % len(PHASE_SUSTAIN.steps)
                    cycle += 1
                    time.sleep(args.tick_sec)
                    continue

            if sustain_step.cmd == "Script.QueueInfantryMix":
                sustain_added, sustain_code, sustain_reason = queue_infantry_mix(
                    client,
                    args.timeout_ms,
                    cycle,
                    soldiers_count=int(sustain_step.args.get("soldiers", 2)),
                    rpg_count=int(sustain_step.args.get("rpg", 2)),
                )
            elif sustain_step.cmd == "Script.QueueVehicleMix":
                sustain_added, sustain_code, sustain_reason = queue_vehicle_mix(
                    client,
                    args.timeout_ms,
                    cycle,
                    radar_count=int(sustain_step.args.get("radar", 1)),
                    quads_count=int(sustain_step.args.get("quads", 3)),
                    scorpions_count=int(sustain_step.args.get("scorpions", 3)),
                )
            elif sustain_step.cmd == "Script.BuildingMix":
                requested_stash_count = int(sustain_step.args.get("stash", 1))
                stash_count_for_cycle = requested_stash_count
                barracks_count_for_cycle = int(sustain_step.args.get("barracks", 1))
                arms_count_for_cycle = int(sustain_step.args.get("arms", 1))
                black_markets_count_for_cycle = int(sustain_step.args.get("black_markets", 0))
                tunnel_networks_count_for_cycle = int(sustain_step.args.get("tunnel_networks", 0))
                stinger_sites_count_for_cycle = int(sustain_step.args.get("stinger_sites", 0))
                if in_low_money_hold:
                    barracks_count_for_cycle = 0
                    arms_count_for_cycle = 0
                if money < SUSTAIN_BLACK_MARKET_MIN_MONEY:
                    black_markets_count_for_cycle = 0
                if requested_stash_count > 0:
                    if money < SUSTAIN_STASH_MIN_MONEY:
                        stash_count_for_cycle = 0
                        print(
                            f"[loop {cycle}] phase=sustain step={sustain_step_index + 1}/{len(PHASE_SUSTAIN.steps)} "
                            f"name='{sustain_step.name}' stash_skip=low_cash money={money} required={SUSTAIN_STASH_MIN_MONEY}",
                            flush=True,
                        )
                    elif cycle % max(1, SUSTAIN_STASH_EVERY_CYCLES) != 0:
                        stash_count_for_cycle = 0
                        print(
                            f"[loop {cycle}] phase=sustain step={sustain_step_index + 1}/{len(PHASE_SUSTAIN.steps)} "
                            f"name='{sustain_step.name}' stash_skip=cadence every={SUSTAIN_STASH_EVERY_CYCLES}",
                            flush=True,
                        )
                sustain_added, sustain_code, sustain_reason = queue_building_mix(
                    client,
                    args.timeout_ms,
                    cycle,
                    stash_count=stash_count_for_cycle,
                    barracks_count=barracks_count_for_cycle,
                    arms_count=arms_count_for_cycle,
                    black_markets_count=black_markets_count_for_cycle,
                    tunnel_networks_count=tunnel_networks_count_for_cycle,
                    stinger_sites_count=stinger_sites_count_for_cycle,
                    zone_center=zone_centers[zone_index],
                    zone_radius=float(args.zone_radius),
                    buildings=buildings,
                    stash_after_primary=True,
                )
            else:
                sustain_added, sustain_code, sustain_reason = try_burst_step(client, sustain_step, args.timeout_ms)
            print(
                f"[loop {cycle}] phase=sustain step={sustain_step_index + 1}/{len(PHASE_SUSTAIN.steps)} "
                f"name='{sustain_step.name}' added={sustain_added} "
                f"money={money} bm={black_markets} palaces={palaces} "
                f"last_code={sustain_code} last_reason={sustain_reason}",
                flush=True,
            )
            previous_step_index = sustain_step_index
            if sustain_added > 0:
                mark_pending(
                    pending_until_by_key,
                    sustain_step.cmd,
                    now,
                    float(args.pending_cooldown_sec),
                )
            sustain_step_index = (sustain_step_index + 1) % len(PHASE_SUSTAIN.steps)
            if previous_step_index == len(PHASE_SUSTAIN.steps) - 1:
                zone_index = (zone_index + 1) % len(zone_centers)
                next_zone = zone_centers[zone_index]
                print(f"[zone] template advance -> zone_index={zone_index} center=({next_zone[0]:.1f},{next_zone[1]:.1f})", flush=True)
            cycle += 1
            last_attempt_at = now

            time.sleep(args.tick_sec)
    except KeyboardInterrupt:
        print("Stopped by user.", flush=True)
        log_handle.close()
        return 0
    except Exception as exc:  # noqa: BLE001
        print(f"Fatal error: {exc}", flush=True)
        log_handle.close()
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
