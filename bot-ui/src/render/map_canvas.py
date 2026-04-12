from __future__ import annotations

import time
import tkinter as tk
from typing import Iterable

from state.store import AutonomyEventOverlay, AutonomyZoneOverlay, UIStore, WorldObject


class MapRenderer:
    def __init__(self, canvas: tk.Canvas) -> None:
        self.canvas = canvas
        self.padding = 16
        self.orientation = "flip_y"
        self.unit_radius = 1.5
        self.animation_duration_sec = 0.25
        self._unit_positions: dict[tuple[int | None, int], tuple[float, float]] = {}
        self._unit_animations: dict[
            tuple[int | None, int],
            tuple[float, tuple[float, float], tuple[float, float]],
        ] = {}

    def has_active_animation(self) -> bool:
        return bool(self._unit_animations)

    def draw(self, store: UIStore) -> None:
        width = max(self.canvas.winfo_width(), 1)
        height = max(self.canvas.winfo_height(), 1)
        self.canvas.delete("all")
        self.canvas.create_rectangle(0, 0, width, height, fill="#0c1318", outline="")
        all_objects = list(store.all_objects.values())
        owned = list(store.owned_objects.values())
        enemies = list(store.visible_enemies.values())
        points = list(store.interesting_points)
        supply_sources = list(store.supply_sources)
        telemetry_points = [(zone.center_x, zone.center_y) for zone in store.autonomy_zones]
        telemetry_points.extend((event.x, event.y) for event in store.autonomy_events if event.x is not None and event.y is not None)
        player_positions = [(meta.player_index, meta.map_position) for meta in store.players.values() if meta.map_position is not None]
        active_map: dict[tuple[int | None, int], WorldObject] = {}
        for obj in all_objects:
            active_map[(obj.owner_player_index, obj.object_id)] = obj
        for obj in owned:
            active_map[(obj.owner_player_index, obj.object_id)] = obj
        for obj in enemies:
            active_map[(obj.owner_player_index, obj.object_id)] = obj
        for obj in store.grid_objects.values():
            active_map[(obj.owner_player_index, obj.object_id)] = obj
        active_objects = list(active_map.values())
        bounds = self._resolve_bounds(store, active_objects, [], points + supply_sources + telemetry_points, player_positions)
        self._draw_map_bounds(width, height, bounds, store.map_width, store.map_height)
        self._draw_grid_overlay(store, width, height, bounds)
        self._draw_autonomy_zones(store.autonomy_zones, width, height, bounds)
        self._draw_objects(active_objects, width, height, bounds, store, friendly=True)
        self._draw_supply_sources(supply_sources, width, height, bounds)
        self._draw_interesting_points(points, width, height, bounds)
        self._draw_autonomy_events(store.autonomy_events, width, height, bounds)
        self._draw_player_positions(player_positions, width, height, bounds, store)
        self._draw_counts(active_objects, width)
        if not active_objects and not points and not supply_sources and not player_positions and not store.grid_cells and not store.autonomy_zones and not store.autonomy_events:
            self.canvas.create_text(
                width * 0.5,
                height * 0.5,
                text="No map objects received yet",
                fill="#7f96a1",
                font=("Segoe UI", 11),
            )

    def _draw_grid_overlay(
        self,
        store: UIStore,
        canvas_w: int,
        canvas_h: int,
        bounds: tuple[float, float, float, float],
    ) -> None:
        if store.grid_cols <= 0 or store.grid_rows <= 0:
            return

        for cell in store.grid_cells.values():
            x0, y0 = self._world_to_canvas(cell.min_x, cell.min_y, canvas_w, canvas_h, bounds)
            x1, y1 = self._world_to_canvas(cell.max_x, cell.max_y, canvas_w, canvas_h, bounds)
            left = min(x0, x1)
            right = max(x0, x1)
            top = min(y0, y1)
            bottom = max(y0, y1)

            outline = "#324752"
            fill = ""
            if cell.dominant_player_index is not None:
                player = store.players.get(cell.dominant_player_index)
                base = self._name_to_hex(player.color) if player is not None else None
                if base is not None:
                    outline = base
                    fill = self._darken_hex(base, 0.18)

            width = 1
            if store.selected_grid_cell and cell.cell == store.selected_grid_cell:
                outline = "#ffe57a"
                fill = self._darken_hex("#ffe57a", 0.35)
                width = 2

            self.canvas.create_rectangle(left, top, right, bottom, outline=outline, fill=fill, width=width)
            if cell.objects_total > 0 and (right - left) >= 18 and (bottom - top) >= 12:
                self.canvas.create_text(
                    left + 3,
                    top + 2,
                    text=cell.cell,
                    anchor="nw",
                    fill="#9db1bb",
                    font=("Segoe UI", 7),
                )

    def _draw_map_bounds(
        self,
        canvas_w: int,
        canvas_h: int,
        bounds: tuple[float, float, float, float],
        world_w: float,
        world_h: float,
    ) -> None:
        left = self.padding
        top = self.padding
        right = canvas_w - self.padding
        bottom = canvas_h - self.padding
        self.canvas.create_rectangle(left, top, right, bottom, outline="#3f5663", width=1)
        self.canvas.create_text(left + 6, top + 6, text="Map", anchor="nw", fill="#92a7b0", font=("Segoe UI", 9))
        min_x, min_y, max_x, max_y = bounds
        self.canvas.create_text(
            right - 6,
            top + 6,
            text=f"world:{int(world_w)}x{int(world_h)} view:{int(max_x - min_x)}x{int(max_y - min_y)}",
            anchor="ne",
            fill="#92a7b0",
            font=("Segoe UI", 9),
        )
        self.canvas.create_text(
            right - 6,
            top + 20,
            text=f"orient:{self.orientation}",
            anchor="ne",
            fill="#7f96a1",
            font=("Segoe UI", 8),
        )

    def _draw_objects(
        self,
        objects: Iterable[WorldObject],
        canvas_w: int,
        canvas_h: int,
        bounds: tuple[float, float, float, float],
        store: UIStore,
        friendly: bool,
    ) -> None:
        now = time.monotonic()
        all_objects = list(objects)
        buildings = [o for o in all_objects if o.kind == "building"]
        units = [o for o in all_objects if o.kind != "building"]

        for obj in buildings:
            cx, cy = self._world_to_canvas(obj.x, obj.y, canvas_w, canvas_h, bounds)
            outline, fill = self._resolve_object_colors(store, obj, friendly)
            self.canvas.create_rectangle(cx - 6, cy - 6, cx + 6, cy + 6, outline=outline, fill=fill)

        # Draw units last so they remain visible over structures.
        current_unit_positions: dict[tuple[int | None, int], tuple[float, float]] = {}
        for obj in units:
            key = (obj.owner_player_index, obj.object_id)
            current = (obj.x, obj.y)
            current_unit_positions[key] = current
            prev = self._unit_positions.get(key)
            if prev is None:
                continue
            dx = current[0] - prev[0]
            dy = current[1] - prev[1]
            if (dx * dx) + (dy * dy) > 0.0001:
                existing = self._unit_animations.get(key)
                if existing is None or existing[2] != current:
                    self._unit_animations[key] = (now, prev, current)

        for obj in units:
            key = (obj.owner_player_index, obj.object_id)
            world_x = obj.x
            world_y = obj.y
            anim = self._unit_animations.get(key)
            if anim is not None:
                start, src, dst = anim
                elapsed = now - start
                if elapsed >= self.animation_duration_sec:
                    self._unit_animations.pop(key, None)
                else:
                    t = max(0.0, min(1.0, elapsed / self.animation_duration_sec))
                    world_x = src[0] + ((dst[0] - src[0]) * t)
                    world_y = src[1] + ((dst[1] - src[1]) * t)

            cx, cy = self._world_to_canvas(world_x, world_y, canvas_w, canvas_h, bounds)
            outline, fill = self._resolve_object_colors(store, obj, friendly)
            self.canvas.create_oval(
                cx - self.unit_radius,
                cy - self.unit_radius,
                cx + self.unit_radius,
                cy + self.unit_radius,
                outline=outline,
                fill=fill,
            )

        # Keep only current units for next interpolation step.
        self._unit_positions = current_unit_positions
        stale_keys = [k for k in self._unit_animations if k not in current_unit_positions]
        for key in stale_keys:
            self._unit_animations.pop(key, None)

    def _draw_interesting_points(
        self,
        points: Iterable[tuple[float, float]],
        canvas_w: int,
        canvas_h: int,
        bounds: tuple[float, float, float, float],
    ) -> None:
        for x, y in points:
            cx, cy = self._world_to_canvas(x, y, canvas_w, canvas_h, bounds)
            self.canvas.create_oval(cx - 7, cy - 7, cx + 7, cy + 7, outline="#ffe57a", width=2)

    def _draw_supply_sources(
        self,
        points: Iterable[tuple[float, float]],
        canvas_w: int,
        canvas_h: int,
        bounds: tuple[float, float, float, float],
    ) -> None:
        for x, y in points:
            cx, cy = self._world_to_canvas(x, y, canvas_w, canvas_h, bounds)
            self.canvas.create_oval(cx - 10, cy - 10, cx + 10, cy + 10, outline="#f4f7fb", width=2)

    def _draw_autonomy_zones(
        self,
        zones: Iterable[AutonomyZoneOverlay],
        canvas_w: int,
        canvas_h: int,
        bounds: tuple[float, float, float, float],
    ) -> None:
        for idx, zone in enumerate(zones):
            cx, cy = self._world_to_canvas(zone.center_x, zone.center_y, canvas_w, canvas_h, bounds)
            edge_x, _ = self._world_to_canvas(zone.center_x + max(zone.radius, 1.0), zone.center_y, canvas_w, canvas_h, bounds)
            radius_px = max(10.0, abs(edge_x - cx))
            outline = "#ffd166" if zone.active else ("#4be68f" if zone.is_main_base else "#6ec8ff")
            dash = () if zone.active else (6, 4)
            self.canvas.create_oval(
                cx - radius_px,
                cy - radius_px,
                cx + radius_px,
                cy + radius_px,
                outline=outline,
                width=2 if zone.active else 1,
                dash=dash,
            )
            if zone.active:
                self.canvas.create_oval(
                    cx - 4,
                    cy - 4,
                    cx + 4,
                    cy + 4,
                    outline=outline,
                    fill=outline,
                )
                self.canvas.create_oval(
                    cx - 8,
                    cy - 8,
                    cx + 8,
                    cy + 8,
                    outline=outline,
                    width=1,
                )
            if zone.active and zone.is_main_base:
                label = "ACTIVE MAIN"
            elif zone.active:
                label = "ACTIVE EXPANSION"
            elif zone.is_main_base:
                label = "MAIN"
            else:
                label = "EXPANSION"
            if zone.developed:
                status = "developed"
            elif zone.needs_followup:
                status = "followup"
            else:
                status = "early"
            counts_text = f"S{zone.supply_stashes} B{zone.barracks} A{zone.arms_dealers} T{zone.tunnels} G{zone.stingers}"
            self.canvas.create_text(
                cx + 8,
                cy - 10,
                text=f"Z{idx + 1} {label}\n{status}  {counts_text}",
                anchor="sw",
                fill=outline,
                font=("Segoe UI", 8, "bold" if zone.active else "normal"),
            )

    def _draw_autonomy_events(
        self,
        events: Iterable[AutonomyEventOverlay],
        canvas_w: int,
        canvas_h: int,
        bounds: tuple[float, float, float, float],
    ) -> None:
        palette = {
            "macro": "#ff9f43",
            "production": "#7bed9f",
            "tech": "#70a1ff",
            "attack": "#ff6b81",
            "capture": "#c56cf0",
        }
        for event in events:
            if event.x is None or event.y is None:
                continue
            cx, cy = self._world_to_canvas(event.x, event.y, canvas_w, canvas_h, bounds)
            color = palette.get(event.kind, "#f5f6fa")
            self.canvas.create_rectangle(cx - 4, cy - 4, cx + 4, cy + 4, outline=color, fill=self._darken_hex(color, 0.45))
            self.canvas.create_text(
                cx + 7,
                cy + 2,
                text=event.kind[:4].upper(),
                anchor="w",
                fill=color,
                font=("Segoe UI", 7),
            )

    def _draw_player_positions(
        self,
        player_positions: Iterable[tuple[int, tuple[float, float] | None]],
        canvas_w: int,
        canvas_h: int,
        bounds: tuple[float, float, float, float],
        store: UIStore,
    ) -> None:
        for pidx, pos in player_positions:
            if pos is None:
                continue
            cx, cy = self._world_to_canvas(pos[0], pos[1], canvas_w, canvas_h, bounds)
            player = store.players.get(pidx)
            base = self._name_to_hex(player.color) if player is not None else None
            color = base or "#f5d76e"
            self.canvas.create_oval(cx - 5, cy - 5, cx + 5, cy + 5, outline=color, width=2)

    def _draw_counts(self, objects: list[WorldObject], canvas_w: int) -> None:
        total_buildings = sum(1 for o in objects if o.kind == "building")
        total_units = max(0, len(objects) - total_buildings)
        self.canvas.create_text(
            canvas_w - self.padding - 6,
            self.padding + 34,
            text=f"objects U:{total_units} B:{total_buildings}",
            anchor="ne",
            fill="#7f96a1",
            font=("Segoe UI", 8),
        )

    def _world_to_canvas(
        self,
        x: float,
        y: float,
        canvas_w: int,
        canvas_h: int,
        bounds: tuple[float, float, float, float],
    ) -> tuple[float, float]:
        min_x, min_y, max_x, max_y = bounds
        world_w = max(max_x - min_x, 1.0)
        world_h = max(max_y - min_y, 1.0)
        usable_w = max(canvas_w - (self.padding * 2), 1)
        usable_h = max(canvas_h - (self.padding * 2), 1)
        scale = min(usable_w / world_w, usable_h / world_h)
        draw_w = world_w * scale
        draw_h = world_h * scale
        origin_x = (canvas_w - draw_w) * 0.5
        origin_y = (canvas_h - draw_h) * 0.5
        u = (x - min_x) / world_w
        v = (y - min_y) / world_h
        if self.orientation == "flip_x":
            u = 1.0 - u
        elif self.orientation == "flip_y":
            v = 1.0 - v
        elif self.orientation == "flip_xy":
            u = 1.0 - u
            v = 1.0 - v
        cx = origin_x + (u * draw_w)
        cy = origin_y + (v * draw_h)
        return cx, cy

    def _resolve_bounds(
        self,
        store: UIStore,
        owned: list[WorldObject],
        enemies: list[WorldObject],
        points: list[tuple[float, float]],
        player_positions: list[tuple[int, tuple[float, float] | None]],
    ) -> tuple[float, float, float, float]:
        world_bounds = (
            store.map_min_x,
            store.map_min_y,
            store.map_min_x + max(store.map_width, 1.0),
            store.map_min_y + max(store.map_height, 1.0),
        )
        samples: list[tuple[float, float]] = []
        for obj in owned:
            samples.append((obj.x, obj.y))
        for obj in enemies:
            samples.append((obj.x, obj.y))
        for x, y in points:
            samples.append((x, y))
        for _pidx, pos in player_positions:
            if pos is not None:
                samples.append((pos[0], pos[1]))
        if not samples:
            return world_bounds

        min_x = min(p[0] for p in samples)
        min_y = min(p[1] for p in samples)
        max_x = max(p[0] for p in samples)
        max_y = max(p[1] for p in samples)
        dyn_w = max(max_x - min_x, 1.0)
        dyn_h = max(max_y - min_y, 1.0)

        world_min_x, world_min_y, world_max_x, world_max_y = world_bounds
        out_of_world = (
            min_x < world_min_x
            or min_y < world_min_y
            or max_x > world_max_x
            or max_y > world_max_y
        )

        if out_of_world:
            pad_x = max(dyn_w * 0.08, 20.0)
            pad_y = max(dyn_h * 0.08, 20.0)
            return (min_x - pad_x, min_y - pad_y, max_x + pad_x, max_y + pad_y)
        return world_bounds

    def _resolve_object_colors(self, store: UIStore, obj: WorldObject, friendly: bool) -> tuple[str, str]:
        default_outline = "#4be68f" if friendly else "#ff6e6e"
        default_fill = "#19442f" if friendly else "#5c1c1c"
        owner_idx = obj.owner_player_index
        if owner_idx is None:
            return default_outline, default_fill
        player = store.players.get(owner_idx)
        if player is None:
            return default_outline, default_fill
        base = self._name_to_hex(player.color)
        if base is None:
            return default_outline, default_fill
        return base, self._darken_hex(base, 0.38)

    @staticmethod
    def _name_to_hex(color_name: str) -> str | None:
        table = {
            "red": "#ff4e4e",
            "blue": "#5d8dff",
            "green": "#5fdc73",
            "yellow": "#f2d463",
            "orange": "#f2a04f",
            "purple": "#b37af0",
            "pink": "#ea93cf",
            "cyan": "#59d5df",
            "teal": "#4ab8b1",
            "white": "#e8eef2",
            "black": "#3a3a3a",
            "gray": "#9aa7ad",
            "grey": "#9aa7ad",
        }
        key = color_name.strip().lower()
        if key in table:
            return table[key]
        if key.startswith("#") and len(key) == 7:
            return key
        return None

    @staticmethod
    def _darken_hex(color_hex: str, factor: float) -> str:
        value = color_hex.lstrip("#")
        if len(value) != 6:
            return color_hex
        r = int(value[0:2], 16)
        g = int(value[2:4], 16)
        b = int(value[4:6], 16)
        r = max(0, min(255, int(r * factor)))
        g = max(0, min(255, int(g * factor)))
        b = max(0, min(255, int(b * factor)))
        return f"#{r:02x}{g:02x}{b:02x}"
