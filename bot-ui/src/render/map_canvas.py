from __future__ import annotations

import tkinter as tk
from typing import Iterable

from state.store import UIStore, WorldObject


class MapRenderer:
    def __init__(self, canvas: tk.Canvas) -> None:
        self.canvas = canvas
        self.padding = 16

    def draw(self, store: UIStore) -> None:
        width = max(self.canvas.winfo_width(), 1)
        height = max(self.canvas.winfo_height(), 1)
        self.canvas.delete("all")
        self.canvas.create_rectangle(0, 0, width, height, fill="#0c1318", outline="")
        self._draw_map_bounds(width, height, store.map_width, store.map_height)
        self._draw_objects(store.owned_objects.values(), width, height, store, friendly=True)
        self._draw_objects(store.visible_enemies.values(), width, height, store, friendly=False)
        self._draw_interesting_points(store.interesting_points, width, height, store)

    def _draw_map_bounds(self, canvas_w: int, canvas_h: int, world_w: float, world_h: float) -> None:
        left = self.padding
        top = self.padding
        right = canvas_w - self.padding
        bottom = canvas_h - self.padding
        self.canvas.create_rectangle(left, top, right, bottom, outline="#3f5663", width=1)
        self.canvas.create_text(left + 6, top + 6, text="Map", anchor="nw", fill="#92a7b0", font=("Segoe UI", 9))
        self.canvas.create_text(
            right - 6,
            top + 6,
            text=f"{int(world_w)}x{int(world_h)}",
            anchor="ne",
            fill="#92a7b0",
            font=("Segoe UI", 9),
        )

    def _draw_objects(
        self,
        objects: Iterable[WorldObject],
        canvas_w: int,
        canvas_h: int,
        store: UIStore,
        friendly: bool,
    ) -> None:
        for obj in objects:
            cx, cy = self._world_to_canvas(obj.x, obj.y, canvas_w, canvas_h, store.map_width, store.map_height)
            if friendly:
                if obj.kind == "building":
                    self.canvas.create_rectangle(cx - 6, cy - 6, cx + 6, cy + 6, outline="#4be68f", fill="#19442f")
                else:
                    self.canvas.create_oval(cx - 4, cy - 4, cx + 4, cy + 4, outline="#4be68f", fill="#19442f")
            else:
                self.canvas.create_oval(cx - 4, cy - 4, cx + 4, cy + 4, outline="#ff6e6e", fill="#5c1c1c")

    def _draw_interesting_points(
        self,
        points: Iterable[tuple[float, float]],
        canvas_w: int,
        canvas_h: int,
        store: UIStore,
    ) -> None:
        for x, y in points:
            cx, cy = self._world_to_canvas(x, y, canvas_w, canvas_h, store.map_width, store.map_height)
            self.canvas.create_oval(cx - 7, cy - 7, cx + 7, cy + 7, outline="#ffe57a", width=2)

    def _world_to_canvas(
        self,
        x: float,
        y: float,
        canvas_w: int,
        canvas_h: int,
        world_w: float,
        world_h: float,
    ) -> tuple[float, float]:
        world_w = max(world_w, 1.0)
        world_h = max(world_h, 1.0)
        usable_w = max(canvas_w - (self.padding * 2), 1)
        usable_h = max(canvas_h - (self.padding * 2), 1)
        scale = min(usable_w / world_w, usable_h / world_h)
        draw_w = world_w * scale
        draw_h = world_h * scale
        origin_x = (canvas_w - draw_w) * 0.5
        origin_y = (canvas_h - draw_h) * 0.5
        cx = origin_x + (x * scale)
        cy = origin_y + (y * scale)
        return cx, cy
