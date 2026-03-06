from __future__ import annotations

import json
import queue
import re
import time
import tkinter as tk
from tkinter import scrolledtext, ttk
from typing import Any

from controllers.commands import (
    LAN_ACTIONS,
    MAIN_MENU_ACTIONS,
    MAIN_MENU_BACK_CONTROL_IDS,
    PLAY_GAME_CONTROL_IDS,
    SINGLE_PLAYER_CONTROL_IDS,
)
from protocol.client import PipeClient
from protocol.messages import hello_message, session_command_message, subscribe_message, unsubscribe_message
from render.map_canvas import MapRenderer
from state.store import UIStore
from uilog.ui_log import UILog


class BotUIApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("Zero Hour Bot UI")
        self.root.geometry("1280x1024")
        self.root.minsize(1024, 768)

        self.client = PipeClient()
        self.store = UIStore()
        self.map_renderer: MapRenderer | None = None
        self.log: UILog | None = None
        self.pending_requests: dict[str, dict[str, Any]] = {}
        self._next_meta_poll_monotonic = 0.0
        self._dirty_view = True
        self._last_redraw_monotonic = 0.0
        self._suspend_poll_until_monotonic = 0.0
        self._hello_ok = False
        self._connecting = False
        self._request_queue: "queue.PriorityQueue[tuple[int, int, dict[str, Any]]]" = queue.PriorityQueue()
        self._request_seq = 0
        self._request_worker_started = False
        self._poll_paths = ["game.objects_all", "game.objects", "game.players", "game.resources", "game.status"]
        self._poll_path_index = 0
        self._map_orientations = ["flip_y", "normal", "flip_x", "flip_xy"]
        self._map_orientation_index = 0

        self.poll_enabled = tk.BooleanVar(value=True)
        self.poll_interval_ms = tk.IntVar(value=1000)
        self.stream_enabled = tk.BooleanVar(value=False)
        self.debug_inbound = tk.BooleanVar(value=True)
        self.pipe_name_var = tk.StringVar(value="zh_ai_control")
        self.connection_state_var = tk.StringVar(value="Disconnected")
        self.command_input_var = tk.StringVar(value='Session.Status {}')

        self._build_ui()
        self._start_request_worker()
        self.root.after(50, self._process_incoming)
        self.root.after(500, self._poll_loop)

    def _build_ui(self) -> None:
        self.root.grid_columnconfigure(0, weight=1, uniform="cols")
        self.root.grid_columnconfigure(1, weight=1, uniform="cols")
        self.root.grid_rowconfigure(0, weight=1)

        left = ttk.Frame(self.root, padding=8)
        left.grid(row=0, column=0, sticky="nsew")
        left.grid_columnconfigure(0, weight=1)

        right = ttk.Frame(self.root, padding=8)
        right.grid(row=0, column=1, sticky="nsew")
        right.grid_columnconfigure(0, weight=1)
        right.grid_rowconfigure(0, weight=1)
        right.grid_rowconfigure(1, weight=1)

        self._build_left_panel(left)
        self._build_right_top_map(right)
        self._build_right_bottom_controls(right)

    def _build_left_panel(self, parent: ttk.Frame) -> None:
        conn = ttk.LabelFrame(parent, text="Connection", padding=8)
        conn.grid(row=0, column=0, sticky="ew")
        conn.grid_columnconfigure(1, weight=1)

        ttk.Label(conn, text="Pipe").grid(row=0, column=0, sticky="w")
        ttk.Entry(conn, textvariable=self.pipe_name_var).grid(row=0, column=1, sticky="ew", padx=6)
        ttk.Button(conn, text="Connect", command=self.connect).grid(row=0, column=2, padx=2)
        ttk.Button(conn, text="Disconnect", command=self.disconnect).grid(row=0, column=3, padx=2)
        ttk.Label(conn, textvariable=self.connection_state_var).grid(row=1, column=0, columnspan=4, sticky="w", pady=(6, 0))

        nav = ttk.LabelFrame(parent, text="Menu / Start Game", padding=8)
        nav.grid(row=1, column=0, sticky="ew", pady=(8, 0))
        nav.grid_columnconfigure(0, weight=1)
        nav.grid_columnconfigure(1, weight=1)
        ttk.Button(nav, text="Main Multiplayer", command=lambda: self._menu_click(MAIN_MENU_ACTIONS["main_multiplayer"])).grid(
            row=0, column=0, sticky="ew", padx=2, pady=2
        )
        ttk.Button(nav, text="Main Network", command=lambda: self._menu_click(MAIN_MENU_ACTIONS["main_network"])).grid(
            row=0, column=1, sticky="ew", padx=2, pady=2
        )
        ttk.Button(nav, text="SOLO PLAY", command=self._go_single_player).grid(
            row=1, column=0, sticky="ew", padx=2, pady=2
        )
        ttk.Button(nav, text="Main Skirmish", command=lambda: self._menu_click(MAIN_MENU_ACTIONS["main_skirmish"])).grid(
            row=1, column=1, sticky="ew", padx=2, pady=2
        )
        ttk.Button(nav, text="LAN Create", command=lambda: self._menu_click(LAN_ACTIONS["lan_create"])).grid(
            row=2, column=0, sticky="ew", padx=2, pady=2
        )
        ttk.Button(nav, text="LAN Join", command=lambda: self._menu_click(LAN_ACTIONS["lan_join"])).grid(
            row=2, column=1, sticky="ew", padx=2, pady=2
        )
        ttk.Button(nav, text="LAN Direct", command=lambda: self._menu_click(LAN_ACTIONS["lan_direct"])).grid(
            row=3, column=0, sticky="ew", padx=2, pady=2
        )
        ttk.Button(nav, text="LAN Back", command=lambda: self._menu_click(LAN_ACTIONS["lan_back"])).grid(
            row=3, column=1, sticky="ew", padx=2, pady=2
        )
        ttk.Button(nav, text="Session Status", command=lambda: self._send_session_command("Session.Status", {})).grid(
            row=4, column=0, sticky="ew", padx=2, pady=2
        )
        ttk.Button(nav, text="PLAY GAME", command=self._play_game).grid(row=4, column=1, sticky="ew", padx=2, pady=2)
        ttk.Button(nav, text="MAIN MENU", command=self._go_main_menu).grid(row=5, column=0, sticky="ew", padx=2, pady=2)

        players = ttk.LabelFrame(parent, text="Players", padding=8)
        players.grid(row=2, column=0, sticky="nsew", pady=(8, 0))
        parent.grid_rowconfigure(2, weight=1)
        columns = ("idx", "name", "color", "team", "cash", "units", "bld", "promo", "pos")
        tree = ttk.Treeview(players, columns=columns, show="headings", height=12)
        self.player_tree = tree
        headers = {
            "idx": "Idx",
            "name": "Name",
            "color": "Color",
            "team": "Team",
            "cash": "Cash",
            "units": "Units",
            "bld": "Bld",
            "promo": "Promo",
            "pos": "Pos",
        }
        widths = {"idx": 42, "name": 120, "color": 64, "team": 50, "cash": 72, "units": 52, "bld": 52, "promo": 60, "pos": 120}
        for key in columns:
            tree.heading(key, text=headers[key])
            tree.column(key, width=widths[key], anchor="center")
        tree.grid(row=0, column=0, sticky="nsew")
        players.grid_rowconfigure(0, weight=1)
        players.grid_columnconfigure(0, weight=1)

    def _build_right_top_map(self, parent: ttk.Frame) -> None:
        map_frame = ttk.LabelFrame(parent, text="Map", padding=6)
        map_frame.grid(row=0, column=0, sticky="nsew")
        map_frame.grid_rowconfigure(1, weight=1)
        map_frame.grid_columnconfigure(0, weight=1)
        toolbar = ttk.Frame(map_frame)
        toolbar.grid(row=0, column=0, sticky="ew", pady=(0, 4))
        ttk.Button(toolbar, text="Rotate/Flip Map", command=self._cycle_map_orientation).grid(row=0, column=0, sticky="w")
        canvas = tk.Canvas(map_frame, bg="#0c1318", highlightthickness=0)
        canvas.grid(row=1, column=0, sticky="nsew")
        self.map_renderer = MapRenderer(canvas)
        self.map_canvas = canvas
        canvas.bind("<Configure>", lambda _evt: self.redraw_map())

    def _build_right_bottom_controls(self, parent: ttk.Frame) -> None:
        bottom = ttk.Frame(parent)
        bottom.grid(row=1, column=0, sticky="nsew", pady=(8, 0))
        bottom.grid_rowconfigure(2, weight=1)
        bottom.grid_columnconfigure(0, weight=1)

        controls = ttk.LabelFrame(bottom, text="Bot Controls", padding=8)
        controls.grid(row=0, column=0, sticky="ew")
        for i in range(5):
            controls.grid_columnconfigure(i, weight=1)

        ttk.Button(controls, text="Build Stash", command=lambda: self._send_session_command("Game.BuildSupplyStashSmart", {})).grid(
            row=0, column=0, sticky="ew", padx=2, pady=2
        )
        ttk.Button(controls, text="Build Barracks", command=lambda: self._send_session_command("Game.BuildBarracksSmart", {})).grid(
            row=0, column=1, sticky="ew", padx=2, pady=2
        )
        ttk.Button(controls, text="Build Command", command=lambda: self._send_session_command("Game.BuildCommandCenterSmart", {})).grid(
            row=0, column=2, sticky="ew", padx=2, pady=2
        )
        ttk.Button(controls, text="Query Objects", command=lambda: self._query("game.objects_all", quiet=False)).grid(
            row=0, column=3, sticky="ew", padx=2, pady=2
        )
        ttk.Button(controls, text="Query Enemies", command=lambda: self._query("game.visible_enemies", quiet=False)).grid(
            row=0, column=4, sticky="ew", padx=2, pady=2
        )
        ttk.Checkbutton(controls, text="Polling", variable=self.poll_enabled).grid(row=1, column=0, sticky="w")
        ttk.Label(controls, text="Interval ms").grid(row=1, column=1, sticky="e")
        ttk.Entry(controls, textvariable=self.poll_interval_ms, width=8).grid(row=1, column=2, sticky="w")
        ttk.Checkbutton(controls, text="Use Streaming", variable=self.stream_enabled, command=self._toggle_streaming).grid(
            row=1, column=3, columnspan=2, sticky="w"
        )
        ttk.Checkbutton(controls, text="Debug Inbound", variable=self.debug_inbound).grid(row=2, column=0, sticky="w")

        cmd_row = ttk.LabelFrame(bottom, text="Command Input", padding=8)
        cmd_row.grid(row=1, column=0, sticky="ew", pady=(6, 0))
        cmd_row.grid_columnconfigure(0, weight=1)
        ttk.Entry(cmd_row, textvariable=self.command_input_var).grid(row=0, column=0, sticky="ew", padx=(0, 6))
        ttk.Button(cmd_row, text="Send", command=self._send_command_input).grid(row=0, column=1)

        logs = ttk.LabelFrame(bottom, text="Logs", padding=4)
        logs.grid(row=2, column=0, sticky="nsew", pady=(6, 0))
        logs.grid_rowconfigure(0, weight=1)
        logs.grid_columnconfigure(0, weight=1)
        log_box = scrolledtext.ScrolledText(logs, height=12, state=tk.DISABLED)
        log_box.grid(row=0, column=0, sticky="nsew")
        self.log = UILog(log_box)

    def connect(self) -> None:
        if self._connecting:
            return
        self.client.pipe_name = self.pipe_name_var.get().strip() or "zh_ai_control"
        self._hello_ok = False
        self._connecting = True
        self.connection_state_var.set("Connecting...")
        self._log("Connecting to adapter.")
        msg = hello_message()
        req_id = str(msg["request_id"])
        self.pending_requests[req_id] = {"kind": "hello", "ts": time.monotonic()}
        self._request_async(msg)

    def disconnect(self) -> None:
        self.client.disconnect()
        self._hello_ok = False
        self._connecting = False
        self.connection_state_var.set("Disconnected")
        self.pending_requests.clear()
        self._log("Disconnected.")

    def _toggle_streaming(self) -> None:
        # Streaming assumes a persistent duplex pipe session. The current UI command path
        # uses one-shot request/reply workers for reliability.
        self._log("Streaming toggle is not enabled in one-shot transport mode yet.")
        self.stream_enabled.set(False)
        return
        if not self.client.is_connected:
            return
        try:
            if self.stream_enabled.get():
                req = subscribe_message(["stateframe", "events", "logs"])
                req_id = self.client.send_json(req)
                self.pending_requests[req_id] = {"kind": "subscribe"}
                self._log("Requested stream subscription.")
            else:
                req = unsubscribe_message(["stateframe", "events", "logs"])
                req_id = self.client.send_json(req)
                self.pending_requests[req_id] = {"kind": "unsubscribe"}
                self._log("Requested stream unsubscription.")
        except Exception as exc:  # noqa: BLE001
            self._log(f"Streaming toggle failed: {exc}")

    def _menu_click(self, control_id: str) -> None:
        self._send_session_command("Menu.Click", {"controlId": control_id})

    def _play_game(self) -> None:
        self._log("Attempting PLAY GAME (ButtonStart variants).")
        for control_id in PLAY_GAME_CONTROL_IDS:
            self._menu_click(control_id)

    def _go_main_menu(self) -> None:
        self._log("Attempting MAIN MENU (ButtonBack variants).")
        for control_id in MAIN_MENU_BACK_CONTROL_IDS:
            self._menu_click(control_id)

    def _go_single_player(self) -> None:
        self._log("Attempting SINGLE PLAYER (ButtonSinglePlayer variants).")
        for control_id in SINGLE_PLAYER_CONTROL_IDS:
            self._menu_click(control_id)

    def _send_session_command(self, cmd: str, args: dict[str, Any], quiet: bool = False) -> None:
        if not self._hello_ok:
            if not quiet:
                self._log("Adapter handshake not ready yet (no HelloAck).")
            return
        msg = session_command_message(cmd, args)
        try:
            req_id = str(msg["request_id"])
            pending = {"kind": "session_command", "cmd": cmd, "ts": time.monotonic()}
            if cmd == "Game.Query":
                pending["path"] = str(args.get("path", ""))
            self.pending_requests[req_id] = pending
            if cmd != "Game.Query":
                # Prioritize operator actions over background polling for a short window.
                self._suspend_poll_until_monotonic = time.monotonic() + 1.0
            priority = 1 if cmd == "Game.Query" else 0
            self._request_async(msg, priority=priority)
            if not quiet:
                self._log(f"sent {cmd} request_id={req_id}")
        except Exception as exc:  # noqa: BLE001
            if not quiet:
                self._log(f"Send failed ({cmd}): {exc}")

    def _send_command_input(self) -> None:
        raw = self.command_input_var.get().strip()
        if not raw:
            return
        parts = raw.split(" ", 1)
        cmd = parts[0]
        args: dict[str, Any] = {}
        if len(parts) > 1 and parts[1].strip():
            try:
                decoded = json.loads(parts[1].strip())
            except json.JSONDecodeError as exc:
                self._log(f"Invalid JSON args: {exc}")
                return
            if not isinstance(decoded, dict):
                self._log("Command args must decode to an object.")
                return
            args = decoded
        self._send_session_command(cmd, args)

    def _query(self, path: str, quiet: bool = True) -> None:
        self._send_session_command("Game.Query", {"path": path}, quiet=quiet)

    def _poll_loop(self) -> None:
        interval = 500
        try:
            interval = max(100, int(self.poll_interval_ms.get()))
            if self._hello_ok and self.poll_enabled.get():
                now = time.monotonic()
                if now < self._suspend_poll_until_monotonic:
                    return
                # Avoid flooding a single-instance named pipe. Send at most one query per tick,
                # and only when no other query is currently pending.
                if self._pending_query_count() == 0 and self._request_queue.qsize() == 0:
                    path = self._poll_paths[self._poll_path_index % len(self._poll_paths)]
                    self._poll_path_index += 1
                    if path in ("game.players", "game.resources", "game.status") and now < self._next_meta_poll_monotonic:
                        pass
                    else:
                        self._query(path, quiet=True)
                        if path in ("game.players", "game.resources", "game.status"):
                            self._next_meta_poll_monotonic = now + 1.5
        except Exception as exc:  # noqa: BLE001
            self._log(f"poll_loop error: {exc}")
        finally:
            self.root.after(interval, self._poll_loop)

    def _process_incoming(self) -> None:
        try:
            now = time.monotonic()
            # Keep pending requests alive longer; queued/in-flight operations can exceed a few seconds.
            stale_ids = []
            for req_id, info in self.pending_requests.items():
                ts = info.get("ts")
                if not isinstance(ts, float):
                    continue
                age = now - float(ts)
                if age > 45.0:
                    stale_ids.append(req_id)
            for req_id in stale_ids:
                info = self.pending_requests.pop(req_id, {})
                self._log(f"dropping stale pending request_id={req_id} kind={info.get('kind')} cmd={info.get('cmd', '')}")

            while not self.client.errors.empty():
                err = self.client.errors.get_nowait()
                self._log(err)
                m = re.search(r"request_id=([0-9a-fA-F]+)", err)
                if m:
                    self.pending_requests.pop(m.group(1), None)
                if self._connecting:
                    self._connecting = False
                    self._hello_ok = False
                    self.connection_state_var.set("Disconnected")
            while not self.client.incoming.empty():
                msg = self.client.incoming.get_nowait()
                self._handle_message(msg)
            now = time.monotonic()
            if self._dirty_view and now - self._last_redraw_monotonic >= 0.1:
                self._refresh_player_table()
                self.redraw_map()
                self._dirty_view = False
                self._last_redraw_monotonic = now
        except Exception as exc:  # noqa: BLE001
            self._log(f"process_incoming error: {exc}")
        finally:
            self.root.after(50, self._process_incoming)

    def _handle_message(self, msg: dict[str, Any]) -> None:
        msg_type = str(msg.get("type", ""))
        request_id = str(msg.get("request_id", ""))
        if self.debug_inbound.get():
            self._log(f"recv {msg_type} request_id={request_id}")
        pending = self.pending_requests.pop(request_id, None) if request_id else None

        if msg_type == "HelloAck":
            ok = msg.get("ok", False)
            self._hello_ok = bool(ok)
            self._connecting = False
            self.connection_state_var.set("Connected" if ok else "Hello failed")
            self._log(f"HelloAck ok={ok} session_id={msg.get('session_id', '?')}")
            return
        if msg_type == "SessionState":
            state = msg.get("state")
            if isinstance(state, dict):
                self.store.session_state = state
                self.store.update_players(state)
            self._log("SessionState update.")
            self._dirty_view = True
            return
        if msg_type == "StateFrame":
            data = msg.get("data", msg)
            if isinstance(data, dict):
                self.store.update_map_metadata(data.get("world", data))
                self.store.update_owned_objects(data.get("self", data))
                self.store.update_visible_enemies(data.get("enemy", data))
                self._dirty_view = True
            return
        if msg_type == "AdapterLog":
            self._log(f"adapter[{msg.get('level', 'info')}]: {msg.get('message', '')}")
            return
        if msg_type == "Error":
            self._log(f"Error code={msg.get('code')} reason={msg.get('reason')}")
            return

        if pending and pending.get("cmd") == "Game.Query":
            self._apply_query_result(msg, str(pending.get("path", "")))
        elif pending and pending.get("kind") == "session_command":
            cmd = str(pending.get("cmd", ""))
            ok = msg.get("ok")
            code = msg.get("code")
            reason = msg.get("reason")
            extra = ""
            if code is not None or reason is not None:
                extra = f" code={code} reason={reason}"
            self._log(f"ack {cmd} ok={ok}{extra}")
            if pending.get("cmd") == "Game.Query":
                self._apply_query_result(msg, str(pending.get("path", "")))
        else:
            if msg_type == "QueryResult":
                # Late/unmatched query replies can still carry useful state updates.
                self._apply_query_result(msg, "")
            elif msg_type == "ActionAck":
                pass

    def _apply_query_result(self, msg: dict[str, Any], pending_path: str = "") -> None:
        if msg.get("ok") is False:
            self._log(
                f"query failed path={pending_path or '?'} code={msg.get('code')} reason={msg.get('reason')}"
            )
            return
        payload = self._extract_payload(msg)
        if not isinstance(payload, (dict, list)):
            self._log(f"query payload unsupported path={pending_path or '?'} type={type(payload).__name__}")
            return

        path = pending_path or self._extract_path_hint(msg, payload)
        if self.debug_inbound.get():
            self._log(f"apply query path={path or '?'} payload_type={type(payload).__name__}")
        if path == "game.objects":
            if isinstance(payload, dict):
                units_n = len(payload.get("units", [])) if isinstance(payload.get("units"), list) else 0
                bld_n = len(payload.get("buildings", [])) if isinstance(payload.get("buildings"), list) else 0
                self._log(f"game.objects counts units={units_n} buildings={bld_n}")
                coords: list[tuple[float, float]] = []
                for key in ("units", "buildings"):
                    rows = payload.get(key)
                    if not isinstance(rows, list):
                        continue
                    for row in rows:
                        if not isinstance(row, dict):
                            continue
                        x = row.get("x")
                        y = row.get("y")
                        if isinstance(x, (int, float)) and isinstance(y, (int, float)):
                            coords.append((float(x), float(y)))
                if coords:
                    min_x = min(p[0] for p in coords)
                    max_x = max(p[0] for p in coords)
                    min_y = min(p[1] for p in coords)
                    max_y = max(p[1] for p in coords)
                    self._log(f"game.objects extents x=[{min_x:.1f},{max_x:.1f}] y=[{min_y:.1f},{max_y:.1f}]")
            self.store.update_owned_objects(payload)
        elif path == "game.objects_all":
            if isinstance(payload, dict):
                players = payload.get("players")
                total_units = 0
                total_buildings = 0
                if isinstance(players, list):
                    for row in players:
                        if not isinstance(row, dict):
                            continue
                        total_units += len(row.get("units", [])) if isinstance(row.get("units"), list) else 0
                        total_buildings += len(row.get("buildings", [])) if isinstance(row.get("buildings"), list) else 0
                self._log(f"game.objects_all counts units={total_units} buildings={total_buildings}")
            self.store.update_objects_all(payload)
        elif path == "game.visible_enemies":
            self.store.update_visible_enemies(payload)
        elif path == "game.players":
            self.store.update_players(payload)
        elif path == "game.resources":
            self.store.update_resources(payload)
        elif path == "game.status":
            self.store.update_map_metadata(payload)
        else:
            if isinstance(payload, dict):
                # Opportunistic parsing for mixed aggregate payloads.
                self.store.update_owned_objects(payload)
                self.store.update_objects_all(payload)
                self.store.update_visible_enemies(payload)
                self.store.update_players(payload)
                self.store.update_resources(payload)
                self.store.update_map_metadata(payload)
        self._dirty_view = True

    def _extract_path_hint(self, msg: dict[str, Any], payload: Any) -> str:
        for key in ("path", "query_path"):
            if isinstance(msg.get(key), str):
                return str(msg[key])
        if isinstance(payload, dict):
            for key in ("path", "query_path"):
                if isinstance(payload.get(key), str):
                    return str(payload[key])
        # Heuristic when adapter doesn't echo path.
        if isinstance(payload, dict):
            if "players" in payload:
                players_node = payload.get("players")
                if isinstance(players_node, list) and players_node and isinstance(players_node[0], dict):
                    first = players_node[0]
                    if "units" in first or "buildings" in first:
                        return "game.objects_all"
            if "buildings" in payload and "units" in payload:
                return "game.objects"
            if "enemies" in payload:
                return "game.visible_enemies"
            if "visible_enemies" in payload:
                return "game.visible_enemies"
            if "players" in payload:
                return "game.players"
            if "resources" in payload:
                return "game.resources"
            if "objects" in payload or "units" in payload:
                return "game.objects"
        if isinstance(payload, list):
            if payload and isinstance(payload[0], dict):
                if "ready" in payload[0] or "slot" in payload[0]:
                    return "game.players"
                return "game.objects"
        return ""

    def _extract_payload(self, msg: dict[str, Any]) -> Any:
        for key in ("result", "data", "payload", "value"):
            if key in msg:
                value = msg[key]
                # Some adapters wrap the useful payload one level deeper.
                if isinstance(value, dict):
                    for inner in ("data", "result", "payload", "value"):
                        if inner in value and isinstance(value[inner], (dict, list)):
                            return value[inner]
                return value
        return msg

    def _refresh_player_table(self) -> None:
        existing = set(self.player_tree.get_children(""))
        target_ids = {f"p-{idx}" for idx in self.store.players}
        for item in existing - target_ids:
            self.player_tree.delete(item)
        for idx in sorted(self.store.players):
            meta = self.store.players[idx]
            pos = "-"
            if meta.map_position:
                pos = f"{meta.map_position[0]:.0f},{meta.map_position[1]:.0f}"
            values = (
                meta.player_index,
                meta.name,
                meta.color,
                meta.team,
                meta.cash if meta.cash is not None else "-",
                meta.unit_count,
                meta.building_count,
                meta.promotions,
                pos,
            )
            item_id = f"p-{idx}"
            if item_id in existing:
                self.player_tree.item(item_id, values=values)
            else:
                self.player_tree.insert("", "end", iid=item_id, values=values)

    def redraw_map(self) -> None:
        if self.map_renderer is not None:
            self.map_renderer.draw(self.store)

    def _cycle_map_orientation(self) -> None:
        if self.map_renderer is None:
            return
        self._map_orientation_index = (self._map_orientation_index + 1) % len(self._map_orientations)
        self.map_renderer.orientation = self._map_orientations[self._map_orientation_index]
        self._log(f"Map orientation set to {self.map_renderer.orientation}")
        self.redraw_map()

    def _log(self, message: str) -> None:
        if self.log is not None:
            self.log.write(message)

    def _request_async(self, payload: dict[str, Any], priority: int = 0) -> None:
        self._request_seq += 1
        self._request_queue.put((priority, self._request_seq, payload))

    def _start_request_worker(self) -> None:
        if self._request_worker_started:
            return
        self._request_worker_started = True

        def worker() -> None:
            while True:
                _priority, _seq, payload = self._request_queue.get()
                try:
                    response = self.client.request_once(payload)
                    self.client.incoming.put(response)
                except Exception as exc:  # noqa: BLE001
                    req_id = str(payload.get("request_id", ""))
                    self.client.errors.put(f"request failed request_id={req_id}: {exc}")
                finally:
                    self._request_queue.task_done()

        import threading  # noqa: PLC0415

        threading.Thread(target=worker, name="botui-request-worker", daemon=True).start()

    def _pending_query_count(self) -> int:
        total = 0
        for info in self.pending_requests.values():
            if info.get("cmd") == "Game.Query":
                total += 1
        return total
