from __future__ import annotations

import argparse
import random
import re
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
    PHASE_EXPANSION,
    PHASE_EXPANSION_NAME,
    PHASE_OPENING,
    PHASE_OPENING_NAME,
    PHASE_SUSTAIN,
    PHASE_SUSTAIN_NAME,
    PHASE_WARMONGER,
    PHASE_WARMONGER_NAME,
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
        try:
            self._secondary.write(data)
        except ValueError:
            pass
        return len(data)

    def flush(self) -> None:
        self._primary.flush()
        try:
            self._secondary.flush()
        except ValueError:
            pass


STASH_WORKER_TARGET = 9
COMMAND_CENTER_WORKER_TARGET = 2
PALACE_MARKET_RATIO = 8
MIN_MONEY_FOR_PALACE_ATTEMPT = 7000
DEFAULT_ZONE_RADIUS = 420.0
DEFAULT_ZONE_STEP = 900.0
DEFAULT_ZONE_COUNT = 64
DEFAULT_GRID_COLS = 32
DEFAULT_GRID_ROWS = 32
DEFAULT_GRID_QUERY_EVERY_CYCLES = 6
DEFAULT_GRID_EXPANSION_MAX_DISTANCE = 1800.0
GRID_QUERY_BACKOFF_SEC = 45.0
DEFAULT_TECH_CHECK_EVERY_CYCLES = 2
DEFAULT_EXPANSION_COMMAND_CENTER_EVERY = 5
DEFAULT_COMMAND_CENTER_MIN_MONEY = 6000
DEFAULT_EXPANSION_ARMS_EVERY = 4
DEFAULT_ARMS_DEALER_MIN_MONEY = 5000
DEFAULT_EXPANSION_INFANTRY_EVERY = 3
DEFAULT_EXPANSION_VEHICLE_EVERY = 5
DEFAULT_EXPANSION_UNIT_MIN_MONEY = 7000
DEFAULT_MARKET_BOOTSTRAP_TARGET = 2
DEFAULT_MARKET_BOOTSTRAP_MAX_MONEY = 12000
DEFAULT_MARKET_BOOTSTRAP_MIN_INCOME_PER_SEC = 120.0
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
OBJECTS_UNITS_QUERY_EVERY_CYCLES = 3
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
DEFAULT_DEFENSE_WORKER_MIN_IDLE = 3
DEFAULT_CC_WORKER_PRESSURE_CYCLES = 3
DEFAULT_ECO_BUILD_DELAY_AFTER_DEFENSE_SEC = 10.0
DEFAULT_PALACE_RECOVERY_GRACE_SEC = 30.0
DEFAULT_STASH_SUPPLY_CLAIM_RADIUS = 260.0
DEFAULT_BUILD_TICK_IDLE_RESERVE = 2
DEFAULT_EXPANSION_ECO_SHARE = 0.4
DEFAULT_WORKER_TRICKLE_CADENCE_SEC = 3.0
DEFAULT_WORKER_TRICKLE_MIN_MONEY = 8000
DEFAULT_WORKER_TRICKLE_MIN_INCOME_PER_SEC = 50.0
DEFAULT_CAPTURABLE_BUILDINGS_QUERY_EVERY_CYCLES = 6
DEFAULT_CAPTURE_ATTEMPT_EVERY_CYCLES = 3
RADAR_KEEPALIVE_INFLIGHT_SEC = 90.0
RADAR_KEEPALIVE_WATCHDOG_SEC = 240.0
DEFAULT_LOW_MONEY_THRESHOLD = 3500
DEFAULT_LOW_MONEY_SKIP_CYCLES = 3
DEBUG_FORCED_CASH = 999_999
USE_ADAPTER_WORKER_RULE = True
USE_ADAPTER_RADAR_VAN_RULE = True
ENABLE_DEBUG_FORCED_CASH = False
ENABLE_DEBUG_DESHROUD = False
CAPTURE_BUILDING_UPGRADE = "Upgrade_InfantryCaptureBuilding"
SCIENCE_PURCHASE_PLAN: tuple[str, ...] = (
    "SCIENCE_ScudLauncher",
    "SCIENCE_CashBounty1",
    "SCIENCE_CashBounty2",
    "SCIENCE_CashBounty3",
)
UPGRADE_QUEUE_PLAN: tuple[tuple[str, str], ...] = (
    ("black_market", "Upgrade_GLAWorkerShoes"),
    ("black_market", "Upgrade_GLAAPBullets"),
    ("palace", "Upgrade_GLAFortifiedStructure"),
    ("palace", "Upgrade_GLAAnthraxBeta"),
    ("palace", "Upgrade_GLAToxinShells"),
    ("palace", "Chem_Upgrade_GLAAnthraxGamma"),
    ("palace", "Upgrade_GLAArmTheMob"),
    ("black_market", "Upgrade_GLAAPRockets"),
    ("black_market", "Upgrade_GLABuggyAmmo"),
    ("black_market", "Upgrade_GLAJunkRepair"),
    ("black_market", "Upgrade_GLARadarVanScan"),
    ("black_market", "Upgrade_GLACamoNetting"),
    ("palace", "GC_Slth_Upgrade_GLAQuadCannonSnipe"),
    ("palace", "Demo_Upgrade_GLADemoTrapHighExplosiveBomb"),
)
ZONE_SINGLETON_BUILD_RULES: dict[str, tuple[str, ...]] = {
    "Game.BuildCommandCenterSmart": ("commandcenter",),
    "Game.BuildSupplyStashSmart": ("supplystash", "supplycenter"),
    "Game.BuildPalaceSmart": ("palace",),
}


def send_request(client: PipeClient, payload: dict[str, Any], timeout_ms: int) -> dict[str, Any]:
    resp = client.request_once(payload, timeout_ms=timeout_ms)
    if isinstance(resp, dict):
        resp.setdefault("request_id", str(payload.get("request_id", "")))
    return resp


def send_session_command(
    client: PipeClient,
    cmd: str,
    args: dict[str, Any],
    timeout_ms: int,
) -> dict[str, Any]:
    payload = session_command_message(cmd, args)
    return send_request(client, payload, timeout_ms)


def request_id_suffix(resp: dict[str, Any] | None) -> str:
    if not isinstance(resp, dict):
        return ""
    request_id = resp.get("request_id")
    return f" request_id={request_id}" if isinstance(request_id, str) and request_id else ""


def extract_request_id_from_error(exc: Exception) -> str:
    match = re.search(r"request_id=([0-9a-fA-F]+)", str(exc))
    if match is None:
        return ""
    return match.group(1)


def try_send_session_command(
    client: PipeClient,
    cmd: str,
    args: dict[str, Any],
    timeout_ms: int,
) -> dict[str, Any] | None:
    try:
        return send_session_command(client, cmd, args, timeout_ms)
    except TimeoutError as exc:
        path_note = f" path={args.get('path')}" if isinstance(args, dict) and isinstance(args.get("path"), str) else ""
        request_id = extract_request_id_from_error(exc)
        req_note = f" request_id={request_id}" if request_id else ""
        print(f"timeout waiting for {cmd} reply{path_note}{req_note}; will retry", flush=True)
        return None
    except Exception as exc:  # noqa: BLE001
        request_id = extract_request_id_from_error(exc)
        req_note = f" request_id={request_id}" if request_id else ""
        print(f"request failed cmd={cmd}{req_note}: {exc}", flush=True)
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


def force_debug_cash(client: PipeClient, timeout_ms: int, amount: int) -> bool:
    resp = try_send_session_command(client, "Game.SetMoney", {"money": int(amount)}, timeout_ms)
    if resp is None:
        return False
    ok = bool(resp.get("ok", False))
    code = resp.get("code")
    reason = resp.get("reason")
    print(
        f"[debug_cash] amount={int(amount)} ok={ok} code={code} reason={reason}{request_id_suffix(resp)}",
        flush=True,
    )
    return ok


def force_debug_deshroud(client: PipeClient, timeout_ms: int) -> bool:
    resp = try_send_session_command(client, "Game.DebugDeshroud", {}, timeout_ms)
    if resp is None:
        return False
    ok = bool(resp.get("ok", False))
    code = resp.get("code")
    reason = resp.get("reason")
    print(
        f"[debug_deshroud] ok={ok} code={code} reason={reason}{request_id_suffix(resp)}",
        flush=True,
    )
    return ok


def reset_adapter_log(client: PipeClient, timeout_ms: int) -> bool:
    resp = try_send_session_command(client, "Adapter.Log.Reset", {"truncate": True}, timeout_ms)
    if resp is None:
        return False
    ok = bool(resp.get("ok", False))
    code = resp.get("code")
    reason = resp.get("reason")
    print(
        f"[adapter_log_reset] ok={ok} code={code} reason={reason}{request_id_suffix(resp)}",
        flush=True,
    )
    return ok


def configure_worker_rule(
    client: PipeClient,
    timeout_ms: int,
    *,
    min_idle_workers: int,
    queue_count: int,
    producer_kind: str | None = None,
    player_index: int | None = None,
    cooldown_ms: int = 3000,
) -> bool:
    args: dict[str, Any] = {
        "min_idle_workers": int(min_idle_workers),
        "queue_count": int(queue_count),
        "cooldown_ms": int(cooldown_ms),
    }
    if producer_kind:
        args["producer_kind"] = producer_kind
    if player_index is not None:
        args["player_index"] = int(player_index)
    resp = try_send_session_command(client, "Automation.ConfigureWorkerRule", args, timeout_ms)
    if resp is None:
        return False
    ok = bool(resp.get("ok", False))
    code = resp.get("code")
    reason = resp.get("reason")
    print(
        "[worker_rule] "
        f"min_idle_workers={int(min_idle_workers)} queue_count={int(queue_count)} "
        f"producer_kind={producer_kind or 'default'} cooldown_ms={int(cooldown_ms)} "
        f"ok={ok} code={code} reason={reason}{request_id_suffix(resp)}",
        flush=True,
    )
    return ok


def configure_attack_rule(
    client: PipeClient,
    timeout_ms: int,
    *,
    min_units: int,
    group_size: int,
    distance: float,
    cooldown_ms: int,
    player_index: int | None = None,
) -> bool:
    args: dict[str, Any] = {
        "min_units": int(min_units),
        "group_size": int(group_size),
        "distance": float(distance),
        "cooldown_ms": int(cooldown_ms),
    }
    if player_index is not None:
        args["player_index"] = int(player_index)
    resp = try_send_session_command(client, "Automation.ConfigureAttackRule", args, timeout_ms)
    if resp is None:
        return False
    ok = bool(resp.get("ok", False))
    code = resp.get("code")
    reason = resp.get("reason")
    print(
        "[attack_rule] "
        f"min_units={int(min_units)} group_size={int(group_size)} "
        f"distance={float(distance):.1f} cooldown_ms={int(cooldown_ms)} "
        f"ok={ok} code={code} reason={reason}{request_id_suffix(resp)}",
        flush=True,
    )
    return ok


def configure_stash_worker_rule(
    client: PipeClient,
    timeout_ms: int,
    *,
    target_workers_per_stash: int,
    cooldown_ms: int,
    player_index: int | None = None,
) -> bool:
    args: dict[str, Any] = {
        "target_workers_per_stash": int(target_workers_per_stash),
        "cooldown_ms": int(cooldown_ms),
    }
    if player_index is not None:
        args["player_index"] = int(player_index)
    resp = try_send_session_command(client, "Automation.ConfigureStashWorkerRule", args, timeout_ms)
    if resp is None:
        return False
    ok = bool(resp.get("ok", False))
    code = resp.get("code")
    reason = resp.get("reason")
    print(
        "[stash_worker_rule] "
        f"target_workers_per_stash={int(target_workers_per_stash)} "
        f"cooldown_ms={int(cooldown_ms)} "
        f"ok={ok} code={code} reason={reason}{request_id_suffix(resp)}",
        flush=True,
    )
    return ok


def configure_capture_rule(
    client: PipeClient,
    timeout_ms: int,
    *,
    max_concurrent: int,
    cooldown_ms: int,
    prefer_idle: bool = True,
    player_index: int | None = None,
) -> bool:
    args: dict[str, Any] = {
        "max_concurrent": int(max_concurrent),
        "cooldown_ms": int(cooldown_ms),
        "prefer_idle": bool(prefer_idle),
    }
    if player_index is not None:
        args["player_index"] = int(player_index)
    resp = try_send_session_command(client, "Automation.ConfigureCaptureRule", args, timeout_ms)
    if resp is None:
        return False
    ok = bool(resp.get("ok", False))
    code = resp.get("code")
    reason = resp.get("reason")
    print(
        "[capture_rule] "
        f"max_concurrent={int(max_concurrent)} prefer_idle={bool(prefer_idle)} "
        f"cooldown_ms={int(cooldown_ms)} ok={ok} code={code} reason={reason}{request_id_suffix(resp)}",
        flush=True,
    )
    return ok


def configure_radar_van_rule(
    client: PipeClient,
    timeout_ms: int,
    *,
    min_count: int,
    cooldown_ms: int,
    player_index: int | None = None,
) -> bool:
    args: dict[str, Any] = {
        "min_count": int(min_count),
        "cooldown_ms": int(cooldown_ms),
    }
    if player_index is not None:
        args["player_index"] = int(player_index)
    resp = try_send_session_command(client, "Automation.ConfigureRadarVanRule", args, timeout_ms)
    if resp is None:
        return False
    ok = bool(resp.get("ok", False))
    code = resp.get("code")
    reason = resp.get("reason")
    print(
        "[radar_van_rule] "
        f"min_count={int(min_count)} cooldown_ms={int(cooldown_ms)} "
        f"ok={ok} code={code} reason={reason}{request_id_suffix(resp)}",
        flush=True,
    )
    return ok


def query_game_status(
    client: PipeClient,
    timeout_ms: int,
    previous_status: dict[str, int] | None = None,
) -> dict[str, int]:
    previous = dict(previous_status or {})
    resp = try_send_session_command(client, "Game.Query", {"path": "game.status"}, timeout_ms)
    if resp is None or resp.get("ok") is False:
        return previous

    payload = extract_payload(resp)
    if not isinstance(payload, dict):
        return previous

    resources = payload.get("resources") if isinstance(payload.get("resources"), dict) else payload
    out = dict(previous)
    science_points = resources.get("science_purchase_points")
    rank_level = resources.get("rank_level")
    skill_points = resources.get("skill_points")
    if isinstance(science_points, int):
        out["science_purchase_points"] = science_points
    if isinstance(rank_level, int):
        out["rank_level"] = rank_level
    if isinstance(skill_points, int):
        out["skill_points"] = skill_points
    return out


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


def query_supply_sources(
    client: PipeClient,
    timeout_ms: int,
) -> list[dict[str, Any]]:
    resp = try_send_session_command(client, "Game.FindSupplySources", {}, timeout_ms)
    if resp is None or resp.get("ok") is False:
        return []
    payload = extract_payload(resp)
    if not isinstance(payload, dict):
        return []
    rows = payload.get("sources")
    return rows if isinstance(rows, list) else []


def query_capturable_buildings(
    client: PipeClient,
    timeout_ms: int,
    previous_buildings: list[dict[str, Any]] | None = None,
) -> list[dict[str, Any]]:
    resp = try_send_session_command(client, "Game.Query", {"path": "game.capturable_buildings"}, timeout_ms)
    if resp is None or resp.get("ok") is False:
        return list(previous_buildings or [])
    payload = extract_payload(resp)
    if not isinstance(payload, dict):
        return list(previous_buildings or [])
    rows = payload.get("buildings")
    return rows if isinstance(rows, list) else list(previous_buildings or [])


def build_supply_source_position_map(rows: list[dict[str, Any]]) -> dict[int, tuple[float, float]]:
    positions: dict[int, tuple[float, float]] = {}
    for row in rows:
        if not isinstance(row, dict):
            continue
        object_id = row.get("object_id")
        x = row.get("x")
        y = row.get("y")
        if not isinstance(object_id, int):
            continue
        if not isinstance(x, (int, float)) or not isinstance(y, (int, float)):
            continue
        positions[int(object_id)] = (float(x), float(y))
    return positions


def query_grid_summary(
    client: PipeClient,
    timeout_ms: int,
    grid_cols: int,
    grid_rows: int,
    previous_summary: dict[str, Any] | None = None,
) -> tuple[dict[str, Any] | None, bool]:
    resp = try_send_session_command(
        client,
        "Game.Query",
        {"path": "game.grid", "grid_cols": max(1, int(grid_cols)), "grid_rows": max(1, int(grid_rows))},
        timeout_ms,
    )
    if resp is None or resp.get("ok") is False:
        return previous_summary, False
    payload = extract_payload(resp)
    return (payload if isinstance(payload, dict) else previous_summary), True


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


def is_barracks_template(template_name: str) -> bool:
    return "barracks" in template_name.lower()


def is_rebel_template(template_name: str) -> bool:
    text = template_name.lower()
    return "rebel" in text and "rpg" not in text


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


def collect_barracks_ids(buildings: list[dict[str, Any]]) -> list[int]:
    ids: list[int] = []
    for row in buildings:
        if not isinstance(row, dict):
            continue
        if bool(row.get("under_construction", False)):
            continue
        object_id = parse_object_id(row)
        if object_id is None:
            continue
        if is_barracks_template(template_text(row)):
            ids.append(object_id)
    return ids


def collect_idle_rebel_ids(units: list[dict[str, Any]]) -> list[int]:
    ids: list[int] = []
    for row in units:
        if not isinstance(row, dict):
            continue
        if bool(row.get("under_construction", False)):
            continue
        if not bool(row.get("idle", False)):
            continue
        object_id = parse_object_id(row)
        if object_id is None:
            continue
        if is_rebel_template(template_text(row)):
            ids.append(object_id)
    return ids


def choose_capture_assignments(
    rebel_ids: list[int],
    units: list[dict[str, Any]],
    targets: list[dict[str, Any]],
    claimed_target_ids: set[int],
) -> list[tuple[int, int]]:
    if not rebel_ids or not targets:
        return []

    rebel_positions: dict[int, tuple[float, float]] = {}
    for row in units:
        if not isinstance(row, dict):
            continue
        object_id = parse_object_id(row)
        if object_id is None or object_id not in rebel_ids:
            continue
        x = row.get("x")
        y = row.get("y")
        if isinstance(x, (int, float)) and isinstance(y, (int, float)):
            rebel_positions[object_id] = (float(x), float(y))

    assignments: list[tuple[int, int]] = []
    used_rebels: set[int] = set()
    for target in targets:
        if not isinstance(target, dict):
            continue
        target_id = parse_object_id(target)
        if target_id is None or target_id in claimed_target_ids:
            continue
        x = target.get("x")
        y = target.get("y")
        if not isinstance(x, (int, float)) or not isinstance(y, (int, float)):
            continue

        best_rebel_id: int | None = None
        best_dist_sq = 0.0
        for rebel_id in rebel_ids:
            if rebel_id in used_rebels:
                continue
            pos = rebel_positions.get(rebel_id)
            if pos is None:
                continue
            dx = pos[0] - float(x)
            dy = pos[1] - float(y)
            dist_sq = (dx * dx) + (dy * dy)
            if best_rebel_id is None or dist_sq < best_dist_sq:
                best_rebel_id = rebel_id
                best_dist_sq = dist_sq

        if best_rebel_id is None:
            continue
        used_rebels.add(best_rebel_id)
        assignments.append((best_rebel_id, target_id))
    return assignments


def has_stash_near_supply_source(
    buildings: list[dict[str, Any]],
    supply_source_id: int,
    supply_source_positions: dict[int, tuple[float, float]] | None,
    claim_radius: float = DEFAULT_STASH_SUPPLY_CLAIM_RADIUS,
) -> bool:
    if not supply_source_positions:
        return False
    source_pos = supply_source_positions.get(int(supply_source_id))
    if source_pos is None:
        return False
    radius_sq = float(claim_radius) * float(claim_radius)
    for row in buildings:
        if not isinstance(row, dict):
            continue
        if not is_supply_stash_template(template_text(row)):
            continue
        x = row.get("x")
        y = row.get("y")
        if not isinstance(x, (int, float)) or not isinstance(y, (int, float)):
            continue
        dx = float(x) - source_pos[0]
        dy = float(y) - source_pos[1]
        if (dx * dx + dy * dy) <= radius_sq:
            return True
    return False


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


def choose_supply_source_ids_near_anchor(
    sources: list[dict[str, Any]],
    anchor_x: float,
    anchor_y: float,
    limit: int,
) -> list[int]:
    ranked: list[tuple[float, float, int]] = []
    for row in sources:
        if not isinstance(row, dict):
            continue
        object_id = parse_object_id(row)
        if object_id is None:
            continue
        x = row.get("x")
        y = row.get("y")
        if not isinstance(x, (int, float)) or not isinstance(y, (int, float)):
            continue
        dx = float(x) - anchor_x
        dy = float(y) - anchor_y
        cash_value = row.get("cash_value")
        ranked.append(
            (
                (dx * dx) + (dy * dy),
                -float(cash_value) if isinstance(cash_value, (int, float)) else 0.0,
                object_id,
            )
        )
    ranked.sort()
    chosen: list[int] = []
    for _distance_sq, _cash_sort, object_id in ranked:
        if object_id not in chosen:
            chosen.append(object_id)
        if len(chosen) >= max(0, int(limit)):
            break
    return chosen


def make_grid_column_label(col_index: int) -> str:
    if col_index < 0:
        return ""
    label = ""
    value = col_index
    while True:
        value, remainder = divmod(value, 26)
        label = chr(ord("A") + remainder) + label
        if value <= 0:
            break
        value -= 1
    return label


def make_grid_cell_label(col_index: int, row_index: int) -> str:
    return f"{make_grid_column_label(col_index)}{row_index + 1}"


def build_grid_targets(
    grid_summary: dict[str, Any] | None,
    anchor_x: float,
    anchor_y: float,
    local_player_index: int | None = None,
    max_distance: float = DEFAULT_GRID_EXPANSION_MAX_DISTANCE,
) -> list[dict[str, Any]]:
    if not isinstance(grid_summary, dict):
        return []

    grid_cols = int(grid_summary.get("grid_cols", 0)) if isinstance(grid_summary.get("grid_cols"), int) else 0
    grid_rows = int(grid_summary.get("grid_rows", 0)) if isinstance(grid_summary.get("grid_rows"), int) else 0
    map_node = grid_summary.get("map")
    if grid_cols <= 0 or grid_rows <= 0 or not isinstance(map_node, dict):
        return []

    min_x = map_node.get("min_x")
    min_y = map_node.get("min_y")
    max_x = map_node.get("max_x")
    max_y = map_node.get("max_y")
    if not all(isinstance(v, (int, float)) for v in (min_x, min_y, max_x, max_y)):
        return []

    map_min_x = float(min_x)
    map_min_y = float(min_y)
    map_max_x = float(max_x)
    map_max_y = float(max_y)
    max_distance_sq = max(256.0, float(max_distance)) ** 2
    cell_w = (map_max_x - map_min_x) / float(grid_cols) if grid_cols > 0 else 0.0
    cell_h = (map_max_y - map_min_y) / float(grid_rows) if grid_rows > 0 else 0.0
    if cell_w <= 0.0 or cell_h <= 0.0:
        return []

    occupied: dict[str, dict[str, Any]] = {}
    rows = grid_summary.get("cells")
    if isinstance(rows, list):
        for raw in rows:
            if not isinstance(raw, dict):
                continue
            label = str(raw.get("cell", "")).strip().upper()
            if label:
                occupied[label] = raw

    targets: list[dict[str, Any]] = []
    for row in range(grid_rows):
        center_y = map_min_y + ((row + 0.5) * cell_h)
        for col in range(grid_cols):
            center_x = map_min_x + ((col + 0.5) * cell_w)
            label = make_grid_cell_label(col, row)
            raw = occupied.get(label, {})
            objects_total = int(raw.get("objects_total", 0)) if isinstance(raw.get("objects_total"), int) else 0
            buildings = int(raw.get("buildings", 0)) if isinstance(raw.get("buildings"), int) else 0
            units = int(raw.get("units", 0)) if isinstance(raw.get("units"), int) else 0
            distance_sq = ((center_x - anchor_x) ** 2) + ((center_y - anchor_y) ** 2)
            if distance_sq > max_distance_sq:
                continue
            dominant_player_index = int(raw.get("dominant_player_index", -1)) if isinstance(raw.get("dominant_player_index"), int) else -1
            player_counts = raw.get("player_counts") if isinstance(raw.get("player_counts"), dict) else {}
            local_total = 0
            enemy_total = 0
            if local_player_index is not None:
                local_node = player_counts.get(str(local_player_index))
                if isinstance(local_node, dict) and isinstance(local_node.get("total"), int):
                    local_total = int(local_node.get("total", 0))
                for key, node in player_counts.items():
                    if not isinstance(node, dict) or not isinstance(node.get("total"), int):
                        continue
                    try:
                        player_index = int(key)
                    except (TypeError, ValueError):
                        continue
                    if player_index == local_player_index or player_index < 0:
                        continue
                    enemy_total = max(enemy_total, int(node.get("total", 0)))
            enemy_penalty = 0
            if local_player_index is not None and dominant_player_index >= 0 and dominant_player_index != local_player_index:
                enemy_penalty += 10
            if enemy_total > 0 and local_total <= 0:
                enemy_penalty += 6
            occupancy_penalty = (3 if buildings > 0 else 0) + (1 if units > 0 else 0)
            targets.append(
                {
                    "cell": label,
                    "col": col,
                    "row": row,
                    "center": (center_x, center_y),
                    "objects_total": objects_total,
                    "buildings": buildings,
                    "units": units,
                    "dominant_player_index": dominant_player_index,
                    "local_total": local_total,
                    "enemy_total": enemy_total,
                    "distance_sq": distance_sq,
                    "sort_key": (enemy_penalty, occupancy_penalty, objects_total, distance_sq, row, col),
                }
            )

    targets.sort(key=lambda item: item["sort_key"])
    return targets


def current_expansion_anchor(
    grid_targets: list[dict[str, Any]],
    grid_index: int,
    zone_centers: list[tuple[float, float]],
    zone_index: int,
) -> tuple[tuple[float, float], str]:
    if grid_targets:
        chosen = grid_targets[grid_index % len(grid_targets)]
        return chosen["center"], str(chosen["cell"])
    return zone_centers[zone_index], f"zone[{zone_index}]"


def current_economic_anchor(
    grid_targets: list[dict[str, Any]],
    grid_index: int,
    zone_centers: list[tuple[float, float]],
    zone_index: int,
) -> tuple[tuple[float, float], str]:
    if grid_targets:
        if grid_index <= 0:
            chosen = grid_targets[0]
        else:
            chosen = grid_targets[(grid_index - 1) % len(grid_targets)]
        return chosen["center"], str(chosen["cell"])
    fallback_index = max(0, zone_index - 1)
    return zone_centers[fallback_index], f"zone[{fallback_index}]"


def find_grid_index_by_label(grid_targets: list[dict[str, Any]], label: str) -> int | None:
    if not label:
        return None
    wanted = label.strip().upper()
    for idx, item in enumerate(grid_targets):
        if str(item.get("cell", "")).upper() == wanted:
            return idx
    return None


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
    max_to_add: int,
) -> int:
    remaining = max(0, producer_target - producer_worker_issued.get(producer_id, 0))
    if remaining <= 0:
        return 0
    remaining = min(remaining, WORKER_TRICKLE_PER_PRODUCER_PER_TICK, max(0, int(max_to_add)))
    if remaining <= 0:
        return 0

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
        f"last_code={last_code} last_reason={last_reason}{request_id_suffix(resp if sent_ok > 0 or last_code is not None or last_reason is not None else None)}",
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
    max_total_to_add: int,
    pending_until_by_key: dict[str, float],
    now: float,
    pending_cooldown_sec: float,
) -> int:
    if max_total_to_add <= 0:
        return 0
    total_added = 0
    for producer_id in sorted(producer_worker_issued):
        if total_added >= max_total_to_add:
            break
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
            max_to_add=max_total_to_add - total_added,
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
            f"[loop {cycle}] infantry_mix cmd={cmd} ok={ok} code={last_code} reason={last_reason}{request_id_suffix(resp)}",
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
            f"[loop {cycle}] vehicle_mix cmd=Game.QueueRadarVan ok={ok} code={last_code} reason={last_reason}{request_id_suffix(resp)}",
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
            print(
                f"[loop {cycle}] vehicle_mix cmd=Game.QueueRadarVansAllWarFactories ok=True "
                f"code={last_code} reason={last_reason}{request_id_suffix(fallback)}",
                flush=True,
            )

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
            f"[loop {cycle}] vehicle_mix cmd={cmd} ok={ok} code={last_code} reason={last_reason}{request_id_suffix(resp)}",
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
    stash_supply_source_ids: list[int] | None = None,
    stash_supply_source_positions: dict[int, tuple[float, float]] | None = None,
    claimed_supply_source_ids: set[int] | None = None,
    available_idle_workers: int | None = None,
    idle_worker_reserve: int = DEFAULT_BUILD_TICK_IDLE_RESERVE,
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
    stash_source_queue = list(stash_supply_source_ids or [])
    build_budget_remaining: int | None = None
    if available_idle_workers is not None:
        build_budget_remaining = max(0, int(available_idle_workers) - max(0, int(idle_worker_reserve)))

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
            if build_budget_remaining is not None and build_budget_remaining <= 0:
                print(
                    f"[loop {cycle}] building_mix cmd={cmd} skip=idle_worker_budget_exhausted "
                    f"reserve={max(0, int(idle_worker_reserve))}",
                    flush=True,
                )
                return sent_ok, last_code, last_reason
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
            if cmd == "Game.BuildSupplyStashSmart":
                requested_supply_id = stash_source_queue.pop(0) if stash_source_queue else None
                if requested_supply_id is not None:
                    if claimed_supply_source_ids is not None and int(requested_supply_id) in claimed_supply_source_ids:
                        print(
                            f"[loop {cycle}] building_mix cmd={cmd} target=GLASupplyStash "
                            f"supply_source_id={requested_supply_id} skip=supply_already_claimed",
                            flush=True,
                        )
                        continue
                    if buildings is not None and has_stash_near_supply_source(
                        buildings,
                        int(requested_supply_id),
                        stash_supply_source_positions,
                    ):
                        print(
                            f"[loop {cycle}] building_mix cmd={cmd} target=GLASupplyStash "
                            f"supply_source_id={requested_supply_id} skip=supply_already_claimed",
                            flush=True,
                        )
                        continue
                    command_args["supply_source_id"] = int(requested_supply_id)
                    print(
                        f"[loop {cycle}] building_mix cmd={cmd} target=GLASupplyStash supply_source_id={requested_supply_id}",
                        flush=True,
                    )
                else:
                    # Let the adapter pick the nearest supply source for the selected worker/dozer
                    # when we do not have an explicit target.
                    command_args.pop("zone_center", None)
                    command_args.pop("zone_radius", None)
                    command_args.pop("strict_zone", None)
            build_target = resolve_build_target(cmd, command_args)
            resp = try_send_session_command(client, cmd, command_args, timeout_ms)
            if build_budget_remaining is not None:
                build_budget_remaining = max(0, build_budget_remaining - 1)
            if resp is None:
                continue
            ok = bool(resp.get("ok", False))
            last_code = resp.get("code")
            last_reason = resp.get("reason")
            print(
                f"[loop {cycle}] building_mix cmd={cmd} target={build_target} "
                f"ok={ok} code={last_code} reason={last_reason}{request_id_suffix(resp)}",
                flush=True,
            )
            if ok:
                sent_ok += 1
                if cmd == "Game.BuildSupplyStashSmart" and requested_supply_id is not None and claimed_supply_source_ids is not None:
                    claimed_supply_source_ids.add(int(requested_supply_id))
            elif last_reason == "idle_worker_not_found":
                print(
                    f"[loop {cycle}] building_mix cmd={cmd} stop=idle_worker_starved",
                    flush=True,
                )
                return sent_ok, last_code, last_reason

    return sent_ok, last_code, last_reason


def defense_mix_needs_workers(
    tunnel_networks_count: int,
    stinger_sites_count: int,
) -> bool:
    return max(0, int(tunnel_networks_count)) > 0 or max(0, int(stinger_sites_count)) > 0


def maybe_purchase_science(
    client: PipeClient,
    timeout_ms: int,
    cycle: int,
    science_points: int,
    purchased_sciences: set[str],
    pending_until_by_key: dict[str, float],
    now: float,
    pending_cooldown_sec: float,
) -> bool:
    if science_points <= 0:
        return False

    for science_name in SCIENCE_PURCHASE_PLAN:
        if science_name in purchased_sciences:
            continue
        pending_key = f"Game.PurchaseScience:{science_name}"
        if is_pending(pending_until_by_key, pending_key, now):
            return False
        resp = try_send_session_command(client, "Game.PurchaseScience", {"science_name": science_name}, timeout_ms)
        if resp is None:
            return False
        ok = bool(resp.get("ok", False))
        code = resp.get("code")
        reason = resp.get("reason")
        print(
            f"[loop {cycle}] science_purchase science={science_name} points={science_points} "
            f"ok={ok} code={code} reason={reason}{request_id_suffix(resp)}",
            flush=True,
        )
        if ok:
            purchased_sciences.add(science_name)
            mark_pending(pending_until_by_key, pending_key, now, pending_cooldown_sec)
            return True
        if reason == "science_not_found":
            purchased_sciences.add(science_name)
            continue
        if reason == "science_not_purchasable":
            continue
        return False
    return False


def maybe_queue_upgrade(
    client: PipeClient,
    timeout_ms: int,
    cycle: int,
    completed_palaces: int,
    completed_black_markets: int,
    queued_or_completed_upgrades: set[str],
    pending_until_by_key: dict[str, float],
    now: float,
    pending_cooldown_sec: float,
) -> bool:
    if completed_palaces <= 0 and completed_black_markets <= 0:
        return False

    for producer_kind, upgrade_name in UPGRADE_QUEUE_PLAN:
        if upgrade_name in queued_or_completed_upgrades:
            continue
        if producer_kind == "palace" and completed_palaces <= 0:
            continue
        if producer_kind == "black_market" and completed_black_markets <= 0:
            continue
        pending_key = f"Game.QueueUpgrade:{producer_kind}:{upgrade_name}"
        if is_pending(pending_until_by_key, pending_key, now):
            return False
        resp = try_send_session_command(
            client,
            "Game.QueueUpgrade",
            {"producer_kind": producer_kind, "upgrade_name": upgrade_name},
            timeout_ms,
        )
        if resp is None:
            return False
        ok = bool(resp.get("ok", False))
        code = resp.get("code")
        reason = resp.get("reason")
        print(
            f"[loop {cycle}] upgrade_queue producer={producer_kind} upgrade={upgrade_name} "
            f"ok={ok} code={code} reason={reason}{request_id_suffix(resp)}",
            flush=True,
        )
        if ok:
            queued_or_completed_upgrades.add(upgrade_name)
            mark_pending(pending_until_by_key, pending_key, now, pending_cooldown_sec)
            return True
        if reason in ("upgrade_already_complete", "upgrade_already_in_production", "upgrade_already_in_queue"):
            queued_or_completed_upgrades.add(upgrade_name)
            continue
        if reason in ("upgrade_not_found", "producer_cannot_make_upgrade", "producer_cannot_receive_upgrade"):
            queued_or_completed_upgrades.add(upgrade_name)
            continue
        if reason in ("palace_not_found", "black_market_not_found"):
            continue
        if reason in ("no_money", "queue_full", "cannot_queue_upgrade"):
            return False
        return False
    return False


def maybe_queue_capture_upgrade(
    client: PipeClient,
    timeout_ms: int,
    cycle: int,
    barracks_ids: list[int],
    queued_or_completed_upgrades: set[str],
    pending_until_by_key: dict[str, float],
    now: float,
    pending_cooldown_sec: float,
) -> bool:
    if not barracks_ids or CAPTURE_BUILDING_UPGRADE in queued_or_completed_upgrades:
        return False
    pending_key = f"Game.QueueUpgrade:barracks:{CAPTURE_BUILDING_UPGRADE}"
    if is_pending(pending_until_by_key, pending_key, now):
        return False

    producer_id = int(barracks_ids[0])
    resp = try_send_session_command(
        client,
        "Game.QueueUpgrade",
        {"producer_object_id": producer_id, "upgrade_name": CAPTURE_BUILDING_UPGRADE},
        timeout_ms,
    )
    if resp is None:
        return False
    ok = bool(resp.get("ok", False))
    code = resp.get("code")
    reason = resp.get("reason")
    print(
        f"[loop {cycle}] upgrade_queue producer=barracks producer_id={producer_id} "
        f"upgrade={CAPTURE_BUILDING_UPGRADE} ok={ok} code={code} reason={reason}{request_id_suffix(resp)}",
        flush=True,
    )
    if ok:
        queued_or_completed_upgrades.add(CAPTURE_BUILDING_UPGRADE)
        mark_pending(pending_until_by_key, pending_key, now, pending_cooldown_sec)
        return True
    if reason in ("upgrade_already_complete", "upgrade_already_in_production", "upgrade_already_in_queue"):
        queued_or_completed_upgrades.add(CAPTURE_BUILDING_UPGRADE)
        return False
    return False


def maybe_capture_buildings(
    client: PipeClient,
    timeout_ms: int,
    cycle: int,
    units: list[dict[str, Any]],
    capturable_buildings: list[dict[str, Any]],
    pending_until_by_key: dict[str, float],
    now: float,
    pending_cooldown_sec: float,
) -> int:
    rebel_ids = collect_idle_rebel_ids(units)
    if not rebel_ids:
        print(
            f"[loop {cycle}] capture_skip reason=no_idle_rebels known_units={len(units)} targets={len(capturable_buildings)}",
            flush=True,
        )
        return 0
    if not capturable_buildings:
        print(
            f"[loop {cycle}] capture_skip reason=no_capturable_targets known_units={len(units)}",
            flush=True,
        )
        return 0

    claimed_target_ids: set[int] = set()
    for key, until in pending_until_by_key.items():
        if not key.startswith("Game.CaptureBuilding:") or until <= now:
            continue
        try:
            claimed_target_ids.add(int(key.rsplit(":", 1)[1]))
        except ValueError:
            continue

    assignments = choose_capture_assignments(rebel_ids, units, capturable_buildings, claimed_target_ids)
    if not assignments:
        print(
            f"[loop {cycle}] capture_skip reason=no_assignments rebels={len(rebel_ids)} "
            f"targets={len(capturable_buildings)} claimed={len(claimed_target_ids)}",
            flush=True,
        )
        return 0
    sent_ok = 0
    for rebel_id, target_id in assignments:
        pending_key = f"Game.CaptureBuilding:{target_id}"
        if is_pending(pending_until_by_key, pending_key, now):
            continue
        resp = try_send_session_command(
            client,
            "Game.CaptureBuilding",
            {"source_object_id": rebel_id, "target_object_id": target_id},
            timeout_ms,
        )
        if resp is None:
            continue
        ok = bool(resp.get("ok", False))
        code = resp.get("code")
        reason = resp.get("reason")
        print(
            f"[loop {cycle}] capture_building source_id={rebel_id} target_id={target_id} "
            f"ok={ok} code={code} reason={reason}{request_id_suffix(resp)}",
            flush=True,
        )
        if ok:
            sent_ok += 1
            mark_pending(pending_until_by_key, pending_key, now, max(20.0, pending_cooldown_sec))
    return sent_ok


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
    parser.add_argument("--grid-cols", type=int, default=DEFAULT_GRID_COLS, help="Expansion grid column count (default: 32).")
    parser.add_argument("--grid-rows", type=int, default=DEFAULT_GRID_ROWS, help="Expansion grid row count (default: 32).")
    parser.add_argument(
        "--grid-query-every-cycles",
        type=int,
        default=DEFAULT_GRID_QUERY_EVERY_CYCLES,
        help="Refresh the coarse grid summary every N cycles (default: 2).",
    )
    parser.add_argument(
        "--tech-check-every-cycles",
        type=int,
        default=DEFAULT_TECH_CHECK_EVERY_CYCLES,
        help="Attempt promotions and upgrades every N cycles when available (default: 2).",
    )
    parser.add_argument(
        "--expansion-command-center-every",
        type=int,
        default=DEFAULT_EXPANSION_COMMAND_CENTER_EVERY,
        help="Attempt one new Command Center every N expansion passes (default: 5).",
    )
    parser.add_argument(
        "--command-center-min-money",
        type=int,
        default=DEFAULT_COMMAND_CENTER_MIN_MONEY,
        help="Minimum money required before attempting an expansion Command Center (default: 6000).",
    )
    parser.add_argument(
        "--expansion-arms-every",
        type=int,
        default=DEFAULT_EXPANSION_ARMS_EVERY,
        help="Attempt one new Arms Dealer every N expansion passes (default: 4).",
    )
    parser.add_argument(
        "--arms-dealer-min-money",
        type=int,
        default=DEFAULT_ARMS_DEALER_MIN_MONEY,
        help="Minimum money required before attempting an expansion Arms Dealer (default: 5000).",
    )
    parser.add_argument(
        "--expansion-infantry-every",
        type=int,
        default=DEFAULT_EXPANSION_INFANTRY_EVERY,
        help="Queue a small infantry mix every N expansion passes (default: 3).",
    )
    parser.add_argument(
        "--expansion-vehicle-every",
        type=int,
        default=DEFAULT_EXPANSION_VEHICLE_EVERY,
        help="Queue a small vehicle mix every N expansion passes (default: 5).",
    )
    parser.add_argument(
        "--expansion-unit-min-money",
        type=int,
        default=DEFAULT_EXPANSION_UNIT_MIN_MONEY,
        help="Minimum money required before queuing defensive expansion units (default: 7000).",
    )
    parser.add_argument(
        "--market-bootstrap-target",
        type=int,
        default=DEFAULT_MARKET_BOOTSTRAP_TARGET,
        help="Minimum Black Markets to force immediately after Palace before broader expansion spending resumes (default: 2).",
    )
    parser.add_argument(
        "--market-bootstrap-max-money",
        type=int,
        default=DEFAULT_MARKET_BOOTSTRAP_MAX_MONEY,
        help="Treat expansion as cash-poor below this while bootstrapping Black Markets (default: 12000).",
    )
    parser.add_argument(
        "--market-bootstrap-min-income-per-sec",
        type=float,
        default=DEFAULT_MARKET_BOOTSTRAP_MIN_INCOME_PER_SEC,
        help="Treat expansion income as insufficient below this while bootstrapping Black Markets (default: 120.0).",
    )
    parser.add_argument(
        "--disable-grid-expansion",
        action="store_true",
        help="Disable grid-driven expansion anchors and fall back to legacy template zones.",
    )
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
        "--build-tick-idle-reserve",
        type=int,
        default=DEFAULT_BUILD_TICK_IDLE_RESERVE,
        help="Minimum idle workers to keep available while issuing building bursts each tick (default: 2).",
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
        "--defense-worker-min-idle",
        type=int,
        default=DEFAULT_DEFENSE_WORKER_MIN_IDLE,
        help="Treat expansion defense as under-served when idle workers fall below this level (default: 3).",
    )
    parser.add_argument(
        "--cc-worker-pressure-cycles",
        type=int,
        default=DEFAULT_CC_WORKER_PRESSURE_CYCLES,
        help="Require this many consecutive low-idle cycles before forcing CC worker top-up (default: 3).",
    )
    parser.add_argument(
        "--eco-build-delay-after-defense-sec",
        type=float,
        default=DEFAULT_ECO_BUILD_DELAY_AFTER_DEFENSE_SEC,
        help="Delay eco build passes briefly after successful defense placement to preserve builders (default: 10).",
    )
    parser.add_argument(
        "--palace-recovery-grace-sec",
        type=float,
        default=DEFAULT_PALACE_RECOVERY_GRACE_SEC,
        help="Do not trigger palace recovery this soon after a successful palace build (default: 30).",
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
    reset_adapter_log(client, args.timeout_ms)
    configure_worker_rule(
        client,
        args.timeout_ms,
        min_idle_workers=10,
        queue_count=9,
        cooldown_ms=3000,
    )
    configure_stash_worker_rule(
        client,
        args.timeout_ms,
        target_workers_per_stash=STASH_WORKER_TARGET,
        cooldown_ms=4000,
    )
    configure_attack_rule(
        client,
        args.timeout_ms,
        min_units=max(1, int(args.raid_min_units)),
        group_size=max(1, int(args.raid_group_size)),
        distance=max(256.0, float(args.raid_distance)),
        cooldown_ms=max(1000, int(float(args.raid_cooldown_sec) * 1000.0)),
    )
    configure_capture_rule(
        client,
        args.timeout_ms,
        max_concurrent=3,
        prefer_idle=True,
        cooldown_ms=4000,
    )
    if USE_ADAPTER_RADAR_VAN_RULE:
        configure_radar_van_rule(
            client,
            args.timeout_ms,
            min_count=2,
            cooldown_ms=12000,
        )
    print(
        f"Plan phases: opening={len(PHASE_OPENING.steps)} expansion={len(PHASE_EXPANSION.steps)} "
        f"warmonger={len(PHASE_WARMONGER.steps)} | cc_worker_target={COMMAND_CENTER_WORKER_TARGET} "
        f"| stash_worker_target={STASH_WORKER_TARGET}",
        flush=True,
    )

    refresh_objects_cache(client, args.timeout_ms)
    query_objects_cache_status(client, args.timeout_ms)
    startup_units, startup_buildings = query_objects_compact(
        client,
        args.timeout_ms,
        include_units=True,
        include_buildings=True,
    )
    startup_supply_sources = query_supply_sources(client, args.timeout_ms)
    startup_supply_source_positions = build_supply_source_position_map(startup_supply_sources)
    cached_capturable_buildings = query_capturable_buildings(client, args.timeout_ms, previous_buildings=[])
    startup_counts = query_zone_counts(client, args.timeout_ms)
    startup_unit_counts = query_unit_composition(client, args.timeout_ms, previous_counts={})
    cached_status = query_game_status(client, args.timeout_ms, previous_status={})
    local_player_index = int(cached_status.get("player_index", -1)) if isinstance(cached_status.get("player_index"), int) else None
    command_center_ids, stash_ids = collect_producer_ids(startup_buildings)
    existing_workers = int(startup_unit_counts.get("workers", 0))
    startup_has_palace = has_palace_started(startup_buildings) or (
        isinstance(startup_counts, dict) and int(startup_counts.get("palaces", 0)) > 0
    )
    anchor_x, anchor_y = resolve_primary_anchor_xy(startup_buildings)
    opening_stash_target = 0
    for step in PHASE_OPENING.steps:
        if step.cmd == "Script.BuildingMix":
            opening_stash_target = int(step.args.get("stash", 0))
            break

    opening_supply_source_ids = choose_supply_source_ids_near_anchor(
        startup_supply_sources,
        anchor_x,
        anchor_y,
        limit=max(1, opening_stash_target),
    )
    opening_claimed_supply_source_ids: set[int] = set()
    zone_centers = build_zone_centers(anchor_x, anchor_y, max(128.0, float(args.zone_step)), max(1, int(args.zone_count)))
    zone_index = 0
    cached_grid_summary: dict[str, Any] | None = None
    grid_targets: list[dict[str, Any]] = []
    grid_index = 0
    active_grid_label = ""
    if not bool(args.disable_grid_expansion):
        cached_grid_summary, _ = query_grid_summary(
            client,
            args.timeout_ms,
            max(1, int(args.grid_cols)),
            max(1, int(args.grid_rows)),
        )
        grid_targets = build_grid_targets(
            cached_grid_summary,
            anchor_x,
            anchor_y,
            local_player_index=local_player_index,
        )
        if grid_targets:
            active_grid_label = str(grid_targets[0].get("cell", ""))

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
        completed_palaces=count_complete_matching_templates(startup_buildings, ("palace",)),
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
        completed_black_markets=count_complete_matching_templates(startup_buildings, ("blackmarket", "black_market")),
    )
    expansion_step_index = 0
    expansion_cycle_count = 0
    sustain_step_index = 0
    warmonger_step_index = 0
    last_attempt_at = 0.0
    last_worker_topup_at = 0.0
    last_worker_trickle_at = 0.0
    cc_worker_pressure_cycles = 0
    defense_worker_pressure_cycles = 0
    defense_worker_failure_cycles = 0
    delay_eco_until = 0.0
    last_palace_success_at = 0.0
    heavy_query_backoff_until = 0.0
    grid_query_backoff_until = 0.0
    low_money_skip_until_cycle = 0
    cycle = 1
    pending_until_by_key: dict[str, float] = {}
    purchased_sciences: set[str] = set()
    queued_or_completed_upgrades: set[str] = set()
    cached_units = startup_units
    cached_buildings = startup_buildings
    cached_unit_counts = startup_unit_counts
    if ENABLE_DEBUG_FORCED_CASH:
        force_debug_cash(client, args.timeout_ms, DEBUG_FORCED_CASH)
    if ENABLE_DEBUG_DESHROUD:
        force_debug_deshroud(client, args.timeout_ms)
    cached_money = query_money(client, args.timeout_ms, previous_money=DEBUG_FORCED_CASH)
    cached_idle_workers = query_idle_workers_count(client, args.timeout_ms, previous_count=0)
    last_money_sample = cached_money
    last_money_sample_at = startup_now
    smoothed_net_income_per_sec = 0.0
    vesting_sample_money = cached_money
    vesting_sample_started_at = startup_now
    if startup_has_palace or count_complete_matching_templates(startup_buildings, ("palace",)) > 0:
        last_palace_success_at = startup_now
    active_zone_center, active_zone_label = current_expansion_anchor(grid_targets, grid_index, zone_centers, zone_index)

    camera_set_top_down(client, args.timeout_ms, height_multiplier=max(0.25, float(args.camera_height_multiplier)))
    camera_look_at(client, args.timeout_ms, active_zone_center[0], active_zone_center[1])

    print(
        "Startup inventory: "
        f"workers={existing_workers} command_centers={len(command_center_ids)} stashes={len(stash_ids)} "
        f"opening_step={opening_step_index + 1 if opening_step_index < len(PHASE_OPENING.steps) else len(PHASE_OPENING.steps)}/{len(PHASE_OPENING.steps)} "
        f"opening_progress={opening_step_attempt_count if opening_step_index < len(PHASE_OPENING.steps) else 0} "
        f"phase={PLAN_PHASES[phase_index].name} palace_seen={startup_has_palace} transition_reason={phase_last_transition_reason or '-'} "
        f"rank={cached_status.get('rank_level', 0)} science_points={cached_status.get('science_purchase_points', 0)} "
        f"zone_anchor=({anchor_x:.1f},{anchor_y:.1f}) zone_count={len(zone_centers)} "
        f"active_anchor={active_zone_label} grid_targets={len(grid_targets)} "
        f"opening_supply_targets={opening_supply_source_ids}",
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
            if cycle % max(1, int(args.tech_check_every_cycles)) == 1:
                cached_status = query_game_status(client, args.timeout_ms, previous_status=cached_status)
            if (
                not bool(args.disable_grid_expansion)
                and cycle % max(1, int(args.grid_query_every_cycles)) == 1
                and time.monotonic() >= grid_query_backoff_until
            ):
                refreshed_grid_summary, grid_ok = query_grid_summary(
                    client,
                    args.timeout_ms,
                    max(1, int(args.grid_cols)),
                    max(1, int(args.grid_rows)),
                    previous_summary=cached_grid_summary,
                )
                if not grid_ok:
                    grid_query_backoff_until = time.monotonic() + GRID_QUERY_BACKOFF_SEC
                    print(
                        f"[query_backoff] grid timeout suspected; backoff_sec={GRID_QUERY_BACKOFF_SEC}",
                        flush=True,
                    )
                elif refreshed_grid_summary is not None:
                    previous_label = active_grid_label
                    cached_grid_summary = refreshed_grid_summary
                    grid_targets = build_grid_targets(
                        cached_grid_summary,
                        anchor_x,
                        anchor_y,
                        local_player_index=local_player_index,
                    )
                    if grid_targets:
                        remapped_index = find_grid_index_by_label(grid_targets, previous_label) if previous_label else None
                        if remapped_index is not None:
                            grid_index = remapped_index
                        else:
                            grid_index = min(grid_index, len(grid_targets) - 1)
                        active_grid_label = str(grid_targets[grid_index].get("cell", ""))
                    else:
                        grid_index = 0
                        active_grid_label = ""

            if cycle % max(1, DEFAULT_CAPTURABLE_BUILDINGS_QUERY_EVERY_CYCLES) == 1:
                cached_capturable_buildings = query_capturable_buildings(
                    client,
                    args.timeout_ms,
                    previous_buildings=cached_capturable_buildings,
                )

            units, buildings = cached_units, cached_buildings
            existing_workers = int(cached_unit_counts.get("workers", 0))
            sync_producers(
                buildings,
                command_center_worker_issued,
                stash_worker_issued,
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
            sample_elapsed_sec = max(0.001, now - last_money_sample_at)
            instantaneous_net_income_per_sec = (money - last_money_sample) / sample_elapsed_sec
            smoothed_net_income_per_sec = (
                instantaneous_net_income_per_sec
                if abs(smoothed_net_income_per_sec) < 0.001
                else ((0.35 * instantaneous_net_income_per_sec) + (0.65 * smoothed_net_income_per_sec))
            )
            last_money_sample = money
            last_money_sample_at = now
            effective_net_income_per_sec = smoothed_net_income_per_sec
            if PLAN_PHASES[phase_index].name == PHASE_SUSTAIN_NAME:
                vesting_elapsed_sec = max(0.001, now - vesting_sample_started_at)
                effective_net_income_per_sec = (money - vesting_sample_money) / vesting_elapsed_sec
            eta_to_warmonger_sec = None
            if effective_net_income_per_sec > 0.1 and money < 30000:
                eta_to_warmonger_sec = max(0.0, (30000 - money) / effective_net_income_per_sec)
            elif money >= 30000:
                eta_to_warmonger_sec = 0.0

            completed_palaces_for_transition = count_complete_matching_templates(buildings, ("palace",))
            completed_black_markets_for_transition = count_complete_matching_templates(buildings, ("blackmarket", "black_market"))
            previous_phase_index = phase_index
            phase_index, phase_entered_at_monotonic, phase_last_transition_reason = maybe_transition_phase(
                phase_index=phase_index,
                opening_step_index=opening_step_index,
                assets=assets,
                completed_palaces=completed_palaces_for_transition,
                opening_core_ready=opening_building_mix_ready(buildings),
                money=money,
                now_monotonic=now,
                phase_entered_at_monotonic=phase_entered_at_monotonic,
                cycle=cycle,
                completed_black_markets=completed_black_markets_for_transition,
                net_income_per_sec=effective_net_income_per_sec,
                eta_to_warmonger_sec=eta_to_warmonger_sec,
            )
            if phase_index != previous_phase_index:
                if PLAN_PHASES[phase_index].name == PHASE_EXPANSION_NAME:
                    expansion_step_index = 0
                elif PLAN_PHASES[phase_index].name == PHASE_SUSTAIN_NAME:
                    sustain_step_index = 0
                elif PLAN_PHASES[phase_index].name == PHASE_WARMONGER_NAME:
                    warmonger_step_index = 0
                vesting_sample_money = money
                vesting_sample_started_at = now

            if now - last_attempt_at < args.retry_cooldown_sec:
                time.sleep(args.tick_sec)
                continue

            guard_idle_every = max(1, int(args.guard_idle_every_cycles))
            guard_idle_key = "Game.GuardAllIdleGroundCombat"
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

            current_phase = PLAN_PHASES[phase_index]
            if not USE_ADAPTER_WORKER_RULE:
                # Keep a healthy worker pool throughout the run. GLA workers are both economy and
                # build pressure, so stopping stash top-up after opening leaves expansion starved.
                allow_cc_topup = True
                allow_stash_topup = True
                idle_skip_threshold = max(0, int(args.idle_workers_skip_topup_threshold))
                defense_worker_min_idle = max(0, int(args.defense_worker_min_idle))
                pressure_cycle_threshold = max(1, int(args.cc_worker_pressure_cycles))
                if cached_idle_workers < defense_worker_min_idle:
                    defense_worker_pressure_cycles += 1
                else:
                    defense_worker_pressure_cycles = 0
                if cached_idle_workers < idle_skip_threshold:
                    cc_worker_pressure_cycles += 1
                else:
                    cc_worker_pressure_cycles = 0

                forced_cc_topup = current_phase.name in (PHASE_EXPANSION_NAME, PHASE_SUSTAIN_NAME) and (
                    defense_worker_pressure_cycles >= pressure_cycle_threshold
                    or cc_worker_pressure_cycles >= pressure_cycle_threshold
                    or defense_worker_failure_cycles >= pressure_cycle_threshold
                )
                if forced_cc_topup:
                    print(
                        f"[loop {cycle}] worker_topup_force cc_idle_workers={cached_idle_workers} "
                        f"pressure_cycles={cc_worker_pressure_cycles} defense_pressure_cycles={defense_worker_pressure_cycles} "
                        f"defense_failure_cycles={defense_worker_failure_cycles}",
                        flush=True,
                    )
                if phase_index > 0:
                    worker_money_gate = max(0, int(args.worker_topup_min_money))
                    if money < worker_money_gate and not forced_cc_topup:
                        allow_cc_topup = False
                        print(
                            f"[loop {cycle}] worker_topup_skip cc_low_cash money={money} required={worker_money_gate}",
                            flush=True,
                        )
                if (allow_cc_topup or allow_stash_topup) and (now - last_worker_topup_at) >= DEFAULT_WORKER_TOPUP_COOLDOWN_SEC:
                    command_center_count = len(command_center_worker_issued)
                    stash_count = len(stash_worker_issued)
                    target_total_workers = (command_center_count * COMMAND_CENTER_WORKER_TARGET) + (stash_count * STASH_WORKER_TARGET if allow_stash_topup else 0)
                    worker_deficit_total = max(0, target_total_workers - existing_workers)
                    worker_topup_done = False
                    prefer_stash_topup = allow_stash_topup and current_phase.name != PHASE_WARMONGER_NAME
                    worker_pending_cooldown = 1.0
                    if idle_skip_threshold > 0 and cached_idle_workers >= idle_skip_threshold and worker_deficit_total <= 0 and not forced_cc_topup:
                        allow_cc_topup = False
                        print(
                            f"[loop {cycle}] worker_topup_skip cc_idle_workers={cached_idle_workers} "
                            f"threshold={idle_skip_threshold} target_satisfied={target_total_workers}",
                            flush=True,
                        )
                    if worker_deficit_total <= 0:
                        print(
                            f"[loop {cycle}] worker_topup_skip ratio_satisfied workers={existing_workers} "
                            f"target={target_total_workers} stashes={stash_count}",
                            flush=True,
                        )
                    if worker_deficit_total > 0 and prefer_stash_topup and not worker_topup_done and allow_stash_topup:
                        stash_added = top_up_workers_for_producers(
                            client=client,
                            timeout_ms=args.timeout_ms,
                            cycle=cycle,
                            producer_label="stash",
                            producer_target=STASH_WORKER_TARGET,
                            producer_worker_issued=stash_worker_issued,
                            max_total_to_add=worker_deficit_total,
                            pending_until_by_key=pending_until_by_key,
                            now=now,
                            pending_cooldown_sec=worker_pending_cooldown,
                        )
                        if stash_added > 0:
                            worker_topup_done = True
                            worker_deficit_total = max(0, worker_deficit_total - stash_added)
                    if worker_deficit_total > 0 and not worker_topup_done and allow_cc_topup:
                        cc_added = top_up_workers_for_producers(
                            client=client,
                            timeout_ms=args.timeout_ms,
                            cycle=cycle,
                            producer_label="command_center",
                            producer_target=COMMAND_CENTER_WORKER_TARGET,
                            producer_worker_issued=command_center_worker_issued,
                            max_total_to_add=worker_deficit_total,
                            pending_until_by_key=pending_until_by_key,
                            now=now,
                            pending_cooldown_sec=worker_pending_cooldown,
                        )
                        if cc_added > 0:
                            worker_topup_done = True
                            worker_deficit_total = max(0, worker_deficit_total - cc_added)
                    if worker_deficit_total > 0 and not worker_topup_done and allow_stash_topup:
                        stash_added = top_up_workers_for_producers(
                            client=client,
                            timeout_ms=args.timeout_ms,
                            cycle=cycle,
                            producer_label="stash",
                            producer_target=STASH_WORKER_TARGET,
                            producer_worker_issued=stash_worker_issued,
                            max_total_to_add=worker_deficit_total,
                            pending_until_by_key=pending_until_by_key,
                            now=now,
                            pending_cooldown_sec=worker_pending_cooldown,
                        )
                        if stash_added > 0:
                            worker_topup_done = True
                    if worker_topup_done:
                        last_worker_topup_at = now

            current_phase = PLAN_PHASES[phase_index]
            black_markets = assets["black_markets"]
            palaces = assets["palaces"]
            arms_total = assets["arms"]
            completed_palaces = count_complete_matching_templates(buildings, ("palace",))
            completed_black_markets = count_complete_matching_templates(buildings, ("blackmarket", "black_market"))
            completed_arms = count_complete_matching_templates(buildings, ("armsdealer", "warfactory"))
            barracks_ids = collect_barracks_ids(buildings)
            home_zone_center = zone_centers[0]
            home_zone_label = "zone[0]"
            active_zone_center, active_zone_label = current_expansion_anchor(
                grid_targets,
                grid_index,
                zone_centers,
                zone_index,
            )
            eco_zone_center, eco_zone_label = current_economic_anchor(
                grid_targets,
                grid_index,
                zone_centers,
                zone_index,
            )
            science_points = int(cached_status.get("science_purchase_points", 0))
            rank_level = int(cached_status.get("rank_level", 0))
            should_check_tech = (cycle % max(1, int(args.tech_check_every_cycles)) == 0)
            tech_ready = opening_building_mix_ready(buildings)
            if should_check_tech and tech_ready:
                science_changed = maybe_purchase_science(
                    client,
                    args.timeout_ms,
                    cycle,
                    science_points=science_points,
                    purchased_sciences=purchased_sciences,
                    pending_until_by_key=pending_until_by_key,
                    now=now,
                    pending_cooldown_sec=max(6.0, float(args.pending_cooldown_sec)),
                )
                if science_changed:
                    cached_status["science_purchase_points"] = max(0, science_points - 1)
                    print(
                        f"[loop {cycle}] tech_progress type=science rank={rank_level} "
                        f"remaining_points={cached_status.get('science_purchase_points', 0)}",
                        flush=True,
                    )
                    last_attempt_at = now
                    cycle += 1
                    time.sleep(args.tick_sec)
                    continue

                capture_upgrade_changed = maybe_queue_capture_upgrade(
                    client,
                    args.timeout_ms,
                    cycle,
                    barracks_ids=barracks_ids,
                    queued_or_completed_upgrades=queued_or_completed_upgrades,
                    pending_until_by_key=pending_until_by_key,
                    now=now,
                    pending_cooldown_sec=max(10.0, float(args.pending_cooldown_sec)),
                )
                if capture_upgrade_changed:
                    print(
                        f"[loop {cycle}] tech_progress type=upgrade rank={rank_level} "
                        f"science_points={science_points} barracks={len(barracks_ids)} upgrade={CAPTURE_BUILDING_UPGRADE}",
                        flush=True,
                    )
                    last_attempt_at = now
                    cycle += 1
                    time.sleep(args.tick_sec)
                    continue

                upgrade_changed = maybe_queue_upgrade(
                    client,
                    args.timeout_ms,
                    cycle,
                    completed_palaces=completed_palaces,
                    completed_black_markets=completed_black_markets,
                    queued_or_completed_upgrades=queued_or_completed_upgrades,
                    pending_until_by_key=pending_until_by_key,
                    now=now,
                    pending_cooldown_sec=max(10.0, float(args.pending_cooldown_sec)),
                )
                if upgrade_changed:
                    print(
                        f"[loop {cycle}] tech_progress type=upgrade rank={rank_level} "
                        f"science_points={science_points} palaces={palaces} black_markets={black_markets}",
                        flush=True,
                    )
                    last_attempt_at = now
                    cycle += 1
                    time.sleep(args.tick_sec)
                    continue

            palace_recovery_grace_sec = max(0.0, float(args.palace_recovery_grace_sec))
            can_attempt_palace_recovery = (now - last_palace_success_at) >= palace_recovery_grace_sec
            if current_phase.name != PHASE_OPENING_NAME and palaces <= 0 and can_attempt_palace_recovery:
                if is_pending(pending_until_by_key, "Game.BuildPalaceSmart", now):
                    cycle += 1
                    time.sleep(args.tick_sec)
                    continue
                if money >= MIN_MONEY_FOR_PALACE_ATTEMPT:
                    recovery_added, recovery_code, recovery_reason = try_burst_step(
                        client,
                        with_zone_args(
                            Step("Build palace recovery", "Game.BuildPalaceSmart", {}),
                            home_zone_center,
                            float(args.zone_radius),
                        ),
                        args.timeout_ms,
                    )
                    print(
                        f"[loop {cycle}] palace_recovery phase={current_phase.name} money={money} "
                        f"anchor={home_zone_label} added={recovery_added} "
                        f"last_code={recovery_code} last_reason={recovery_reason}",
                        flush=True,
                    )
                    if recovery_added > 0:
                        last_palace_success_at = now
                        mark_pending(
                            pending_until_by_key,
                            "Game.BuildPalaceSmart",
                            now,
                            float(args.pending_cooldown_sec),
                        )
                        last_attempt_at = now
                        cycle += 1
                        time.sleep(args.tick_sec)
                        continue
            elif current_phase.name != PHASE_OPENING_NAME and palaces <= 0 and not can_attempt_palace_recovery:
                print(
                    f"[loop {cycle}] palace_recovery_skip grace_remaining_sec={max(0.0, palace_recovery_grace_sec - (now - last_palace_success_at)):.1f}",
                    flush=True,
                )

            if current_phase.name != PHASE_OPENING_NAME and arms_total <= 0:
                if is_pending(pending_until_by_key, "Game.BuildArmsDealerSmart", now):
                    cycle += 1
                    time.sleep(args.tick_sec)
                    continue
                if money >= max(0, int(args.arms_dealer_min_money)):
                    recovery_added, recovery_code, recovery_reason = try_burst_step(
                        client,
                        with_zone_args(
                            Step("Build arms dealer recovery", "Game.BuildArmsDealerSmart", {}),
                            home_zone_center,
                            float(args.zone_radius),
                        ),
                        args.timeout_ms,
                    )
                    print(
                        f"[loop {cycle}] arms_recovery phase={current_phase.name} money={money} "
                        f"anchor={home_zone_label} added={recovery_added} "
                        f"last_code={recovery_code} last_reason={recovery_reason}",
                        flush=True,
                    )
                    if recovery_added > 0:
                        mark_pending(
                            pending_until_by_key,
                            "Game.BuildArmsDealerSmart",
                            now,
                            float(args.pending_cooldown_sec),
                        )
                        last_attempt_at = now
                        cycle += 1
                        time.sleep(args.tick_sec)
                        continue

            if current_phase.name == PHASE_EXPANSION_NAME:
                desired_markets_for_next_palace = max(4, (palaces + 1) * max(1, int(args.palace_market_ratio)))
                market_bootstrap_target = max(0, int(args.market_bootstrap_target))
                prereq_market_floor = 4
                pre_market_ramp = completed_palaces > 0 and black_markets < prereq_market_floor
                pre_palace_completion_ramp = palaces > 0 and completed_palaces <= 0
                tech_ramp_restriction = pre_palace_completion_ramp or pre_market_ramp
                market_bootstrap_needed = (
                    completed_palaces > 0
                    and black_markets < min(desired_markets_for_next_palace, market_bootstrap_target)
                )
                market_bootstrap_priority = market_bootstrap_needed and (
                    money <= max(0, int(args.market_bootstrap_max_money))
                    or effective_net_income_per_sec < float(args.market_bootstrap_min_income_per_sec)
                )
                should_try_market = (
                    completed_palaces > 0
                    and black_markets < desired_markets_for_next_palace
                    and money >= SUSTAIN_BLACK_MARKET_MIN_MONEY
                    and (
                        market_bootstrap_priority
                        or (cycle % SUSTAIN_MARKET_ATTEMPT_EVERY == 0)
                    )
                )
                if should_try_market and not is_pending(pending_until_by_key, "Game.BuildBlackMarketSmart", now):
                    market_added, market_code, market_reason = try_burst_step(
                        client,
                        with_zone_args(
                            Step("Build black market expansion", "Game.BuildBlackMarketSmart", {}),
                            eco_zone_center,
                            float(args.zone_radius),
                        ),
                        args.timeout_ms,
                    )
                    print(
                        f"[loop {cycle}] phase=expansion econ=black_market bm={black_markets} palaces={palaces} "
                        f"completed_palaces={completed_palaces} target_bm={desired_markets_for_next_palace} "
                        f"bootstrap_priority={market_bootstrap_priority} anchor={eco_zone_label} added={market_added} "
                        f"last_code={market_code} last_reason={market_reason}",
                        flush=True,
                    )
                    if market_added > 0:
                        mark_pending(pending_until_by_key, "Game.BuildBlackMarketSmart", now, float(args.pending_cooldown_sec))
                        if market_bootstrap_priority:
                            cycle += 1
                            last_attempt_at = now
                            time.sleep(args.tick_sec)
                            continue
                elif (cycle % SUSTAIN_MARKET_ATTEMPT_EVERY == 0) and money < SUSTAIN_BLACK_MARKET_MIN_MONEY:
                    print(
                        f"[loop {cycle}] phase=expansion econ=black_market skip=low_cash "
                        f"money={money} required={SUSTAIN_BLACK_MARKET_MIN_MONEY}",
                        flush=True,
                    )

                should_try_palace = (
                    money >= MIN_MONEY_FOR_PALACE_ATTEMPT
                    and black_markets >= desired_markets_for_next_palace
                    and (cycle % SUSTAIN_PALACE_ATTEMPT_EVERY == 0)
                )
                if should_try_palace and not is_pending(pending_until_by_key, "Game.BuildPalaceSmart", now):
                    palace_added, palace_code, palace_reason = try_burst_step(
                        client,
                        with_zone_args(
                            Step("Build palace expansion", "Game.BuildPalaceSmart", {}),
                            active_zone_center,
                            float(args.zone_radius),
                        ),
                        args.timeout_ms,
                    )
                    print(
                        f"[loop {cycle}] phase=expansion econ=palace money={money} bm={black_markets} "
                        f"palaces={palaces} anchor={active_zone_label} added={palace_added} "
                        f"last_code={palace_code} last_reason={palace_reason}",
                        flush=True,
                    )
                    if palace_added > 0:
                        last_palace_success_at = now
                        mark_pending(pending_until_by_key, "Game.BuildPalaceSmart", now, float(args.pending_cooldown_sec))

                arms_every = max(0, int(args.expansion_arms_every))
                should_try_arms = (
                    arms_every > 0
                    and ((expansion_cycle_count + 1) % arms_every == 0)
                    and money >= max(0, int(args.arms_dealer_min_money))
                    and not tech_ramp_restriction
                )
                if should_try_arms and not is_pending(pending_until_by_key, "Game.BuildArmsDealerSmart", now):
                    arms_complete_in_zone, arms_uc_in_zone = zone_building_state_counts(
                        buildings,
                        active_zone_center,
                        float(args.zone_radius),
                        ("armsdealer", "warfactory"),
                    )
                    if (arms_complete_in_zone + arms_uc_in_zone) > 0:
                        print(
                            f"[loop {cycle}] phase=expansion econ=arms skip=zone_has_arms "
                            f"anchor={active_zone_label} complete={arms_complete_in_zone} under_construction={arms_uc_in_zone}",
                            flush=True,
                        )
                    else:
                        arms_added, arms_code, arms_reason = try_burst_step(
                            client,
                            with_zone_args(
                                Step("Build arms dealer expansion", "Game.BuildArmsDealerSmart", {}),
                                active_zone_center,
                                float(args.zone_radius),
                            ),
                            args.timeout_ms,
                        )
                        print(
                            f"[loop {cycle}] phase=expansion econ=arms money={money} "
                            f"anchor={active_zone_label} total_arms={arms_total} completed_arms={completed_arms} "
                            f"added={arms_added} last_code={arms_code} last_reason={arms_reason}",
                            flush=True,
                        )
                        if arms_added > 0:
                            mark_pending(
                                pending_until_by_key,
                                "Game.BuildArmsDealerSmart",
                                now,
                                float(args.pending_cooldown_sec),
                            )

                cc_every = max(0, int(args.expansion_command_center_every))
                should_try_command_center = (
                    cc_every > 0
                    and ((expansion_cycle_count + 1) % cc_every == 0)
                    and money >= max(0, int(args.command_center_min_money))
                    and not tech_ramp_restriction
                )
                if should_try_command_center and not is_pending(pending_until_by_key, "Game.BuildCommandCenterSmart", now):
                    cc_complete, cc_uc = zone_building_state_counts(
                        buildings,
                        active_zone_center,
                        float(args.zone_radius),
                        ("commandcenter",),
                    )
                    if (cc_complete + cc_uc) > 0:
                        print(
                            f"[loop {cycle}] phase=expansion econ=command_center skip=zone_has_command_center "
                            f"anchor={active_zone_label} complete={cc_complete} under_construction={cc_uc}",
                            flush=True,
                        )
                    else:
                        cc_added, cc_code, cc_reason = try_burst_step(
                            client,
                            with_zone_args(
                                Step("Build command center expansion", "Game.BuildCommandCenterSmart", {}),
                                active_zone_center,
                                float(args.zone_radius),
                            ),
                            args.timeout_ms,
                        )
                        print(
                            f"[loop {cycle}] phase=expansion econ=command_center money={money} "
                            f"anchor={active_zone_label} added={cc_added} last_code={cc_code} last_reason={cc_reason}",
                            flush=True,
                        )
                        if cc_added > 0:
                            mark_pending(
                                pending_until_by_key,
                                "Game.BuildCommandCenterSmart",
                                now,
                                float(args.pending_cooldown_sec),
                            )

                expansion_unit_min_money = max(0, int(args.expansion_unit_min_money))
                infantry_every = max(0, int(args.expansion_infantry_every))
                should_queue_expansion_infantry = (
                    infantry_every > 0
                    and ((expansion_cycle_count + 1) % infantry_every == 0)
                    and money >= expansion_unit_min_money
                    and assets.get("barracks", 0) > 0
                    and not tech_ramp_restriction
                    and not is_pending(pending_until_by_key, "Script.QueueInfantryMix.Expansion", now)
                )
                if should_queue_expansion_infantry:
                    infantry_added, infantry_code, infantry_reason = queue_infantry_mix(
                        client,
                        args.timeout_ms,
                        cycle,
                        soldiers_count=1,
                        rpg_count=2,
                    )
                    print(
                        f"[loop {cycle}] phase=expansion econ=infantry money={money} "
                        f"anchor={active_zone_label} added={infantry_added} "
                        f"last_code={infantry_code} last_reason={infantry_reason}",
                        flush=True,
                    )
                    if infantry_added > 0:
                        mark_pending(
                            pending_until_by_key,
                            "Script.QueueInfantryMix.Expansion",
                            now,
                            max(8.0, float(args.pending_cooldown_sec)),
                        )

                vehicle_every = max(0, int(args.expansion_vehicle_every))
                should_queue_expansion_vehicle = (
                    vehicle_every > 0
                    and ((expansion_cycle_count + 1) % vehicle_every == 0)
                    and money >= expansion_unit_min_money
                    and assets.get("arms", 0) > 0
                    and not tech_ramp_restriction
                    and not is_pending(pending_until_by_key, "Script.QueueVehicleMix.Expansion", now)
                )
                if should_queue_expansion_vehicle:
                    vehicle_added, vehicle_code, vehicle_reason = queue_vehicle_mix(
                        client,
                        args.timeout_ms,
                        cycle,
                        radar_count=0,
                        quads_count=2,
                        scorpions_count=1,
                    )
                    print(
                        f"[loop {cycle}] phase=expansion econ=vehicle money={money} "
                        f"anchor={active_zone_label} added={vehicle_added} "
                        f"last_code={vehicle_code} last_reason={vehicle_reason}",
                        flush=True,
                    )
                    if vehicle_added > 0:
                        mark_pending(
                            pending_until_by_key,
                            "Script.QueueVehicleMix.Expansion",
                            now,
                            max(10.0, float(args.pending_cooldown_sec)),
                        )

                expansion_step = with_zone_args(PHASE_EXPANSION.steps[expansion_step_index], active_zone_center, float(args.zone_radius))
                if is_pending(pending_until_by_key, expansion_step.cmd, now):
                    cycle += 1
                    time.sleep(args.tick_sec)
                    continue
                if cached_idle_workers <= 0:
                    print(
                        f"[loop {cycle}] phase=expansion step={expansion_step_index + 1}/{len(PHASE_EXPANSION.steps)} "
                        f"name='{expansion_step.name}' skip=no_idle_workers idle_workers={cached_idle_workers}",
                        flush=True,
                    )
                    cycle += 1
                    time.sleep(args.tick_sec)
                    continue

                requested_stash_count = int(expansion_step.args.get("stash", 1))
                stash_count_for_cycle = requested_stash_count
                barracks_count_for_cycle = int(expansion_step.args.get("barracks", 1)) if (cycle % 20 == 0) else 0
                arms_count_for_cycle = int(expansion_step.args.get("arms", 1)) if (cycle % 20 == 0) else 0
                black_markets_count_for_cycle = int(expansion_step.args.get("black_markets", 0)) if money >= SUSTAIN_BLACK_MARKET_MIN_MONEY else 0
                tunnel_networks_count_for_cycle = int(expansion_step.args.get("tunnel_networks", 0))
                stinger_sites_count_for_cycle = int(expansion_step.args.get("stinger_sites", 0))
                defense_needs_workers = defense_mix_needs_workers(
                    tunnel_networks_count=tunnel_networks_count_for_cycle,
                    stinger_sites_count=stinger_sites_count_for_cycle,
                )
                force_defense_first = defense_needs_workers and (
                    cached_idle_workers < max(0, int(args.defense_worker_min_idle))
                    or defense_worker_pressure_cycles > 0
                )
                if market_bootstrap_priority:
                    requested_stash_count = 0
                    stash_count_for_cycle = 0
                    barracks_count_for_cycle = 0
                    arms_count_for_cycle = 0
                    tunnel_networks_count_for_cycle = 0
                    stinger_sites_count_for_cycle = 0
                    black_markets_count_for_cycle = max(1, black_markets_count_for_cycle)
                    print(
                        f"[loop {cycle}] phase=expansion econ=market_bootstrap "
                        f"bm={black_markets} target={market_bootstrap_target} money={money} "
                        f"net_income_per_sec={effective_net_income_per_sec:.2f}",
                        flush=True,
                    )
                elif tech_ramp_restriction:
                    requested_stash_count = 0
                    stash_count_for_cycle = 0
                    barracks_count_for_cycle = 0
                    arms_count_for_cycle = 0
                    if force_defense_first:
                        tunnel_networks_count_for_cycle = min(1, tunnel_networks_count_for_cycle)
                        stinger_sites_count_for_cycle = 0
                    else:
                        tunnel_networks_count_for_cycle = 0
                        stinger_sites_count_for_cycle = 0
                    print(
                        f"[loop {cycle}] phase=expansion econ=tech_ramp_lock "
                        f"palaces={palaces} completed_palaces={completed_palaces} "
                        f"bm={black_markets} market_floor={prereq_market_floor} "
                        f"force_defense_first={force_defense_first} money={money}",
                        flush=True,
                    )
                if requested_stash_count > 0:
                    if money < SUSTAIN_STASH_MIN_MONEY:
                        stash_count_for_cycle = 0
                        print(
                            f"[loop {cycle}] phase=expansion step={expansion_step_index + 1}/{len(PHASE_EXPANSION.steps)} "
                            f"name='{expansion_step.name}' stash_skip=low_cash money={money} required={SUSTAIN_STASH_MIN_MONEY}",
                            flush=True,
                        )
                    elif cycle % max(1, SUSTAIN_STASH_EVERY_CYCLES) != 0:
                        stash_count_for_cycle = 0
                        print(
                            f"[loop {cycle}] phase=expansion step={expansion_step_index + 1}/{len(PHASE_EXPANSION.steps)} "
                            f"name='{expansion_step.name}' stash_skip=cadence every={SUSTAIN_STASH_EVERY_CYCLES}",
                            flush=True,
                        )
                idle_worker_reserve = int(args.build_tick_idle_reserve)
                usable_idle_workers = max(0, cached_idle_workers - max(0, idle_worker_reserve))
                eco_request_count = (
                    stash_count_for_cycle
                    + barracks_count_for_cycle
                    + arms_count_for_cycle
                    + black_markets_count_for_cycle
                )
                defense_request_count = tunnel_networks_count_for_cycle + stinger_sites_count_for_cycle
                post_defense_delay_active = now < delay_eco_until
                eco_usable_budget = 0
                defense_usable_budget = 0
                if market_bootstrap_priority:
                    eco_usable_budget = usable_idle_workers
                    defense_usable_budget = 0
                elif tech_ramp_restriction:
                    eco_usable_budget = usable_idle_workers
                    defense_usable_budget = 0 if not force_defense_first else min(1, usable_idle_workers)
                elif defense_request_count > 0 and eco_request_count > 0:
                    eco_usable_budget = int(round(float(usable_idle_workers) * DEFAULT_EXPANSION_ECO_SHARE))
                    eco_usable_budget = max(1, eco_usable_budget)
                    if post_defense_delay_active or force_defense_first:
                        eco_usable_budget = min(eco_usable_budget, max(1, usable_idle_workers // 3))
                    eco_usable_budget = min(usable_idle_workers, eco_usable_budget)
                    defense_usable_budget = max(0, usable_idle_workers - eco_usable_budget)
                elif defense_request_count > 0:
                    defense_usable_budget = usable_idle_workers
                else:
                    eco_usable_budget = usable_idle_workers

                if defense_request_count > 0 or eco_request_count > 0:
                    print(
                        f"[loop {cycle}] phase=expansion build_split "
                        f"usable_idle_workers={usable_idle_workers} reserve={max(0, idle_worker_reserve)} "
                        f"eco_budget={eco_usable_budget} defense_budget={defense_usable_budget} "
                        f"eco_requests={eco_request_count} defense_requests={defense_request_count} "
                        f"pressure={force_defense_first} post_defense_delay={post_defense_delay_active}",
                        flush=True,
                    )
                expansion_defense_added, expansion_defense_code, expansion_defense_reason = queue_building_mix(
                    client,
                    args.timeout_ms,
                    cycle,
                    stash_count=0,
                    barracks_count=0,
                    arms_count=0,
                    black_markets_count=0,
                    tunnel_networks_count=tunnel_networks_count_for_cycle,
                    stinger_sites_count=stinger_sites_count_for_cycle,
                    available_idle_workers=defense_usable_budget + max(0, idle_worker_reserve),
                    idle_worker_reserve=idle_worker_reserve,
                    zone_center=active_zone_center,
                    zone_radius=float(args.zone_radius),
                    buildings=buildings,
                    stash_after_primary=False,
                )
                if defense_needs_workers:
                    if expansion_defense_added > 0:
                        defense_worker_failure_cycles = 0
                    elif expansion_defense_reason == "idle_worker_not_found":
                        defense_worker_failure_cycles += 1
                    else:
                        defense_worker_failure_cycles = max(0, defense_worker_failure_cycles - 1)
                else:
                    defense_worker_failure_cycles = 0
                if expansion_defense_added > 0:
                    delay_eco_until = max(delay_eco_until, now + max(0.0, float(args.eco_build_delay_after_defense_sec)))
                eco_gate_reason: str | None = None
                if eco_request_count <= 0:
                    eco_gate_reason = "no_eco_requests"
                elif eco_usable_budget <= 0:
                    eco_gate_reason = "eco_budget_exhausted"
                if eco_gate_reason is not None:
                    remaining_delay = max(0.0, delay_eco_until - now)
                    print(
                        f"[loop {cycle}] phase=expansion econ=eco_hold "
                        f"reason={eco_gate_reason} "
                        f"idle_workers={cached_idle_workers} remaining_sec={remaining_delay:.1f}",
                        flush=True,
                    )
                    expansion_eco_added, expansion_eco_code, expansion_eco_reason = 0, None, eco_gate_reason
                else:
                    expansion_eco_added, expansion_eco_code, expansion_eco_reason = queue_building_mix(
                        client,
                        args.timeout_ms,
                        cycle,
                        stash_count=stash_count_for_cycle,
                        barracks_count=barracks_count_for_cycle,
                        arms_count=arms_count_for_cycle,
                        black_markets_count=black_markets_count_for_cycle,
                        tunnel_networks_count=0,
                        stinger_sites_count=0,
                        available_idle_workers=eco_usable_budget + max(0, idle_worker_reserve),
                        idle_worker_reserve=idle_worker_reserve,
                        zone_center=eco_zone_center,
                        zone_radius=float(args.zone_radius),
                        buildings=buildings,
                        stash_after_primary=False,
                    )
                expansion_added = expansion_eco_added + expansion_defense_added
                expansion_code = expansion_defense_code if expansion_defense_added > 0 else expansion_eco_code
                expansion_reason = expansion_defense_reason if expansion_defense_reason is not None else expansion_eco_reason
                print(
                    f"[loop {cycle}] phase=expansion step={expansion_step_index + 1}/{len(PHASE_EXPANSION.steps)} "
                    f"name='{expansion_step.name}' added={expansion_added} money={money} "
                    f"bm={black_markets} palaces={palaces} eco_anchor={eco_zone_label} defense_anchor={active_zone_label} "
                    f"last_code={expansion_code} last_reason={expansion_reason}",
                    flush=True,
                )
                if expansion_added > 0:
                    mark_pending(pending_until_by_key, expansion_step.cmd, now, float(args.pending_cooldown_sec))
                expansion_step_index = (expansion_step_index + 1) % len(PHASE_EXPANSION.steps)
                expansion_cycle_count += 1
                if grid_targets:
                    grid_index = (grid_index + 1) % len(grid_targets)
                    active_grid_label = str(grid_targets[grid_index].get("cell", ""))
                    next_zone = grid_targets[grid_index]["center"]
                    print(
                        f"[grid] advance -> cell={active_grid_label} center=({next_zone[0]:.1f},{next_zone[1]:.1f})",
                        flush=True,
                    )
                else:
                    zone_index = (zone_index + 1) % len(zone_centers)
                    next_zone = zone_centers[zone_index]
                    print(f"[zone] template advance -> zone_index={zone_index} center=({next_zone[0]:.1f},{next_zone[1]:.1f})", flush=True)
                cycle += 1
                last_attempt_at = now
                time.sleep(args.tick_sec)
                continue

            if current_phase.name == PHASE_WARMONGER_NAME:
                warmonger_step = PHASE_WARMONGER.steps[warmonger_step_index]
                if is_pending(pending_until_by_key, warmonger_step.cmd, now):
                    warmonger_step_index = (warmonger_step_index + 1) % len(PHASE_WARMONGER.steps)
                    cycle += 1
                    time.sleep(args.tick_sec)
                    continue
                reserve_cash = max(0, int(args.reserve_cash))
                if warmonger_step.cmd == "Script.QueueInfantryMix":
                    required = reserve_cash + BUDGET_INFANTRY_MIX_MIN
                    if money < required:
                        print(
                            f"[loop {cycle}] phase=warmonger step={warmonger_step_index + 1}/{len(PHASE_WARMONGER.steps)} "
                            f"name='{warmonger_step.name}' skip=low_cash money={money} required={required}",
                            flush=True,
                        )
                        warmonger_step_index = (warmonger_step_index + 1) % len(PHASE_WARMONGER.steps)
                        cycle += 1
                        time.sleep(args.tick_sec)
                        continue
                    warmonger_added, warmonger_code, warmonger_reason = queue_infantry_mix(
                        client,
                        args.timeout_ms,
                        cycle,
                        soldiers_count=int(warmonger_step.args.get("soldiers", 2)),
                        rpg_count=int(warmonger_step.args.get("rpg", 2)),
                    )
                else:
                    required = reserve_cash + BUDGET_VEHICLE_MIX_MIN
                    if money < required:
                        print(
                            f"[loop {cycle}] phase=warmonger step={warmonger_step_index + 1}/{len(PHASE_WARMONGER.steps)} "
                            f"name='{warmonger_step.name}' skip=low_cash money={money} required={required}",
                            flush=True,
                        )
                        warmonger_step_index = (warmonger_step_index + 1) % len(PHASE_WARMONGER.steps)
                        cycle += 1
                        time.sleep(args.tick_sec)
                        continue
                    warmonger_added, warmonger_code, warmonger_reason = queue_vehicle_mix(
                        client,
                        args.timeout_ms,
                        cycle,
                        radar_count=int(warmonger_step.args.get("radar", 1)),
                        quads_count=int(warmonger_step.args.get("quads", 3)),
                        scorpions_count=int(warmonger_step.args.get("scorpions", 3)),
                    )
                print(
                    f"[loop {cycle}] phase=warmonger step={warmonger_step_index + 1}/{len(PHASE_WARMONGER.steps)} "
                    f"name='{warmonger_step.name}' added={warmonger_added} money={money} "
                    f"bm={black_markets} palaces={palaces} last_code={warmonger_code} last_reason={warmonger_reason}",
                    flush=True,
                )
                if warmonger_added > 0:
                    mark_pending(pending_until_by_key, warmonger_step.cmd, now, float(args.pending_cooldown_sec))
                warmonger_step_index = (warmonger_step_index + 1) % len(PHASE_WARMONGER.steps)
                cycle += 1
                last_attempt_at = now
                time.sleep(args.tick_sec)
                continue

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
                        with_zone_args(
                            Step("Build palace priority", "Game.BuildPalaceSmart", {}),
                            zone_centers[0],
                            float(args.zone_radius),
                        ),
                        args.timeout_ms,
                    )
                    if palace_added > 0:
                        last_palace_success_at = now
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
                            completed_palaces=count_complete_matching_templates(buildings, ("palace",)),
                            opening_core_ready=opening_building_mix_ready(buildings),
                            money=money,
                            now_monotonic=now,
                            phase_entered_at_monotonic=phase_entered_at_monotonic,
                            cycle=cycle,
                            completed_black_markets=count_complete_matching_templates(buildings, ("blackmarket", "black_market")),
                            net_income_per_sec=effective_net_income_per_sec,
                            eta_to_warmonger_sec=eta_to_warmonger_sec,
                        )
                        if PLAN_PHASES[phase_index].name == PHASE_EXPANSION_NAME:
                            expansion_step_index = 0
                        elif PLAN_PHASES[phase_index].name == PHASE_SUSTAIN_NAME:
                            sustain_step_index = 0
                        elif PLAN_PHASES[phase_index].name == PHASE_WARMONGER_NAME:
                            warmonger_step_index = 0
                        print(f"[phase] palace_priority added={palace_added} code={palace_code} reason={palace_reason}", flush=True)
                        last_attempt_at = now
                        cycle += 1
                        time.sleep(args.tick_sec)
                        continue

                current = PHASE_OPENING.steps[opening_step_index]
                if USE_ADAPTER_WORKER_RULE and current.cmd == "Game.BuildWorker":
                    opening_step_index += 1
                    opening_step_attempt_count = 0
                    print(
                        f"[loop {cycle}] phase=open step={opening_step_index}/{len(PHASE_OPENING.steps)} "
                        "skip=adapter_worker_rule",
                        flush=True,
                    )
                    cycle += 1
                    time.sleep(args.tick_sec)
                    continue
                if current.cmd == "Game.BuildWorker":
                    opening_step_attempt_count = max(
                        opening_step_attempt_count,
                        min(int(cached_unit_counts.get("workers", 0)), current.repeat),
                    )
                    if opening_step_attempt_count >= current.repeat:
                        opening_step_index += 1
                        opening_step_attempt_count = 0
                        cycle += 1
                        time.sleep(args.tick_sec)
                        continue
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
                            stash_supply_source_ids=opening_supply_source_ids if current_phase.name == PHASE_OPENING_NAME else None,
                            stash_supply_source_positions=startup_supply_source_positions,
                            claimed_supply_source_ids=opening_claimed_supply_source_ids if current_phase.name == PHASE_OPENING_NAME else None,
                            available_idle_workers=cached_idle_workers,
                            idle_worker_reserve=0 if current_phase.name == PHASE_OPENING_NAME else int(args.build_tick_idle_reserve),
                            buildings=buildings,
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
                    f"progress={opening_step_attempt_count if current.cmd == 'Game.BuildWorker' else opening_step_attempt_count}/{current.repeat} "
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
                elif current.cmd == "Game.BuildWorker":
                    current_workers = int(cached_unit_counts.get("workers", 0))
                    if current_workers >= current.repeat:
                        opening_step_index += 1
                        opening_step_attempt_count = 0
                    else:
                        opening_step_attempt_count = max(
                            opening_step_attempt_count,
                            min(current_workers, current.repeat),
                        )
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
                    completed_palaces=count_complete_matching_templates(cached_buildings, ("palace",)),
                    opening_core_ready=opening_building_mix_ready(cached_buildings),
                    money=money,
                    now_monotonic=now,
                    phase_entered_at_monotonic=phase_entered_at_monotonic,
                    cycle=cycle,
                    completed_black_markets=count_complete_matching_templates(cached_buildings, ("blackmarket", "black_market")),
                    net_income_per_sec=effective_net_income_per_sec,
                    eta_to_warmonger_sec=eta_to_warmonger_sec,
                )
                if PLAN_PHASES[phase_index].name == PHASE_EXPANSION_NAME:
                    expansion_step_index = 0
                elif PLAN_PHASES[phase_index].name == PHASE_SUSTAIN_NAME:
                    sustain_step_index = 0
                elif PLAN_PHASES[phase_index].name == PHASE_WARMONGER_NAME:
                    warmonger_step_index = 0

                cycle += 1
                time.sleep(args.tick_sec)
                continue

            # Phase 3 (vesting): low-spend consolidation with measured cash growth.
            low_money_threshold = max(0, int(args.low_money_threshold))
            low_money_skip_cycles = max(1, int(args.low_money_skip_cycles))
            if money < low_money_threshold:
                low_money_skip_until_cycle = max(low_money_skip_until_cycle, cycle + low_money_skip_cycles)
            in_low_money_hold = (money < low_money_threshold) and (cycle < low_money_skip_until_cycle)

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
                            eco_zone_center,
                            float(args.zone_radius),
                        ),
                        args.timeout_ms,
                    )
                    print(
                        f"[loop {cycle}] phase=vesting econ=black_market "
                        f"bm={black_markets} palaces={palaces} completed_palaces={completed_palaces} "
                        f"target_bm={desired_markets_for_next_palace} anchor={eco_zone_label} "
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
                    f"[loop {cycle}] phase=vesting econ=black_market skip=no_completed_palace "
                    f"palaces={palaces} completed_palaces={completed_palaces}",
                    flush=True,
                )
            elif (cycle % SUSTAIN_MARKET_ATTEMPT_EVERY == 0) and money < SUSTAIN_BLACK_MARKET_MIN_MONEY:
                print(
                    f"[loop {cycle}] phase=vesting econ=black_market skip=low_cash "
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
                    active_zone_center,
                    float(args.zone_radius),
                    ("palace",),
                )
                if (palace_complete + palace_uc) > 0:
                    print(
                        f"[loop {cycle}] phase=vesting econ=palace skip=zone_has_palace "
                        f"anchor={active_zone_label} complete={palace_complete} under_construction={palace_uc}",
                        flush=True,
                    )
                elif not is_pending(pending_until_by_key, "Game.BuildPalaceSmart", now):
                    palace_added, palace_code, palace_reason = try_burst_step(
                        client,
                        with_zone_args(
                            Step("Build palace sustain", "Game.BuildPalaceSmart", {}),
                            eco_zone_center,
                            float(args.zone_radius),
                        ),
                        args.timeout_ms,
                    )
                    print(
                        f"[loop {cycle}] phase=vesting econ=palace "
                        f"money={money} bm={black_markets} palaces={palaces} "
                        f"anchor={eco_zone_label} added={palace_added} "
                        f"last_code={palace_code} last_reason={palace_reason}",
                        flush=True,
                    )
                    if palace_added > 0:
                        last_palace_success_at = now
                        mark_pending(
                            pending_until_by_key,
                            "Game.BuildPalaceSmart",
                            now,
                            float(args.pending_cooldown_sec),
                        )

            sustain_step = with_zone_args(PHASE_SUSTAIN.steps[sustain_step_index], active_zone_center, float(args.zone_radius))
            singleton_needles = ZONE_SINGLETON_BUILD_RULES.get(sustain_step.cmd)
            if singleton_needles is not None:
                singleton_complete, singleton_uc = zone_building_state_counts(
                    buildings,
                    active_zone_center,
                    float(args.zone_radius),
                    singleton_needles,
                )
                if (singleton_complete + singleton_uc) > 0:
                    print(
                        f"[loop {cycle}] phase=vesting step={sustain_step_index + 1}/{len(PHASE_SUSTAIN.steps)} "
                        f"name='{sustain_step.name}' skip=zone_singleton_present "
                        f"anchor={active_zone_label} complete={singleton_complete} under_construction={singleton_uc}",
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
                    f"[loop {cycle}] phase=vesting step={sustain_step_index + 1}/{len(PHASE_SUSTAIN.steps)} "
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
                        f"[loop {cycle}] phase=vesting step={sustain_step_index + 1}/{len(PHASE_SUSTAIN.steps)} "
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
                        f"[loop {cycle}] phase=vesting step={sustain_step_index + 1}/{len(PHASE_SUSTAIN.steps)} "
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
                        f"[loop {cycle}] phase=vesting step={sustain_step_index + 1}/{len(PHASE_SUSTAIN.steps)} "
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
                        f"[loop {cycle}] phase=vesting step={sustain_step_index + 1}/{len(PHASE_SUSTAIN.steps)} "
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
                defense_needs_workers = defense_mix_needs_workers(
                    tunnel_networks_count=tunnel_networks_count_for_cycle,
                    stinger_sites_count=stinger_sites_count_for_cycle,
                )
                if in_low_money_hold:
                    barracks_count_for_cycle = 0
                    arms_count_for_cycle = 0
                if money < SUSTAIN_BLACK_MARKET_MIN_MONEY:
                    black_markets_count_for_cycle = 0
                if requested_stash_count > 0:
                    if money < SUSTAIN_STASH_MIN_MONEY:
                        stash_count_for_cycle = 0
                        print(
                            f"[loop {cycle}] phase=vesting step={sustain_step_index + 1}/{len(PHASE_SUSTAIN.steps)} "
                            f"name='{sustain_step.name}' stash_skip=low_cash money={money} required={SUSTAIN_STASH_MIN_MONEY}",
                            flush=True,
                        )
                    elif cycle % max(1, SUSTAIN_STASH_EVERY_CYCLES) != 0:
                        stash_count_for_cycle = 0
                        print(
                            f"[loop {cycle}] phase=vesting step={sustain_step_index + 1}/{len(PHASE_SUSTAIN.steps)} "
                            f"name='{sustain_step.name}' stash_skip=cadence every={SUSTAIN_STASH_EVERY_CYCLES}",
                            flush=True,
                        )
                sustain_eco_added, sustain_eco_code, sustain_eco_reason = queue_building_mix(
                    client,
                    args.timeout_ms,
                    cycle,
                    stash_count=stash_count_for_cycle,
                    barracks_count=barracks_count_for_cycle,
                    arms_count=arms_count_for_cycle,
                    black_markets_count=black_markets_count_for_cycle,
                    tunnel_networks_count=0,
                    stinger_sites_count=0,
                    available_idle_workers=cached_idle_workers,
                    idle_worker_reserve=int(args.build_tick_idle_reserve),
                    zone_center=eco_zone_center,
                    zone_radius=float(args.zone_radius),
                    buildings=buildings,
                    stash_after_primary=True,
                )
                sustain_defense_added, sustain_defense_code, sustain_defense_reason = queue_building_mix(
                    client,
                    args.timeout_ms,
                    cycle,
                    stash_count=0,
                    barracks_count=0,
                    arms_count=0,
                    black_markets_count=0,
                    tunnel_networks_count=tunnel_networks_count_for_cycle,
                    stinger_sites_count=stinger_sites_count_for_cycle,
                    available_idle_workers=max(0, cached_idle_workers - sustain_eco_added),
                    idle_worker_reserve=int(args.build_tick_idle_reserve),
                    zone_center=active_zone_center,
                    zone_radius=float(args.zone_radius),
                    buildings=buildings,
                    stash_after_primary=True,
                )
                if defense_needs_workers:
                    if sustain_defense_added > 0:
                        defense_worker_failure_cycles = 0
                    elif sustain_defense_reason == "idle_worker_not_found":
                        defense_worker_failure_cycles += 1
                    else:
                        defense_worker_failure_cycles = max(0, defense_worker_failure_cycles - 1)
                else:
                    defense_worker_failure_cycles = 0
                sustain_added = sustain_eco_added + sustain_defense_added
                sustain_code = sustain_defense_code if sustain_defense_added > 0 else sustain_eco_code
                sustain_reason = sustain_defense_reason if sustain_defense_reason is not None else sustain_eco_reason
            else:
                sustain_added, sustain_code, sustain_reason = try_burst_step(client, sustain_step, args.timeout_ms)
            print(
                f"[loop {cycle}] phase=vesting step={sustain_step_index + 1}/{len(PHASE_SUSTAIN.steps)} "
                f"name='{sustain_step.name}' added={sustain_added} "
                f"money={money} bm={black_markets} palaces={palaces} "
                f"eco_anchor={eco_zone_label} defense_anchor={active_zone_label} "
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
                if grid_targets:
                    grid_index = (grid_index + 1) % len(grid_targets)
                    active_grid_label = str(grid_targets[grid_index].get("cell", ""))
                    next_zone = grid_targets[grid_index]["center"]
                    print(
                        f"[grid] advance -> cell={active_grid_label} center=({next_zone[0]:.1f},{next_zone[1]:.1f})",
                        flush=True,
                    )
                else:
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
