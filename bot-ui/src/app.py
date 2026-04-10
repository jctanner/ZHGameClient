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
    SOLO_BACK_CONTROL_IDS,
)
from protocol.client import PipeClient
from protocol.messages import hello_message, session_command_message, subscribe_message, unsubscribe_message
from render.map_canvas import MapRenderer
from state.store import UIStore
from uilog.ui_log import UILog

GLA_SCIENCE_SUGGESTIONS = (
    "SCIENCE_ScudLauncher",
    "SCIENCE_MarauderTank",
    "SCIENCE_TechnicalTraining",
    "SCIENCE_Hijacker",
    "SCIENCE_RebelAmbush1",
    "SCIENCE_RebelAmbush2",
    "SCIENCE_RebelAmbush3",
    "SCIENCE_CashBounty1",
    "SCIENCE_CashBounty2",
    "SCIENCE_CashBounty3",
    "SCIENCE_EmergencyRepair1",
    "SCIENCE_EmergencyRepair2",
    "SCIENCE_EmergencyRepair3",
    "SCIENCE_AnthraxBomb",
    "SCIENCE_SneakAttack",
    "SCIENCE_GPSScrambler",
    "Slth_SCIENCE_GPSScrambler",
    "Early_SCIENCE_EmergencyRepair1",
    "Early_SCIENCE_EmergencyRepair2",
    "Early_SCIENCE_EmergencyRepair3",
)

GLA_PALACE_UPGRADE_SUGGESTIONS = (
    "Upgrade_GLAFortifiedStructure",
    "Upgrade_GLAArmTheMob",
    "Upgrade_GLAAnthraxBeta",
    "Upgrade_GLAToxinShells",
    "Chem_Upgrade_GLAAnthraxGamma",
    "GC_Slth_Upgrade_GLAQuadCannonSnipe",
    "Demo_Upgrade_GLADemoTrapHighExplosiveBomb",
)

GLA_BLACK_MARKET_UPGRADE_SUGGESTIONS = (
    "Upgrade_GLAAPBullets",
    "Upgrade_GLAAPRockets",
    "Upgrade_GLABuggyAmmo",
    "Upgrade_GLAJunkRepair",
    "Upgrade_GLARadarVanScan",
    "Upgrade_GLAWorkerShoes",
    "Upgrade_GLACamoNetting",
)


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
        self._request_queue: "queue.PriorityQueue[tuple[int, int, dict[str, Any], int | None]]" = queue.PriorityQueue()
        self._request_seq = 0
        self._request_worker_started = False
        self._autonomy_mode = "manual"
        self._autonomy_config_after_id: str | None = None
        self._suppress_autonomy_control_updates = False
        self._last_autonomy_config_signature: tuple[tuple[str, Any], ...] | None = None
        self._poll_paths = ["game.grid", "game.players", "game.status", "game.resources", "game.grid_objects"]
        self._poll_path_index = 0
        self._map_orientations = ["flip_y", "normal", "flip_x", "flip_xy"]
        self._map_orientation_index = 0

        self.poll_enabled = tk.BooleanVar(value=True)
        self.map_updates_enabled = tk.BooleanVar(value=True)
        self.poll_interval_ms = tk.IntVar(value=1000)
        self.stream_enabled = tk.BooleanVar(value=False)
        self.debug_inbound = tk.BooleanVar(value=True)
        self.build_count_var = tk.IntVar(value=1)
        self.unit_queue_count_var = tk.IntVar(value=1)
        self.raid_count_var = tk.IntVar(value=1)
        self.target_player_index_var = tk.IntVar(value=0)
        self.money_player_index_var = tk.IntVar(value=0)
        self.money_value_var = tk.StringVar(value="20000")
        self.promotion_science_var = tk.StringVar(value=GLA_SCIENCE_SUGGESTIONS[0])
        self.palace_upgrade_var = tk.StringVar(value=GLA_PALACE_UPGRADE_SUGGESTIONS[0])
        self.black_market_upgrade_var = tk.StringVar(value=GLA_BLACK_MARKET_UPGRADE_SUGGESTIONS[0])
        self.scud_x_var = tk.StringVar(value="0")
        self.scud_y_var = tk.StringVar(value="0")
        self.grid_cell_var = tk.StringVar(value="C16")
        self.chat_text_var = tk.StringVar(value="")
        self.chat_scope_var = tk.StringVar(value="everyone")
        self.query_preset_var = tk.StringVar(value="game.objects")
        self.pipe_name_var = tk.StringVar(value="zh_ai_control")
        self.connection_state_var = tk.StringVar(value="Disconnected")
        self.command_input_var = tk.StringVar(value='Session.Status {}')
        self.build_mix_stash_var = tk.IntVar(value=0)
        self.build_mix_barracks_var = tk.IntVar(value=0)
        self.build_mix_command_var = tk.IntVar(value=0)
        self.build_mix_arms_var = tk.IntVar(value=0)
        self.build_mix_palace_var = tk.IntVar(value=0)
        self.build_mix_market_var = tk.IntVar(value=0)
        self.build_mix_scud_var = tk.IntVar(value=0)
        self.build_mix_tunnel_var = tk.IntVar(value=0)
        self.build_mix_stinger_var = tk.IntVar(value=0)
        self.camera_angle_deg_var = tk.DoubleVar(value=0.0)
        self.camera_zoom_var = tk.DoubleVar(value=1.0)
        self.camera_height_var = tk.DoubleVar(value=0.0)
        self.camera_zoom_limited_var = tk.BooleanVar(value=True)
        self.autonomy_profile_var = tk.StringVar(value="sprawl")
        self.autonomy_status_var = tk.StringVar(value="Autonomy: manual")
        self.sprawl_multiplier_var = tk.DoubleVar(value=10.0)
        self.sprawl_multiplier_var.trace_add("write", self._on_sprawl_multiplier_changed)

        self._build_ui()
        self._start_request_worker()
        self.root.after(50, self._process_incoming)
        self.root.after(500, self._poll_loop)

    def _build_ui(self) -> None:
        self.root.grid_columnconfigure(0, weight=1)
        self.root.grid_rowconfigure(0, weight=1)

        notebook = ttk.Notebook(self.root)
        notebook.grid(row=0, column=0, sticky="nsew")

        ops_tab = ttk.Frame(notebook, padding=8)
        menu_tab = ttk.Frame(notebook, padding=8)
        notebook.add(ops_tab, text="Bot")
        notebook.add(menu_tab, text="Menu")

        ops_tab.grid_columnconfigure(0, weight=1, uniform="cols")
        ops_tab.grid_columnconfigure(1, weight=1, uniform="cols")
        ops_tab.grid_rowconfigure(0, weight=1)

        left = ttk.Frame(ops_tab, padding=8)
        left.grid(row=0, column=0, sticky="nsew")
        left.grid_columnconfigure(0, weight=1)
        left.grid_rowconfigure(3, weight=1)

        right = ttk.Frame(ops_tab, padding=8)
        right.grid(row=0, column=1, sticky="nsew")
        right.grid_columnconfigure(0, weight=1)
        right.grid_rowconfigure(0, weight=1)
        right.grid_rowconfigure(1, weight=1)

        self._build_ops_left_panel(left)
        self._build_right_top_players(right)
        self._build_right_bottom_controls(right)
        self._build_left_bottom_map(left)
        self._build_menu_tab(menu_tab)

    def _build_ops_left_panel(self, parent: ttk.Frame) -> None:
        conn = ttk.LabelFrame(parent, text="Connection", padding=8)
        conn.grid(row=0, column=0, sticky="ew")
        conn.grid_columnconfigure(1, weight=1)

        ttk.Label(conn, text="Pipe").grid(row=0, column=0, sticky="w")
        ttk.Entry(conn, textvariable=self.pipe_name_var).grid(row=0, column=1, sticky="ew", padx=6)
        ttk.Button(conn, text="Connect", command=self.connect).grid(row=0, column=2, padx=2)
        ttk.Button(conn, text="Disconnect", command=self.disconnect).grid(row=0, column=3, padx=2)
        ttk.Label(conn, textvariable=self.connection_state_var).grid(row=1, column=0, columnspan=4, sticky="w", pady=(6, 0))

        chat = ttk.LabelFrame(parent, text="Chat", padding=8)
        chat.grid(row=1, column=0, sticky="ew", pady=(8, 0))
        chat.grid_columnconfigure(0, weight=1)
        ttk.Entry(chat, textvariable=self.chat_text_var).grid(row=0, column=0, sticky="ew", padx=(0, 6))
        ttk.Combobox(chat, textvariable=self.chat_scope_var, values=("players", "allies", "everyone"), width=10, state="readonly").grid(
            row=0, column=1, sticky="w", padx=(0, 6)
        )
        ttk.Button(chat, text="Send Chat", command=self._send_chat).grid(row=0, column=2, sticky="ew")

    def _build_menu_tab(self, parent: ttk.Frame) -> None:
        parent.grid_columnconfigure(0, weight=1)
        parent.grid_rowconfigure(1, weight=1)

        conn = ttk.LabelFrame(parent, text="Connection", padding=8)
        conn.grid(row=0, column=0, sticky="ew")
        conn.grid_columnconfigure(1, weight=1)

        ttk.Label(conn, text="Pipe").grid(row=0, column=0, sticky="w")
        ttk.Entry(conn, textvariable=self.pipe_name_var).grid(row=0, column=1, sticky="ew", padx=6)
        ttk.Button(conn, text="Connect", command=self.connect).grid(row=0, column=2, padx=2)
        ttk.Button(conn, text="Disconnect", command=self.disconnect).grid(row=0, column=3, padx=2)
        ttk.Label(conn, textvariable=self.connection_state_var).grid(row=1, column=0, columnspan=4, sticky="w", pady=(6, 0))

        nav = ttk.LabelFrame(parent, text="Menu / Start Game", padding=8)
        nav.grid(row=1, column=0, sticky="nsew", pady=(8, 0))
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
        ttk.Button(nav, text="Skirmish", command=lambda: self._menu_click(MAIN_MENU_ACTIONS["main_skirmish"])).grid(
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
        ttk.Button(nav, text="SOLO Back", command=self._go_solo_back).grid(row=5, column=1, sticky="ew", padx=2, pady=2)
        ttk.Button(nav, text="List Controls", command=self._list_controls).grid(row=6, column=0, sticky="ew", padx=2, pady=2)
        ttk.Button(nav, text="Query Players", command=lambda: self._query("game.players", quiet=False)).grid(
            row=6, column=1, sticky="ew", padx=2, pady=2
        )

    def _build_right_top_players(self, parent: ttk.Frame) -> None:
        players = ttk.LabelFrame(parent, text="Players", padding=8)
        players.grid(row=0, column=0, sticky="nsew")
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

    def _build_left_bottom_map(self, parent: ttk.Frame) -> None:
        map_frame = ttk.LabelFrame(parent, text="Map", padding=6)
        map_frame.grid(row=3, column=0, sticky="nsew", pady=(8, 0))
        map_frame.grid_rowconfigure(1, weight=1)
        map_frame.grid_columnconfigure(0, weight=1)
        toolbar = ttk.Frame(map_frame)
        toolbar.grid(row=0, column=0, sticky="ew", pady=(0, 4))
        ttk.Button(toolbar, text="Rotate/Flip Map", command=self._cycle_map_orientation).grid(row=0, column=0, sticky="w")
        ttk.Button(toolbar, text="Find Supplies", command=self._find_supply_sources).grid(row=0, column=1, sticky="w", padx=(6, 0))
        ttk.Label(toolbar, text="Cell").grid(row=0, column=2, sticky="e", padx=(10, 2))
        ttk.Entry(toolbar, textvariable=self.grid_cell_var, width=8).grid(row=0, column=3, sticky="w")
        ttk.Button(toolbar, text="Query Cell", command=self._query_selected_grid_cell).grid(row=0, column=4, sticky="w", padx=(6, 0))
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
        controls.grid_columnconfigure(0, weight=1)

        buildings = ttk.LabelFrame(controls, text="Buildings", padding=6)
        buildings.grid(row=0, column=0, sticky="ew")
        for i in range(4):
            buildings.grid_columnconfigure(i, weight=1)
        ttk.Label(buildings, text="Build Count (1-9)").grid(row=0, column=0, sticky="e")
        tk.Spinbox(buildings, from_=1, to=9, textvariable=self.build_count_var, width=5).grid(row=0, column=1, sticky="w")
        ttk.Button(buildings, text="Build Stash", command=self._build_stash).grid(
            row=0, column=2, sticky="ew", padx=2, pady=2
        )
        ttk.Button(buildings, text="Build Barracks", command=self._build_barracks).grid(
            row=0, column=3, sticky="ew", padx=2, pady=2
        )
        ttk.Button(buildings, text="Build Command", command=self._build_command_center).grid(
            row=1, column=0, sticky="ew", padx=2, pady=2
        )
        ttk.Button(buildings, text="Build Arms Dealer", command=self._build_arms_dealer).grid(
            row=1, column=1, sticky="ew", padx=2, pady=2
        )
        ttk.Button(buildings, text="Build Palace", command=self._build_palace).grid(
            row=1, column=2, sticky="ew", padx=2, pady=2
        )
        ttk.Button(buildings, text="Build Black Market", command=self._build_black_market).grid(
            row=1, column=3, sticky="ew", padx=2, pady=2
        )
        ttk.Button(buildings, text="Dozer Construct", command=self._dozer_construct_supply).grid(
            row=2, column=0, sticky="ew", padx=2, pady=2
        )
        ttk.Button(buildings, text="Build Tunnel", command=self._build_tunnel_network).grid(
            row=2, column=1, sticky="ew", padx=2, pady=2
        )
        ttk.Button(buildings, text="Build Stinger", command=self._build_stinger_site).grid(
            row=2, column=2, sticky="ew", padx=2, pady=2
        )
        ttk.Button(buildings, text="Build Scud Storm", command=self._build_scud_storm).grid(
            row=2, column=3, sticky="ew", padx=2, pady=2
        )
        mix = ttk.LabelFrame(buildings, text="Build Mix", padding=6)
        mix.grid(row=3, column=0, columnspan=4, sticky="ew", pady=(6, 0))
        for i in range(9):
            mix.grid_columnconfigure(i, weight=1)
        self._build_mix_spinbox(mix, 0, "Stash", self.build_mix_stash_var)
        self._build_mix_spinbox(mix, 1, "Barracks", self.build_mix_barracks_var)
        self._build_mix_spinbox(mix, 2, "Command", self.build_mix_command_var)
        self._build_mix_spinbox(mix, 3, "Arms", self.build_mix_arms_var)
        self._build_mix_spinbox(mix, 4, "Palace", self.build_mix_palace_var)
        self._build_mix_spinbox(mix, 5, "Markets", self.build_mix_market_var)
        self._build_mix_spinbox(mix, 6, "Scud", self.build_mix_scud_var)
        self._build_mix_spinbox(mix, 7, "Tunnels", self.build_mix_tunnel_var)
        self._build_mix_spinbox(mix, 8, "Stingers", self.build_mix_stinger_var)
        ttk.Button(mix, text="Send Build Mix", command=self._build_mix).grid(
            row=2, column=0, columnspan=9, sticky="ew", padx=2, pady=(6, 0)
        )

        units = ttk.LabelFrame(controls, text="Units", padding=6)
        units.grid(row=1, column=0, sticky="ew", pady=(8, 0))
        for i in range(4):
            units.grid_columnconfigure(i, weight=1)
        ttk.Label(units, text="Queue Count (1-9)").grid(row=0, column=0, sticky="e")
        tk.Spinbox(units, from_=1, to=9, textvariable=self.unit_queue_count_var, width=5).grid(row=0, column=1, sticky="w")
        ttk.Button(units, text="Build Worker (CC)", command=self._build_worker_command_center).grid(
            row=0, column=2, sticky="ew", padx=2, pady=2
        )
        ttk.Button(units, text="Build Worker (All Stashes)", command=self._build_worker_supply_stash).grid(
            row=0, column=3, sticky="ew", padx=2, pady=2
        )
        ttk.Button(units, text="Queue Soldiers", command=self._queue_soldiers_all_barracks).grid(
            row=1, column=0, sticky="ew", padx=2, pady=2
        )
        ttk.Button(units, text="Queue RPG", command=self._queue_rpg_troopers_all_barracks).grid(
            row=1, column=1, sticky="ew", padx=2, pady=2
        )
        ttk.Button(units, text="Queue Quads", command=self._queue_quads_all_war_factories).grid(
            row=1, column=2, sticky="ew", padx=2, pady=2
        )
        ttk.Button(units, text="Queue Scorpions", command=self._queue_scorpions_all_war_factories).grid(
            row=1, column=3, sticky="ew", padx=2, pady=2
        )
        ttk.Button(units, text="Queue Radar Van", command=self._queue_radar_van).grid(
            row=2, column=2, sticky="ew", padx=2, pady=2
        )

        actions = ttk.LabelFrame(controls, text="Actions", padding=6)
        actions.grid(row=2, column=0, sticky="ew", pady=(8, 0))
        for i in range(4):
            actions.grid_columnconfigure(i, weight=1)
        ttk.Label(actions, text="Target Player Idx").grid(row=0, column=0, sticky="e")
        tk.Spinbox(actions, from_=0, to=11, textvariable=self.target_player_index_var, width=5).grid(row=0, column=1, sticky="w")
        ttk.Button(actions, text="AttackMove -> Player", command=self._attackmove_all_combat_to_player).grid(
            row=0, column=2, sticky="ew", padx=2, pady=2
        )
        ttk.Button(actions, text="Scud -> Player", command=self._scud_storm_player).grid(
            row=0, column=3, sticky="ew", padx=2, pady=2
        )
        ttk.Label(actions, text="Raid Units").grid(row=1, column=0, sticky="e")
        tk.Spinbox(actions, from_=1, to=99, textvariable=self.raid_count_var, width=5).grid(row=1, column=1, sticky="w")
        ttk.Label(actions, text="Scud X").grid(row=1, column=2, sticky="e")
        ttk.Entry(actions, textvariable=self.scud_x_var, width=10).grid(row=1, column=3, sticky="ew", padx=2)
        ttk.Label(actions, text="Scud Y").grid(row=2, column=0, sticky="e")
        ttk.Entry(actions, textvariable=self.scud_y_var, width=10).grid(row=2, column=1, sticky="ew", padx=2)
        ttk.Button(actions, text="Raid Smart", command=self._raid_smart).grid(
            row=2, column=2, sticky="ew", padx=2, pady=2
        )
        ttk.Button(actions, text="Scud -> Position", command=self._scud_storm_position).grid(
            row=2, column=3, sticky="ew", padx=2, pady=2
        )
        ttk.Button(actions, text="Guard Idle", command=self._guard_idle_ground_combat).grid(
            row=3, column=0, sticky="ew", padx=2, pady=2
        )
        ttk.Button(actions, text="Query Objects", command=lambda: self._query("game.objects_all", quiet=False)).grid(
            row=3, column=1, sticky="ew", padx=2, pady=2
        )
        ttk.Button(actions, text="Query Enemies", command=lambda: self._query("game.visible_enemies", quiet=False)).grid(
            row=3, column=2, sticky="ew", padx=2, pady=2
        )
        debug_money = ttk.LabelFrame(actions, text="Debug Money", padding=6)
        debug_money.grid(row=4, column=0, columnspan=4, sticky="ew", pady=(6, 0))
        for i in range(6):
            debug_money.grid_columnconfigure(i, weight=1)
        ttk.Label(debug_money, text="Player Idx").grid(row=0, column=0, sticky="e")
        tk.Spinbox(debug_money, from_=0, to=11, textvariable=self.money_player_index_var, width=5).grid(row=0, column=1, sticky="w")
        ttk.Label(debug_money, text="Money").grid(row=0, column=2, sticky="e")
        ttk.Entry(debug_money, textvariable=self.money_value_var, width=16).grid(row=0, column=3, sticky="ew", padx=(0, 6))
        ttk.Button(debug_money, text="Set Money", command=self._set_money).grid(row=0, column=4, sticky="ew", padx=2)
        ttk.Label(debug_money, text="SP/Skirmish only").grid(row=0, column=5, sticky="w")

        upgrades = ttk.LabelFrame(actions, text="GLA Upgrades / Promotions", padding=6)
        upgrades.grid(row=5, column=0, columnspan=4, sticky="ew", pady=(6, 0))
        upgrades.grid_columnconfigure(1, weight=1)
        upgrades.grid_columnconfigure(3, weight=1)
        ttk.Label(upgrades, text="Promotion").grid(row=0, column=0, sticky="e")
        ttk.Combobox(upgrades, textvariable=self.promotion_science_var, values=GLA_SCIENCE_SUGGESTIONS).grid(
            row=0, column=1, sticky="ew", padx=(4, 6)
        )
        ttk.Button(upgrades, text="Buy Promotion", command=self._purchase_science).grid(row=0, column=2, sticky="ew", padx=2)
        ttk.Label(upgrades, text="Uses promotion points").grid(row=0, column=3, sticky="w")
        ttk.Label(upgrades, text="Palace").grid(row=1, column=0, sticky="e", pady=(6, 0))
        ttk.Combobox(upgrades, textvariable=self.palace_upgrade_var, values=GLA_PALACE_UPGRADE_SUGGESTIONS).grid(
            row=1, column=1, sticky="ew", padx=(4, 6), pady=(6, 0)
        )
        ttk.Button(upgrades, text="Queue Palace Upgrade", command=self._queue_palace_upgrade).grid(
            row=1, column=2, sticky="ew", padx=2, pady=(6, 0)
        )
        ttk.Label(upgrades, text="Black Market").grid(row=2, column=0, sticky="e", pady=(6, 0))
        ttk.Combobox(upgrades, textvariable=self.black_market_upgrade_var, values=GLA_BLACK_MARKET_UPGRADE_SUGGESTIONS).grid(
            row=2, column=1, sticky="ew", padx=(4, 6), pady=(6, 0)
        )
        ttk.Button(upgrades, text="Queue Market Upgrade", command=self._queue_black_market_upgrade).grid(
            row=2, column=2, sticky="ew", padx=2, pady=(6, 0)
        )

        ttk.Checkbutton(actions, text="Polling", variable=self.poll_enabled).grid(row=6, column=0, sticky="w")
        ttk.Label(actions, text="Interval ms").grid(row=6, column=1, sticky="e")
        ttk.Entry(actions, textvariable=self.poll_interval_ms, width=8).grid(row=6, column=2, sticky="w")
        ttk.Checkbutton(actions, text="Use Streaming", variable=self.stream_enabled, command=self._toggle_streaming).grid(
            row=6, column=3, sticky="w"
        )
        ttk.Checkbutton(
            actions,
            text="Map Updates",
            variable=self.map_updates_enabled,
            command=self._on_map_updates_toggle,
        ).grid(row=7, column=0, sticky="w")
        ttk.Checkbutton(actions, text="Debug Inbound", variable=self.debug_inbound).grid(row=7, column=1, sticky="w")

        autonomy = ttk.LabelFrame(controls, text="Autonomy", padding=6)
        autonomy.grid(row=3, column=0, sticky="ew", pady=(8, 0))
        for i in range(4):
            autonomy.grid_columnconfigure(i, weight=1)
        ttk.Label(autonomy, textvariable=self.autonomy_status_var).grid(row=0, column=0, columnspan=4, sticky="w")
        ttk.Label(autonomy, text="Profile").grid(row=1, column=0, sticky="e", pady=(6, 0))
        ttk.Combobox(
            autonomy,
            textvariable=self.autonomy_profile_var,
            values=("standard", "aggressive", "economic", "defensive", "tech", "sprawl", "builtin_passthrough"),
            state="readonly",
        ).grid(row=1, column=1, sticky="ew", padx=(4, 6), pady=(6, 0))
        ttk.Button(autonomy, text="Configure", command=self._autonomy_configure).grid(row=1, column=2, sticky="ew", padx=2, pady=(6, 0))
        ttk.Button(autonomy, text="Status", command=self._autonomy_status).grid(row=1, column=3, sticky="ew", padx=2, pady=(6, 0))
        ttk.Label(autonomy, text="Sprawl x").grid(row=2, column=0, sticky="e", pady=(6, 0))
        tk.Spinbox(
            autonomy,
            from_=0.5,
            to=10.0,
            increment=0.5,
            textvariable=self.sprawl_multiplier_var,
            width=8,
        ).grid(row=2, column=1, sticky="w", pady=(6, 0))
        ttk.Button(autonomy, text="Go Autonomous", command=self._autonomy_go).grid(row=3, column=0, sticky="ew", padx=2, pady=(6, 0))
        ttk.Button(autonomy, text="Pause", command=self._autonomy_pause).grid(row=3, column=1, sticky="ew", padx=2, pady=(6, 0))
        ttk.Button(autonomy, text="Resume", command=self._autonomy_resume).grid(row=3, column=2, sticky="ew", padx=2, pady=(6, 0))
        ttk.Button(autonomy, text="Manual", command=self._autonomy_manual).grid(row=3, column=3, sticky="ew", padx=2, pady=(6, 0))

        cmd_row = ttk.LabelFrame(bottom, text="Command Input", padding=8)
        cmd_row.grid(row=1, column=0, sticky="ew", pady=(6, 0))
        cmd_row.grid_columnconfigure(1, weight=1)
        ttk.Label(cmd_row, text="Query").grid(row=0, column=0, sticky="w")
        ttk.Combobox(
            cmd_row,
            textvariable=self.query_preset_var,
            values=(
                "game.status",
                "game.resources",
                "game.players",
                "game.grid",
                "game.grid_objects",
                "game.objects",
                "game.objects_all",
                "game.visible_enemies",
                "game.idle_workers",
                "game.unit_composition",
                "game.zone_counts",
                "game.objects_cache_status",
            ),
            state="readonly",
            width=24,
        ).grid(row=0, column=1, sticky="w", padx=(0, 6))
        ttk.Button(cmd_row, text="Run Query", command=self._run_query_preset).grid(row=0, column=2, padx=(0, 10))
        ttk.Button(cmd_row, text="Camera Get", command=self._camera_get).grid(row=0, column=3, padx=2)
        ttk.Button(cmd_row, text="Camera TopDown", command=self._camera_top_down).grid(row=0, column=4, padx=2)
        ttk.Button(cmd_row, text="Camera Reset", command=self._camera_reset).grid(row=0, column=5, padx=2)
        ttk.Button(cmd_row, text="Look At Player", command=self._camera_look_at_player).grid(row=0, column=6, padx=2)
        ttk.Label(cmd_row, text="Angle Deg").grid(row=1, column=0, sticky="e", pady=(6, 0))
        tk.Spinbox(cmd_row, from_=-360.0, to=360.0, increment=5.0, textvariable=self.camera_angle_deg_var, width=8).grid(
            row=1, column=1, sticky="w", pady=(6, 0)
        )
        ttk.Button(cmd_row, text="Set Angle", command=self._camera_set_angle).grid(row=1, column=2, padx=2, pady=(6, 0))
        ttk.Label(cmd_row, text="Zoom").grid(row=1, column=3, sticky="e", pady=(6, 0))
        ttk.Entry(cmd_row, textvariable=self.camera_zoom_var, width=8).grid(row=1, column=4, sticky="w", pady=(6, 0))
        ttk.Button(cmd_row, text="Set Zoom", command=self._camera_set_zoom).grid(row=1, column=5, padx=2, pady=(6, 0))
        ttk.Label(cmd_row, text="Height").grid(row=2, column=0, sticky="e", pady=(6, 0))
        tk.Spinbox(cmd_row, from_=50.0, to=5000.0, increment=25.0, textvariable=self.camera_height_var, width=8).grid(
            row=2, column=1, sticky="w", pady=(6, 0)
        )
        ttk.Button(cmd_row, text="Set Height", command=self._camera_set_height).grid(row=2, column=2, padx=2, pady=(6, 0))
        ttk.Checkbutton(
            cmd_row,
            text="Zoom Limited",
            variable=self.camera_zoom_limited_var,
            command=self._camera_set_zoom_limited,
        ).grid(row=2, column=3, columnspan=2, sticky="w", pady=(6, 0))
        ttk.Entry(cmd_row, textvariable=self.command_input_var).grid(row=3, column=0, columnspan=6, sticky="ew", padx=(0, 6), pady=(6, 0))
        ttk.Button(cmd_row, text="Send", command=self._send_command_input).grid(row=3, column=6, pady=(6, 0))

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

    def _go_solo_back(self) -> None:
        self._log("Attempting SOLO Back (single-player back variants).")
        for control_id in SOLO_BACK_CONTROL_IDS:
            self._menu_click(control_id)

    def _clamp_count_var(self, variable: tk.Variable, minimum: int = 1, maximum: int = 9) -> int:
        try:
            count = int(variable.get())
        except Exception:  # noqa: BLE001
            count = minimum
        return max(minimum, min(maximum, count))

    def _get_build_count(self) -> int:
        return self._clamp_count_var(self.build_count_var)

    def _get_unit_queue_count(self) -> int:
        return self._clamp_count_var(self.unit_queue_count_var)

    def _get_raid_count(self) -> int:
        return self._clamp_count_var(self.raid_count_var, minimum=1, maximum=99)

    def _queue_soldiers_all_barracks(self) -> None:
        count = self._get_unit_queue_count()
        self._send_session_command("Game.QueueSoldiersAllBarracks", {"count": count})

    def _queue_quads_all_war_factories(self) -> None:
        count = self._get_unit_queue_count()
        self._send_session_command("Game.QueueQuadsAllWarFactories", {"count": count})

    def _queue_rpg_troopers_all_barracks(self) -> None:
        count = self._get_unit_queue_count()
        self._send_session_command("Game.QueueRpgTroopersAllBarracks", {"count": count})

    def _queue_scorpions_all_war_factories(self) -> None:
        count = self._get_unit_queue_count()
        self._send_session_command("Game.QueueScorpionsAllWarFactories", {"count": count})

    def _build_worker_command_center(self) -> None:
        count = self._get_unit_queue_count()
        self._send_session_command("Game.BuildWorker", {"producer_kind": "command_center", "count": count})

    def _build_worker_supply_stash(self) -> None:
        count = self._get_unit_queue_count()
        self._send_session_command("Game.BuildWorker", {"producer_kind": "supply_stash", "count": count})

    def _build_stash(self) -> None:
        self._send_session_command("Game.BuildSupplyStashSmart", {"count": self._get_build_count()})

    def _build_barracks(self) -> None:
        self._send_session_command("Game.BuildBarracksSmart", {"count": self._get_build_count()})

    def _build_command_center(self) -> None:
        self._send_session_command("Game.BuildCommandCenterSmart", {"count": self._get_build_count()})

    def _build_arms_dealer(self) -> None:
        self._send_session_command("Game.BuildArmsDealerSmart", {"count": self._get_build_count()})

    def _build_palace(self) -> None:
        self._send_session_command("Game.BuildPalaceSmart", {"count": self._get_build_count()})

    def _build_black_market(self) -> None:
        self._send_session_command("Game.BuildBlackMarketSmart", {"count": self._get_build_count()})

    def _build_scud_storm(self) -> None:
        self._send_session_command("Game.BuildScudStormSmart", {"count": self._get_build_count()})

    def _build_tunnel_network(self) -> None:
        self._send_session_command(
            "Game.BuildBarracksSmart",
            {"count": self._get_build_count(), "building_template": "GLATunnelNetwork"},
        )

    def _build_stinger_site(self) -> None:
        self._send_session_command(
            "Game.BuildBarracksSmart",
            {"count": self._get_build_count(), "building_template": "GLAStingerSite"},
        )

    def _attackmove_all_combat_to_player(self) -> None:
        try:
            target = int(self.target_player_index_var.get())
        except Exception:  # noqa: BLE001
            target = 0
        target = max(0, target)
        self._send_session_command("Game.AttackMoveAllCombatToPlayer", {"target_player_index": target})

    def _queue_radar_van(self) -> None:
        self._send_session_command("Game.QueueRadarVan", {})

    def _set_money(self) -> None:
        try:
            player_index = int(self.money_player_index_var.get())
        except Exception:  # noqa: BLE001
            self._log("Invalid money player index.")
            return
        if player_index < 0:
            self._log("Money player index must be >= 0.")
            return

        raw_value = self.money_value_var.get().strip()
        if not raw_value:
            self._log("Money value is empty.")
            return
        try:
            money = int(raw_value, 10)
        except Exception:  # noqa: BLE001
            self._log("Money value must be an integer.")
            return
        if money < 0:
            self._log("Money value must be >= 0.")
            return

        max_money = 0xFFFFFFFF
        if money > max_money:
            self._log(f"Money value exceeds engine max; clamping to {max_money}.")
            money = max_money

        self._send_session_command("Game.SetMoney", {"player_index": player_index, "money": money}, quiet=False)

    def _purchase_science(self) -> None:
        science_name = self.promotion_science_var.get().strip()
        if not science_name:
            self._log("Science name is empty.")
            return
        self._send_session_command("Game.PurchaseScience", {"science_name": science_name}, quiet=False)

    def _queue_upgrade(self, producer_kind: str, upgrade_name: str) -> None:
        upgrade_name = upgrade_name.strip()
        if not upgrade_name:
            self._log("Upgrade name is empty.")
            return
        self._send_session_command(
            "Game.QueueUpgrade",
            {"producer_kind": producer_kind, "upgrade_name": upgrade_name},
            quiet=False,
        )

    def _queue_palace_upgrade(self) -> None:
        self._queue_upgrade("palace", self.palace_upgrade_var.get())

    def _queue_black_market_upgrade(self) -> None:
        self._queue_upgrade("black_market", self.black_market_upgrade_var.get())

    def _scud_storm_player(self) -> None:
        try:
            target = int(self.target_player_index_var.get())
        except Exception:  # noqa: BLE001
            target = 0
        target = max(0, target)
        self._send_session_command("Game.ScudStormAtPlayer", {"target_player_index": target}, quiet=False)

    def _scud_storm_position(self) -> None:
        try:
            x = float(self.scud_x_var.get().strip())
            y = float(self.scud_y_var.get().strip())
        except Exception:  # noqa: BLE001
            self._log("Scud target position must be numeric.")
            return
        self._send_session_command("Game.ScudStormAtPosition", {"x": x, "y": y}, quiet=False)

    def _find_supply_sources(self) -> None:
        self._send_session_command("Game.FindSupplySources", {}, quiet=False)

    def _dozer_construct_supply(self) -> None:
        self._send_session_command("Game.DozerConstruct", {}, quiet=False)

    def _raid_smart(self) -> None:
        count = self._get_raid_count()
        self._send_session_command("Game.AttackMove.RaidSmart", {"min_units": max(1, count), "group_size": max(1, count)})

    def _guard_idle_ground_combat(self) -> None:
        self._send_session_command("Game.GuardAllIdleGroundCombat", {})

    def _build_mix_spinbox(self, parent: ttk.Frame, column: int, label: str, variable: tk.IntVar) -> None:
        ttk.Label(parent, text=label).grid(row=0, column=column, sticky="s")
        tk.Spinbox(parent, from_=0, to=9, textvariable=variable, width=5).grid(row=1, column=column, sticky="n")

    def _build_mix(self) -> None:
        buildings: list[dict[str, Any]] = []

        def add(kind: str, value: tk.IntVar) -> None:
            try:
                count = int(value.get())
            except Exception:  # noqa: BLE001
                count = 0
            count = max(0, min(9, count))
            if count > 0:
                buildings.append({"kind": kind, "count": count})

        def add_explicit(item: dict[str, Any], value: tk.IntVar) -> None:
            try:
                count = int(value.get())
            except Exception:  # noqa: BLE001
                count = 0
            count = max(0, min(9, count))
            if count > 0:
                payload = dict(item)
                payload["count"] = count
                buildings.append(payload)

        add("supply_stash", self.build_mix_stash_var)
        add("barracks", self.build_mix_barracks_var)
        add("command_center", self.build_mix_command_var)
        add("arms_dealer", self.build_mix_arms_var)
        add("palace", self.build_mix_palace_var)
        add("black_market", self.build_mix_market_var)
        add("scud_storm", self.build_mix_scud_var)
        add_explicit({"cmd": "Game.BuildBarracksSmart", "building_template": "GLATunnelNetwork"}, self.build_mix_tunnel_var)
        add_explicit({"cmd": "Game.BuildBarracksSmart", "building_template": "GLAStingerSite"}, self.build_mix_stinger_var)

        if not buildings:
            self._log("Build mix is empty.")
            return

        self._send_session_command("Game.BuildBuildingMix", {"buildings": buildings})

    def _send_chat(self) -> None:
        text = self.chat_text_var.get().strip()
        if not text:
            self._log("Chat text is empty.")
            return
        self._send_session_command("Chat.Send", {"text": text, "scope": self.chat_scope_var.get() or "everyone"})

    def _list_controls(self) -> None:
        self._send_session_command("Menu.ListControls", {"kind": "all", "include_hidden": False}, quiet=False)

    def _run_query_preset(self) -> None:
        path = self.query_preset_var.get().strip()
        if not path:
            return
        self._query(path, quiet=False)

    def _camera_get(self) -> None:
        self._send_session_command("Game.Camera.Get", {}, quiet=False)

    def _camera_top_down(self) -> None:
        self._send_session_command("Game.Camera.Set", {"top_down": True, "angle": 0.0}, quiet=False)

    def _camera_reset(self) -> None:
        self._send_session_command("Game.Camera.Reset", {}, quiet=False)

    def _camera_set_angle(self) -> None:
        try:
            angle_deg = float(self.camera_angle_deg_var.get())
        except Exception:  # noqa: BLE001
            self._log("Invalid camera angle.")
            return
        self._send_session_command("Game.Camera.Set", {"angle": angle_deg * (3.141592653589793 / 180.0)}, quiet=False)

    def _camera_set_zoom(self) -> None:
        try:
            zoom = float(self.camera_zoom_var.get())
        except Exception:  # noqa: BLE001
            self._log("Invalid camera zoom.")
            return
        if zoom <= 0.0:
            self._log("Camera zoom must be > 0.")
            return
        self._send_session_command("Game.Camera.Set", {"zoom": zoom}, quiet=False)

    def _camera_set_height(self) -> None:
        try:
            height = float(self.camera_height_var.get())
        except Exception:  # noqa: BLE001
            self._log("Invalid camera height.")
            return
        if height <= 0.0:
            self._log("Camera height must be > 0.")
            return
        self._send_session_command("Game.Camera.Set", {"height": height}, quiet=False)

    def _camera_set_zoom_limited(self) -> None:
        self._send_session_command(
            "Game.Camera.SetZoomLimited",
            {"enabled": bool(self.camera_zoom_limited_var.get())},
            quiet=False,
        )

    def _camera_look_at_player(self) -> None:
        try:
            target = int(self.target_player_index_var.get())
        except Exception:  # noqa: BLE001
            target = 0
        meta = self.store.players.get(target)
        if meta is None or meta.map_position is None:
            self._log(f"No known map position for player {target}.")
            return
        self._send_session_command(
            "Game.Camera.LookAt",
            {"x": float(meta.map_position[0]), "y": float(meta.map_position[1])},
            quiet=False,
        )

    def _autonomy_config_payload(self) -> dict[str, Any]:
        payload: dict[str, Any] = {"profile": self.autonomy_profile_var.get().strip() or "standard"}
        try:
            sprawl_multiplier = float(self.sprawl_multiplier_var.get())
        except Exception:  # noqa: BLE001
            sprawl_multiplier = 1.0
        payload["sprawl_multiplier"] = max(0.5, min(10.0, sprawl_multiplier))
        try:
            target = int(self.target_player_index_var.get())
        except Exception:  # noqa: BLE001
            target = -1
        if target >= 0:
            payload["target_player_index"] = target
        return payload

    def _autonomy_config_signature(self) -> tuple[tuple[str, Any], ...]:
        payload = self._autonomy_config_payload()
        return tuple(sorted(payload.items()))

    def _autonomy_configure(self) -> None:
        payload = self._autonomy_config_payload()
        self._last_autonomy_config_signature = tuple(sorted(payload.items()))
        self._send_session_command("Autonomy.Configure", payload, quiet=False)

    def _autonomy_status(self) -> None:
        self._send_session_command("Autonomy.Status", {}, quiet=False)

    def _autonomy_go(self) -> None:
        self.poll_enabled.set(False)
        self._suspend_poll_until_monotonic = time.monotonic() + 10.0
        self._autonomy_mode = "autonomous"
        self.autonomy_status_var.set(f"Autonomy: autonomous ({self.autonomy_profile_var.get().strip() or 'standard'})")
        payload = self._autonomy_config_payload()
        self._last_autonomy_config_signature = tuple(sorted(payload.items()))
        self._send_session_command("Autonomy.Configure", payload, quiet=True)
        self._send_session_command("Autonomy.SetMode", {"mode": "autonomous"}, quiet=False)
        self._send_session_command("Autonomy.Status", {}, quiet=True)

    def _autonomy_pause(self) -> None:
        self._send_session_command("Autonomy.Pause", {}, quiet=False)
        self._send_session_command("Autonomy.Status", {}, quiet=True)

    def _autonomy_resume(self) -> None:
        self._send_session_command("Autonomy.Resume", {}, quiet=False)
        self._send_session_command("Autonomy.Status", {}, quiet=True)

    def _autonomy_manual(self) -> None:
        self.poll_enabled.set(True)
        self._autonomy_mode = "manual"
        self.autonomy_status_var.set("Autonomy: manual")
        self._send_session_command("Autonomy.SetMode", {"mode": "manual"}, quiet=False)
        self._send_session_command("Autonomy.Status", {}, quiet=True)

    def _on_sprawl_multiplier_changed(self, *_args: object) -> None:
        if self._suppress_autonomy_control_updates:
            return
        if self._autonomy_mode not in ("autonomous", "hybrid"):
            return
        if self._autonomy_config_after_id is not None:
            try:
                self.root.after_cancel(self._autonomy_config_after_id)
            except Exception:  # noqa: BLE001
                pass
        self._autonomy_config_after_id = self.root.after(250, self._send_live_autonomy_config)

    def _send_live_autonomy_config(self) -> None:
        self._autonomy_config_after_id = None
        if self._autonomy_mode not in ("autonomous", "hybrid"):
            return
        payload = self._autonomy_config_payload()
        signature = tuple(sorted(payload.items()))
        if signature == self._last_autonomy_config_signature:
            return
        self._last_autonomy_config_signature = signature
        self._send_session_command("Autonomy.Configure", payload, quiet=True)
        self._send_session_command("Autonomy.Status", {}, quiet=True)

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
            timeout_ms: int | None = None
            if cmd == "Game.Query":
                # Keep map polling responsive when a query path stalls on the adapter side.
                path = str(args.get("path", ""))
                if path in ("game.grid", "game.grid_objects"):
                    timeout_ms = 1500
                else:
                    timeout_ms = 4000 if path == "game.objects_all_map" else 2500
            self._request_async(msg, priority=priority, timeout_ms=timeout_ms)
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

    def _query(self, path: str, args: dict[str, Any] | None = None, quiet: bool = True) -> None:
        payload: dict[str, Any] = {"path": path}
        if args:
            payload.update(args)
        self._send_session_command("Game.Query", payload, quiet=quiet)

    def _query_selected_grid_cell(self) -> None:
        cell = self.grid_cell_var.get().strip().upper()
        if not cell:
            self._log("Grid cell is empty.")
            return
        self.store.selected_grid_cell = cell
        self._dirty_view = True
        self._query("game.grid_objects", {"cell": cell}, quiet=False)

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
                    path = self._next_poll_path()
                    if not path:
                        return
                    if path in ("game.players", "game.resources", "game.status", "game.grid_objects") and now < self._next_meta_poll_monotonic:
                        pass
                    else:
                        query_args: dict[str, Any] | None = None
                        if path == "game.grid_objects":
                            cell = self.grid_cell_var.get().strip().upper()
                            if not cell:
                                return
                            query_args = {"cell": cell}
                            self.store.selected_grid_cell = cell
                        self._query(path, args=query_args, quiet=True)
                        if path in ("game.players", "game.resources", "game.status", "game.grid_objects"):
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
                if self.map_updates_enabled.get():
                    self.redraw_map()
                self._dirty_view = False
                self._last_redraw_monotonic = now
            elif (
                self.map_updates_enabled.get()
                and self.map_renderer is not None
                and self.map_renderer.has_active_animation()
                and now - self._last_redraw_monotonic >= 0.033
            ):
                self.redraw_map()
                self._last_redraw_monotonic = now
            elif (
                self.map_renderer is not None
                and self.map_renderer.has_active_animation()
                and now - self._last_redraw_monotonic >= 0.033
            ):
                # Keep consuming animation state while map updates are disabled.
                pass
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
            if ok:
                # Prime the player table and top-level metadata immediately instead of
                # waiting for the background poll rotation to reach these paths.
                self._query("game.players", quiet=True)
                self._query("game.resources", quiet=True)
                self._query("game.status", quiet=True)
                self._query("game.grid", quiet=True)
                cell = self.grid_cell_var.get().strip().upper()
                if cell:
                    self.store.selected_grid_cell = cell
                    self._query("game.grid_objects", {"cell": cell}, quiet=True)
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
            elif msg_type == "QueryResult":
                self._handle_command_query_result(cmd, msg)
        else:
            if msg_type == "QueryResult":
                # Late/unmatched query replies can still carry useful state updates.
                self._apply_query_result(msg, "")
            elif msg_type == "ActionAck":
                pass

    def _handle_command_query_result(self, cmd: str, msg: dict[str, Any]) -> None:
        payload = self._extract_payload(msg)
        if cmd == "Menu.ListControls" and isinstance(payload, dict):
            count = payload.get("count")
            self._log(f"{cmd} count={count}")
        elif cmd == "Game.FindSupplySources" and isinstance(payload, dict):
            count = payload.get("count")
            self.store.update_supply_sources(payload)
            self._dirty_view = True
            self.redraw_map()
            self._log(f"{cmd} count={count}")
        elif cmd == "Game.Camera.Get" and isinstance(payload, dict):
            x = payload.get("x")
            y = payload.get("y")
            zoom = payload.get("zoom")
            pitch = payload.get("pitch")
            angle = payload.get("angle")
            height = payload.get("height_above_ground")
            zoom_limited = payload.get("zoom_limited")
            if isinstance(angle, (int, float)):
                self.camera_angle_deg_var.set(float(angle) * (180.0 / 3.141592653589793))
            if isinstance(zoom, (int, float)):
                self.camera_zoom_var.set(float(zoom))
            if isinstance(height, (int, float)):
                self.camera_height_var.set(float(height))
            if isinstance(zoom_limited, bool):
                self.camera_zoom_limited_var.set(zoom_limited)
            self._log(f"{cmd} x={x} y={y} zoom={zoom} pitch={pitch} height={height} zoom_limited={zoom_limited}")
        elif cmd == "Autonomy.Status" and isinstance(payload, dict):
            mode = str(payload.get("mode", "manual"))
            profile = str(payload.get("profile", "standard"))
            paused = bool(payload.get("paused", False))
            sprawl_multiplier = payload.get("sprawl_multiplier")
            selected_zone = payload.get("selected_zone") if isinstance(payload.get("selected_zone"), dict) else {}
            last_decision = payload.get("last_decision") if isinstance(payload.get("last_decision"), dict) else {}
            self._autonomy_mode = mode
            money = payload.get("money")
            assets = payload.get("assets") if isinstance(payload.get("assets"), dict) else {}
            units = assets.get("units", "-")
            buildings = assets.get("buildings", "-")
            workers = assets.get("workers", "-")
            zone_anchor = selected_zone.get("anchor_id", "-")
            zone_main = bool(selected_zone.get("is_main_base", False))
            zone_x = selected_zone.get("center_x", "-")
            zone_y = selected_zone.get("center_y", "-")
            decision_category = last_decision.get("category", "")
            decision_command = last_decision.get("command", "")
            decision_reason = last_decision.get("reason", "")
            suffix = " paused" if paused else ""
            if isinstance(sprawl_multiplier, (int, float)):
                remote_multiplier = float(sprawl_multiplier)
                try:
                    current_multiplier = float(self.sprawl_multiplier_var.get())
                except Exception:  # noqa: BLE001
                    current_multiplier = remote_multiplier
                if abs(current_multiplier - remote_multiplier) > 1e-6:
                    self._suppress_autonomy_control_updates = True
                    try:
                        self.sprawl_multiplier_var.set(remote_multiplier)
                    finally:
                        self._suppress_autonomy_control_updates = False
            self._last_autonomy_config_signature = self._autonomy_config_signature()
            self.autonomy_status_var.set(f"Autonomy: {mode}/{profile} x{float(sprawl_multiplier) if isinstance(sprawl_multiplier, (int, float)) else 1.0:g}{suffix}")
            self._log(
                f"{cmd} mode={mode} profile={profile} sprawl_multiplier={sprawl_multiplier} paused={paused} "
                f"money={money} units={units} buildings={buildings} workers={workers} "
                f"zone_anchor={zone_anchor} zone_main={zone_main} zone=({zone_x},{zone_y}) "
                f"last_decision={decision_category}/{decision_command} reason={decision_reason}"
            )
        elif isinstance(payload, dict):
            keys = ", ".join(sorted(payload.keys())[:8])
            self._log(f"{cmd} keys={keys}")
        elif isinstance(payload, list):
            self._log(f"{cmd} rows={len(payload)}")
        else:
            self._log(f"{cmd} payload={payload}")

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
        if path == "game.objects" or path == "game.objects_map":
            if isinstance(payload, dict):
                units_n = len(payload.get("units", [])) if isinstance(payload.get("units"), list) else 0
                bld_n = len(payload.get("buildings", [])) if isinstance(payload.get("buildings"), list) else 0
                self._log(f"{path} counts units={units_n} buildings={bld_n}")
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
                    self._log(f"{path} extents x=[{min_x:.1f},{max_x:.1f}] y=[{min_y:.1f},{max_y:.1f}]")
            self.store.update_owned_objects(payload)
        elif path == "game.objects_all" or path == "game.objects_all_map":
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
                self._log(f"{path} counts units={total_units} buildings={total_buildings}")
            self.store.update_objects_all(payload)
        elif path == "game.visible_enemies":
            self.store.update_visible_enemies(payload)
        elif path == "game.players":
            self.store.update_players(payload)
        elif path == "game.resources":
            self.store.update_resources(payload)
        elif path == "game.status":
            self.store.update_map_metadata(payload)
        elif path == "game.grid":
            self.store.update_grid_summary(payload)
            self.store.update_map_metadata(payload)
        elif path == "game.grid_objects":
            self.store.update_grid_objects(payload)
        else:
            if isinstance(payload, dict):
                # Opportunistic parsing for mixed aggregate payloads.
                self.store.update_owned_objects(payload)
                self.store.update_objects_all(payload)
                self.store.update_visible_enemies(payload)
                self.store.update_players(payload)
                self.store.update_resources(payload)
                self.store.update_map_metadata(payload)
                self.store.update_grid_summary(payload)
                self.store.update_grid_objects(payload)
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
                        payload_path = payload.get("path")
                        if payload_path == "game.objects_all_map":
                            return "game.objects_all_map"
                        return "game.objects_all"
            if "buildings" in payload and "units" in payload:
                return "game.objects"
            if payload.get("path") == "game.objects_map":
                return "game.objects_map"
            if payload.get("path") == "game.objects_all_map":
                return "game.objects_all_map"
            if payload.get("path") == "game.grid":
                return "game.grid"
            if payload.get("path") == "game.grid_objects":
                return "game.grid_objects"
            if "enemies" in payload:
                return "game.visible_enemies"
            if "visible_enemies" in payload:
                return "game.visible_enemies"
            if "occupied_cell_count" in payload and "cells" in payload:
                return "game.grid"
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
        if self.map_renderer is not None and self.map_updates_enabled.get():
            self.map_renderer.draw(self.store)

    def _on_map_updates_toggle(self) -> None:
        if self.map_updates_enabled.get():
            self._log("Map updates enabled.")
            self._dirty_view = True
            self.redraw_map()
        else:
            self._log("Map updates disabled.")

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

    def _request_async(self, payload: dict[str, Any], priority: int = 0, timeout_ms: int | None = None) -> None:
        self._request_seq += 1
        self._request_queue.put((priority, self._request_seq, payload, timeout_ms))

    def _start_request_worker(self) -> None:
        if self._request_worker_started:
            return
        self._request_worker_started = True

        def worker() -> None:
            while True:
                _priority, _seq, payload, timeout_ms = self._request_queue.get()
                try:
                    response = self.client.request_once(payload, timeout_ms=timeout_ms)
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

    def _next_poll_path(self) -> str:
        if not self._poll_paths:
            return ""
        if not self.store.players:
            return "game.players"
        map_paths = {"game.grid", "game.grid_objects", "game.objects_map", "game.objects_all_map"}
        for _ in range(len(self._poll_paths)):
            path = self._poll_paths[self._poll_path_index % len(self._poll_paths)]
            self._poll_path_index += 1
            if not self.map_updates_enabled.get() and path in map_paths:
                continue
            if path == "game.grid_objects" and not self.grid_cell_var.get().strip():
                continue
            return path
        return ""
