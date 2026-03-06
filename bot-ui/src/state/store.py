from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


def _as_float(value: Any, fallback: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return fallback


def _extract_xy(obj: dict[str, Any]) -> tuple[float, float]:
    if "x" in obj and "y" in obj:
        return _as_float(obj.get("x")), _as_float(obj.get("y"))
    for key in ("position", "pos", "location", "coord", "coords", "map_position"):
        pos = obj.get(key)
        if isinstance(pos, dict):
            return _as_float(pos.get("x")), _as_float(pos.get("y"))
    return 0.0, 0.0


def _extract_id(obj: dict[str, Any]) -> int:
    for key in ("id", "object_id"):
        if key in obj:
            try:
                return int(obj[key])
            except (TypeError, ValueError):
                continue
    return -1


def _to_color_name(value: Any) -> str:
    if value is None:
        return "-"
    raw = str(value).strip()
    if not raw:
        return "-"

    lower = raw.lower()
    if lower in {
        "red",
        "blue",
        "green",
        "yellow",
        "orange",
        "purple",
        "pink",
        "cyan",
        "teal",
        "white",
        "black",
        "gray",
        "grey",
        "-",
    }:
        return raw

    hex_text = raw.lstrip("#")
    if len(hex_text) == 8:
        hex_text = hex_text[2:]  # drop alpha from AARRGGBB
    if len(hex_text) != 6:
        return raw
    try:
        r = int(hex_text[0:2], 16)
        g = int(hex_text[2:4], 16)
        b = int(hex_text[4:6], 16)
    except ValueError:
        return raw

    palette: list[tuple[str, tuple[int, int, int]]] = [
        ("Red", (255, 60, 60)),
        ("Blue", (70, 130, 255)),
        ("Green", (70, 200, 90)),
        ("Yellow", (245, 220, 70)),
        ("Orange", (245, 150, 60)),
        ("Purple", (170, 90, 220)),
        ("Pink", (230, 120, 180)),
        ("Cyan", (70, 210, 220)),
        ("Teal", (45, 150, 150)),
        ("White", (240, 240, 240)),
        ("Black", (30, 30, 30)),
        ("Gray", (130, 130, 130)),
    ]
    best_name = "Color"
    best_dist = 10**12
    for name, (pr, pg, pb) in palette:
        dr = r - pr
        dg = g - pg
        db = b - pb
        dist = (dr * dr) + (dg * dg) + (db * db)
        if dist < best_dist:
            best_dist = dist
            best_name = name
    return best_name


@dataclass
class WorldObject:
    object_id: int
    template: str
    obj_class: str
    owner_player_index: int | None
    x: float
    y: float
    hp_cur: float | None = None
    hp_max: float | None = None
    kind: str = "unit"


@dataclass
class PlayerMeta:
    player_index: int
    name: str = "-"
    color: str = "-"
    team: str = "-"
    cash: int | None = None
    unit_count: int = 0
    building_count: int = 0
    promotions: str = "-"
    science_points: int | None = None
    map_position: tuple[float, float] | None = None
    status: str = "unknown"


@dataclass
class UIStore:
    owned_objects: dict[int, WorldObject] = field(default_factory=dict)
    visible_enemies: dict[int, WorldObject] = field(default_factory=dict)
    all_objects: dict[int, WorldObject] = field(default_factory=dict)
    interesting_points: list[tuple[float, float]] = field(default_factory=list)
    players: dict[int, PlayerMeta] = field(default_factory=dict)
    session_state: dict[str, Any] = field(default_factory=dict)
    map_width: float = 1024.0
    map_height: float = 1024.0

    def update_owned_objects(self, payload: Any) -> None:
        objects: list[Any] = []
        if isinstance(payload, dict):
            units = self._extract_list(payload, "units") or []
            buildings = self._extract_list(payload, "buildings") or []
            objects_field = self._extract_list(payload, "objects") or []
            owned_field = self._extract_list(payload, "owned_objects") or []
            items_field = self._extract_list(payload, "items") or []
            # Adapter shape is typically units + buildings; include all known lists.
            objects.extend(units)
            objects.extend(buildings)
            objects.extend(objects_field)
            objects.extend(owned_field)
            objects.extend(items_field)
        elif isinstance(payload, list):
            objects = payload

        owner_from_payload: int | None = None
        if isinstance(payload, dict) and isinstance(payload.get("player_index"), int):
            owner_from_payload = int(payload["player_index"])
        updated: dict[int, WorldObject] = {}
        for raw in objects:
            if not isinstance(raw, dict):
                continue
            object_id = _extract_id(raw)
            if object_id < 0:
                continue
            x, y = _extract_xy(raw)
            obj_class = str(raw.get("class", ""))
            template = str(raw.get("template", raw.get("template_name", "")))
            under_construction = bool(raw.get("under_construction", False))
            lower_class = obj_class.lower()
            kind = "building" if (under_construction or "structure" in lower_class or "building" in lower_class) else "unit"
            owner = raw.get("owner_player_index", raw.get("player_index"))
            owner_idx = int(owner) if isinstance(owner, int) else owner_from_payload
            hp_cur = raw.get("hp_cur", raw.get("hp"))
            hp_max = raw.get("hp_max")
            updated[object_id] = WorldObject(
                object_id=object_id,
                template=template,
                obj_class=obj_class,
                owner_player_index=owner_idx,
                x=x,
                y=y,
                hp_cur=float(hp_cur) if isinstance(hp_cur, (int, float)) else None,
                hp_max=float(hp_max) if isinstance(hp_max, (int, float)) else None,
                kind=kind,
            )
        self.owned_objects = updated
        self._recompute_player_aggregates()

    def update_visible_enemies(self, payload: Any) -> None:
        enemies = self._extract_list(payload, "visible_enemies")
        if enemies is None:
            enemies = self._extract_list(payload, "enemies")
        if enemies is None:
            enemies = self._extract_list(payload, "enemy_objects")
        if enemies is None:
            enemies = payload if isinstance(payload, list) else []
        updated: dict[int, WorldObject] = {}
        for raw in enemies:
            if not isinstance(raw, dict):
                continue
            object_id = _extract_id(raw)
            if object_id < 0:
                continue
            x, y = _extract_xy(raw)
            obj_class = str(raw.get("class", ""))
            template = str(raw.get("template", raw.get("template_name", "")))
            lower_class = obj_class.lower()
            under_construction = bool(raw.get("under_construction", False))
            kind = "building" if (under_construction or "structure" in lower_class or "building" in lower_class) else "enemy"
            updated[object_id] = WorldObject(
                object_id=object_id,
                template=template,
                obj_class=obj_class,
                owner_player_index=int(raw["player_index"]) if isinstance(raw.get("player_index"), int) else None,
                x=x,
                y=y,
                kind=kind,
            )
        self.visible_enemies = updated

    def update_objects_all(self, payload: Any) -> None:
        if not isinstance(payload, dict):
            return
        players = payload.get("players")
        if not isinstance(players, list):
            return
        updated: dict[int, WorldObject] = {}
        for row in players:
            if not isinstance(row, dict):
                continue
            pidx = row.get("player_index")
            if not isinstance(pidx, int):
                continue
            units = row.get("units") if isinstance(row.get("units"), list) else []
            buildings = row.get("buildings") if isinstance(row.get("buildings"), list) else []
            for raw in units:
                if not isinstance(raw, dict):
                    continue
                object_id = _extract_id(raw)
                if object_id < 0:
                    continue
                x, y = _extract_xy(raw)
                obj_class = str(raw.get("class", ""))
                template = str(raw.get("template", raw.get("template_name", "")))
                hp_cur = raw.get("hp_cur", raw.get("hp"))
                hp_max = raw.get("hp_max")
                key = self._reserve_object_key(updated, pidx, object_id)
                updated[key] = WorldObject(
                    object_id=object_id,
                    template=template,
                    obj_class=obj_class,
                    owner_player_index=pidx,
                    x=x,
                    y=y,
                    hp_cur=float(hp_cur) if isinstance(hp_cur, (int, float)) else None,
                    hp_max=float(hp_max) if isinstance(hp_max, (int, float)) else None,
                    kind="unit",
                )
            for raw in buildings:
                if not isinstance(raw, dict):
                    continue
                object_id = _extract_id(raw)
                if object_id < 0:
                    continue
                x, y = _extract_xy(raw)
                obj_class = str(raw.get("class", ""))
                template = str(raw.get("template", raw.get("template_name", "")))
                hp_cur = raw.get("hp_cur", raw.get("hp"))
                hp_max = raw.get("hp_max")
                key = self._reserve_object_key(updated, pidx, object_id)
                updated[key] = WorldObject(
                    object_id=object_id,
                    template=template,
                    obj_class=obj_class,
                    owner_player_index=pidx,
                    x=x,
                    y=y,
                    hp_cur=float(hp_cur) if isinstance(hp_cur, (int, float)) else None,
                    hp_max=float(hp_max) if isinstance(hp_max, (int, float)) else None,
                    kind="building",
                )
        self.all_objects = updated
        self._recompute_player_aggregates()

    def update_players(self, payload: Any) -> None:
        players = self._extract_list(payload, "players")
        if players is None and isinstance(payload, list):
            players = payload
        if players is None:
            return
        updated_players: dict[int, PlayerMeta] = {}
        for raw in players:
            if not isinstance(raw, dict):
                continue
            # Adapter game.players rows are nested: { player:{...}, resources:{...}, units:{...}, ... }
            player_node = raw.get("player") if isinstance(raw.get("player"), dict) else raw
            resources_node = raw.get("resources") if isinstance(raw.get("resources"), dict) else raw
            units_node = raw.get("units") if isinstance(raw.get("units"), dict) else raw

            pidx = player_node.get("player_index", player_node.get("index", player_node.get("slot")))
            if not isinstance(pidx, int) or pidx < 0:
                continue
            meta = self.players.get(pidx, PlayerMeta(player_index=pidx))
            # Name fallback from key if human-readable name missing.
            name = player_node.get("name", player_node.get("player_name_key", meta.name))
            name_str = str(name)
            lower_name = name_str.lower()
            if "civilian" in lower_name or "observer" in lower_name:
                continue
            meta.name = name_str
            side = str(player_node.get("side", player_node.get("template_side", player_node.get("base_side", ""))))
            # Adapter may omit explicit color/team; use stable fallbacks.
            color_value = player_node.get("color_hex", player_node.get("color", player_node.get("color_name")))
            if color_value is None:
                lower_side = side.lower()
                if "china" in lower_side:
                    meta.color = "red"
                elif "america" in lower_side or "usa" in lower_side:
                    meta.color = "blue"
                elif "gla" in lower_side:
                    meta.color = "green"
                else:
                    meta.color = "-"
            else:
                meta.color = _to_color_name(color_value)

            team_val = player_node.get("team", player_node.get("team_id"))
            if team_val is None:
                # If team is unavailable, show slot/index-derived team label.
                meta.team = f"P{pidx}"
            else:
                meta.team = str(team_val)
            if isinstance(resources_node.get("money"), int):
                meta.cash = int(resources_node["money"])
            if isinstance(resources_node.get("cash"), int):
                meta.cash = int(resources_node["cash"])
            if isinstance(resources_node.get("science_points"), int):
                meta.science_points = int(resources_node["science_points"])
            if isinstance(resources_node.get("science_purchase_points"), int):
                meta.science_points = int(resources_node["science_purchase_points"])
            rank = resources_node.get("rank_level", player_node.get("rank_level", player_node.get("rank")))
            if rank is not None:
                meta.promotions = str(rank)
            # Prefer aggregate counts from query payload.
            if isinstance(units_node.get("units_total"), int):
                meta.unit_count = int(units_node["units_total"])
            if isinstance(units_node.get("buildings"), int):
                meta.building_count = int(units_node["buildings"])
            status = player_node.get("status", raw.get("status"))
            if status is not None:
                meta.status = str(status)
            x, y = _extract_xy(player_node)
            if x != 0.0 or y != 0.0:
                meta.map_position = (x, y)
            updated_players[pidx] = meta
        # Replace the player view with the latest snapshot to avoid stale rows.
        self.players = updated_players
        # Keep derived centroid/count fallback for cases without unit aggregates.
        self._recompute_player_aggregates()

    def update_resources(self, payload: Any, local_player_index: int = 0) -> None:
        resources = payload.get("resources") if isinstance(payload, dict) else None
        candidate = resources if isinstance(resources, dict) else payload
        if isinstance(candidate, dict):
            money = candidate.get("money")
            if isinstance(money, int):
                meta = self.players.get(local_player_index, PlayerMeta(player_index=local_player_index))
                meta.cash = money
                self.players[local_player_index] = meta

    def update_map_metadata(self, payload: Any) -> None:
        if not isinstance(payload, dict):
            return
        map_node = payload.get("map")
        candidate = map_node if isinstance(map_node, dict) else payload
        width = candidate.get("width")
        height = candidate.get("height")
        if isinstance(width, (int, float)) and width > 0:
            self.map_width = float(width)
        if isinstance(height, (int, float)) and height > 0:
            self.map_height = float(height)

    def _recompute_player_aggregates(self) -> None:
        source_objects = self.all_objects if self.all_objects else self.owned_objects
        for meta in self.players.values():
            xs: list[float] = []
            ys: list[float] = []
            derived_units = 0
            derived_buildings = 0
            for obj in source_objects.values():
                if obj.owner_player_index != meta.player_index:
                    continue
                if obj.kind == "building":
                    derived_buildings += 1
                else:
                    derived_units += 1
                xs.append(obj.x)
                ys.append(obj.y)
            # Only override table counts when we actually observed objects for this player.
            if derived_units > 0 or derived_buildings > 0:
                meta.unit_count = derived_units
                meta.building_count = derived_buildings
            if xs and ys:
                meta.map_position = (sum(xs) / len(xs), sum(ys) / len(ys))

    @staticmethod
    def _extract_list(payload: Any, key: str) -> list[Any] | None:
        if isinstance(payload, dict):
            value = payload.get(key)
            if isinstance(value, list):
                return value
        return None

    @staticmethod
    def _reserve_object_key(existing: dict[int, WorldObject], player_index: int, object_id: int) -> int:
        if object_id not in existing:
            return object_id
        # Some payloads can reuse object ids across players; keep all entries.
        key = (player_index * 10_000_000) + object_id
        while key in existing:
            key += 1
        return key
