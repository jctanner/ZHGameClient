#!/usr/bin/env python3
"""Minimal controller CLI for Zero Hour AI adapter MVP."""

from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wt
import json
import os
import subprocess
import sys
import time
import uuid
from dataclasses import dataclass
from typing import Any


PIPE_ACCESS_DUPLEX = 0xC0000000
OPEN_EXISTING = 3
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
ERROR_SEM_TIMEOUT = 121


class WinApiError(RuntimeError):
    """Raised when a Win32 API call fails."""


def _require_windows() -> None:
    if os.name != "nt":
        raise RuntimeError("zhctl.py currently supports Windows only.")


def _new_request_id() -> str:
    return uuid.uuid4().hex


def _compact_json(obj: Any) -> str:
    return json.dumps(obj, separators=(",", ":"))


def _print_json(obj: Any, pretty: bool) -> None:
    if pretty:
        print(json.dumps(obj, indent=2))
    else:
        print(_compact_json(obj))


@dataclass
class PipeConnection:
    stream: Any

    def close(self) -> None:
        self.stream.close()

    def write_json_line(self, payload: dict[str, Any]) -> None:
        self.stream.write((_compact_json(payload) + "\n").encode("utf-8"))
        self.stream.flush()

    def read_line_with_timeout(self, timeout_ms: int) -> str:
        deadline = time.monotonic() + (timeout_ms / 1000.0)
        while time.monotonic() < deadline:
            line = self.stream.readline()
            if line:
                return line.decode("utf-8", errors="replace").rstrip("\r\n")
            time.sleep(0.01)
        raise TimeoutError(f"Timed out waiting for adapter response ({timeout_ms}ms).")


def open_pipe(pipe_name: str, timeout_ms: int) -> PipeConnection:
    _require_windows()
    pipe_path = fr"\\.\pipe\{pipe_name}"

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

    kernel32.WaitNamedPipeW.argtypes = [wt.LPCWSTR, wt.DWORD]
    kernel32.WaitNamedPipeW.restype = wt.BOOL
    kernel32.CreateFileW.argtypes = [
        wt.LPCWSTR,
        wt.DWORD,
        wt.DWORD,
        wt.LPVOID,
        wt.DWORD,
        wt.DWORD,
        wt.HANDLE,
    ]
    kernel32.CreateFileW.restype = wt.HANDLE

    wait_ok = kernel32.WaitNamedPipeW(pipe_path, wt.DWORD(timeout_ms))
    if not wait_ok:
        code = ctypes.get_last_error()
        if code != ERROR_SEM_TIMEOUT:
            raise WinApiError(f"WaitNamedPipe failed for {pipe_path} (winerr={code}).")

    handle = kernel32.CreateFileW(
        pipe_path,
        PIPE_ACCESS_DUPLEX,
        0,
        None,
        OPEN_EXISTING,
        0,
        None,
    )

    if handle == INVALID_HANDLE_VALUE:
        code = ctypes.get_last_error()
        raise WinApiError(f"CreateFile failed for {pipe_path} (winerr={code}).")

    fd = msvcrt_open_osfhandle(handle)
    stream = os.fdopen(fd, "r+b", buffering=0)
    return PipeConnection(stream=stream)


def msvcrt_open_osfhandle(handle: int) -> int:
    import msvcrt  # Local import keeps module importable on non-Windows.

    return msvcrt.open_osfhandle(handle, 0)


def send_protocol_request(conn: PipeConnection, message: dict[str, Any], timeout_ms: int) -> dict[str, Any]:
    conn.write_json_line(message)
    deadline = time.monotonic() + (timeout_ms / 1000.0)
    while True:
        remaining_ms = int((deadline - time.monotonic()) * 1000)
        if remaining_ms <= 0:
            raise TimeoutError(f"Timed out waiting for reply request_id={message['request_id']}.")

        line = conn.read_line_with_timeout(remaining_ms)
        if not line.strip():
            continue
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            continue
        if obj.get("request_id") == message["request_id"]:
            return obj


def send_hello(conn: PipeConnection, timeout_ms: int) -> dict[str, Any]:
    req = {
        "type": "Hello",
        "request_id": _new_request_id(),
        "protocol": "zh-ai-control-v1",
        "client_name": "zhctl.py",
        "client_version": "0.1.0",
    }
    return send_protocol_request(conn, req, timeout_ms)


def send_session_command(conn: PipeConnection, cmd: str, args: dict[str, Any], timeout_ms: int) -> dict[str, Any]:
    req = {
        "type": "SessionCommand",
        "request_id": _new_request_id(),
        "cmd": cmd,
        "args": args,
    }
    return send_protocol_request(conn, req, timeout_ms)


def ensure_success_ack(response: dict[str, Any]) -> None:
    if response.get("ok") is True:
        return
    code = response.get("code", "unknown")
    reason = response.get("reason", "request failed")
    raise RuntimeError(f"Adapter rejected request: code={code} reason={reason}")


def cmd_launch(args: argparse.Namespace) -> int:
    if not args.exe_path:
        raise ValueError("--exe-path is required for command 'launch'.")
    if not os.path.exists(args.exe_path):
        raise FileNotFoundError(f"Executable not found: {args.exe_path}")

    proc = subprocess.Popen([args.exe_path, *args.exe_args])
    print(f"launched pid={proc.pid} path={args.exe_path}")
    return 0


def cmd_hello(args: argparse.Namespace) -> int:
    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        resp = send_hello(conn, args.timeout_ms)
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_send(args: argparse.Namespace) -> int:
    if not args.session_cmd:
        raise ValueError("--session-cmd is required for command 'send'.")
    try:
        args_obj = json.loads(args.args_json)
    except json.JSONDecodeError as exc:
        raise ValueError("--args-json must be a valid JSON object.") from exc
    if not isinstance(args_obj, dict):
        raise ValueError("--args-json must decode to an object.")

    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        send_hello(conn, args.timeout_ms)
        resp = send_session_command(conn, args.session_cmd, args_obj, args.timeout_ms)
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_menu_click(args: argparse.Namespace) -> int:
    if not args.control_id:
        raise ValueError("--control-id is required for command 'menu-click'.")
    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        send_hello(conn, args.timeout_ms)
        resp = send_session_command(conn, "Menu.Click", {"controlId": args.control_id}, args.timeout_ms)
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_menu_set_text(args: argparse.Namespace) -> int:
    if not args.control_id:
        raise ValueError("--control-id is required for command 'menu-set-text'.")
    if args.text is None:
        raise ValueError("--text is required for command 'menu-set-text'.")
    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        send_hello(conn, args.timeout_ms)
        resp = send_session_command(
            conn,
            "Menu.SetText",
            {"controlId": args.control_id, "text": args.text},
            args.timeout_ms,
        )
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_lan_click(args: argparse.Namespace) -> int:
    action_map = {
        "main-menu": "LanLobbyMenu.wnd:ButtonBack",
        "create-game": "LanLobbyMenu.wnd:ButtonHost",
        "join-game": "LanLobbyMenu.wnd:ButtonJoin",
        "direct-connect": "LanLobbyMenu.wnd:ButtonDirectConnect",
    }
    control_id = action_map[args.lan_action]
    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        send_hello(conn, args.timeout_ms)
        resp = send_session_command(conn, "Menu.Click", {"controlId": control_id}, args.timeout_ms)
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_main_click(args: argparse.Namespace) -> int:
    action_map = {
        "skirmish": "MainMenu.wnd:ButtonSkirmish",
        "multiplayer": "MainMenu.wnd:ButtonMultiplayer",
        "singleplayer": "MainMenu.wnd:ButtonSinglePlayer",
        "soloplay": "MainMenu.wnd:ButtonSinglePlayer",
        "solo-play": "MainMenu.wnd:ButtonSinglePlayer",
    }
    control_id = action_map[args.main_action]
    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        send_hello(conn, args.timeout_ms)
        resp = send_session_command(conn, "Menu.Click", {"controlId": control_id}, args.timeout_ms)
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_lan_name_set(args: argparse.Namespace) -> int:
    if args.text is None:
        raise ValueError("--text is required for command 'lan-name-set'.")
    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        send_hello(conn, args.timeout_ms)
        resp = send_session_command(
            conn,
            "Menu.SetText",
            {"controlId": "LanLobbyMenu.wnd:TextEntryPlayerName", "text": args.text},
            args.timeout_ms,
        )
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_list_controls(args: argparse.Namespace) -> int:
    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        send_hello(conn, args.timeout_ms)
        req_args: dict[str, Any] = {
            "kind": args.kind,
            "include_hidden": args.include_hidden,
        }
        resp = send_session_command(conn, "Menu.ListControls", req_args, args.timeout_ms)
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_chat_send(args: argparse.Namespace) -> int:
    if args.text is None or args.text == "":
        raise ValueError("--text is required for command 'chat-send'.")
    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        send_hello(conn, args.timeout_ms)
        resp = send_session_command(
            conn,
            "Chat.Send",
            {"text": args.text, "scope": args.scope},
            args.timeout_ms,
        )
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_query(args: argparse.Namespace) -> int:
    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        send_hello(conn, args.timeout_ms)
        req_args: dict[str, Any] = {"path": args.path}
        if args.player_index is not None:
            req_args["player_index"] = args.player_index
        resp = send_session_command(
            conn,
            "Game.Query",
            req_args,
            args.timeout_ms,
        )
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_status(args: argparse.Namespace) -> int:
    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        send_hello(conn, args.timeout_ms)
        req_args: dict[str, Any] = {"path": "game.status"}
        if args.player_index is not None:
            req_args["player_index"] = args.player_index
        resp = send_session_command(
            conn,
            "Game.Query",
            req_args,
            args.timeout_ms,
        )
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_queue_unit(args: argparse.Namespace) -> int:
    if not args.unit_template:
        raise ValueError("--unit-template is required for command 'queue-unit'.")

    req_args: dict[str, Any] = {
        "unit_template": args.unit_template,
        "producer_kind": args.producer_kind,
    }
    if args.player_index is not None:
        req_args["player_index"] = args.player_index
    if args.producer_object_id is not None:
        req_args["producer_object_id"] = args.producer_object_id

    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        send_hello(conn, args.timeout_ms)
        resp = send_session_command(conn, "Game.QueueUnit", req_args, args.timeout_ms)
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_build_worker(args: argparse.Namespace) -> int:
    req_args: dict[str, Any] = {}
    if args.player_index is not None:
        req_args["player_index"] = args.player_index
    if args.producer_object_id is not None:
        req_args["producer_object_id"] = args.producer_object_id
    if args.unit_template:
        req_args["unit_template"] = args.unit_template

    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        send_hello(conn, args.timeout_ms)
        resp = send_session_command(conn, "Game.BuildWorker", req_args, args.timeout_ms)
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_find_supply_sources(args: argparse.Namespace) -> int:
    req_args: dict[str, Any] = {"minimum_cash": args.minimum_cash}
    if args.player_index is not None:
        req_args["player_index"] = args.player_index

    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        send_hello(conn, args.timeout_ms)
        resp = send_session_command(conn, "Game.FindSupplySources", req_args, args.timeout_ms)
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_find_build_location_near_supply(args: argparse.Namespace) -> int:
    req_args: dict[str, Any] = {
        "building_template": args.building_template,
        "minimum_cash": args.minimum_cash,
    }
    if args.player_index is not None:
        req_args["player_index"] = args.player_index
    if args.worker_object_id is not None:
        req_args["worker_object_id"] = args.worker_object_id
    if args.supply_source_id is not None:
        req_args["supply_source_id"] = args.supply_source_id

    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        send_hello(conn, args.timeout_ms)
        resp = send_session_command(conn, "Game.FindBuildLocationNearSupply", req_args, args.timeout_ms)
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_dozer_construct(args: argparse.Namespace) -> int:
    req_args: dict[str, Any] = {
        "building_template": args.building_template,
        "minimum_cash": args.minimum_cash,
    }
    if args.player_index is not None:
        req_args["player_index"] = args.player_index
    if args.worker_object_id is not None:
        req_args["worker_object_id"] = args.worker_object_id
    if args.supply_source_id is not None:
        req_args["supply_source_id"] = args.supply_source_id

    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        send_hello(conn, args.timeout_ms)
        resp = send_session_command(conn, "Game.DozerConstruct", req_args, args.timeout_ms)
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_build_supply_stash(args: argparse.Namespace) -> int:
    req_args: dict[str, Any] = {"minimum_cash": args.minimum_cash}
    if args.player_index is not None:
        req_args["player_index"] = args.player_index
    if args.worker_object_id is not None:
        req_args["worker_object_id"] = args.worker_object_id
    if args.building_template:
        req_args["building_template"] = args.building_template

    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        send_hello(conn, args.timeout_ms)
        resp = send_session_command(conn, "Game.BuildSupplyStashAuto", req_args, args.timeout_ms)
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_build_supply_stash_smart(args: argparse.Namespace) -> int:
    req_args: dict[str, Any] = {"minimum_cash": args.minimum_cash}
    if args.player_index is not None:
        req_args["player_index"] = args.player_index
    if args.worker_object_id is not None:
        req_args["worker_object_id"] = args.worker_object_id
    if args.supply_source_id is not None:
        req_args["supply_source_id"] = args.supply_source_id
    if args.building_template:
        req_args["building_template"] = args.building_template

    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        send_hello(conn, args.timeout_ms)
        resp = send_session_command(conn, "Game.BuildSupplyStashSmart", req_args, args.timeout_ms)
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_build_barracks_smart(args: argparse.Namespace) -> int:
    req_args: dict[str, Any] = {}
    if args.player_index is not None:
        req_args["player_index"] = args.player_index
    if args.worker_object_id is not None:
        req_args["worker_object_id"] = args.worker_object_id
    if args.anchor_object_id is not None:
        req_args["anchor_object_id"] = args.anchor_object_id
    if args.building_template:
        req_args["building_template"] = args.building_template

    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        send_hello(conn, args.timeout_ms)
        resp = send_session_command(conn, "Game.BuildBarracksSmart", req_args, args.timeout_ms)
        _print_json(resp, args.pretty)
        return 0
    finally:
        conn.close()


def cmd_lobby_mvp(args: argparse.Namespace) -> int:
    if args.exe_path:
        if not os.path.exists(args.exe_path):
            raise FileNotFoundError(f"Executable not found: {args.exe_path}")
        proc = subprocess.Popen([args.exe_path, *args.exe_args])
        print(f"launched pid={proc.pid} path={args.exe_path}")
        time.sleep(args.delay_ms / 1000.0)

    conn = open_pipe(args.pipe_name, args.timeout_ms)
    try:
        hello_resp = send_hello(conn, args.timeout_ms)
        ensure_success_ack(hello_resp)
        print(f"hello ok session_id={hello_resp.get('session_id', '')}")

        for click in args.lobby_clicks:
            resp = send_session_command(conn, "Menu.Click", {"controlId": click}, args.timeout_ms)
            ensure_success_ack(resp)
            print(f"clicked controlId={click}")
            time.sleep(args.delay_ms / 1000.0)

        print("mvp complete: sent menu click sequence to reach lobby")
        return 0
    finally:
        conn.close()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Zero Hour AI control CLI (MVP).")
    parser.add_argument(
        "command",
        choices=[
            "hello",
            "launch",
            "send",
            "menu-click",
            "menu-set-text",
            "list-controls",
            "chat-send",
            "query",
            "status",
            "queue-unit",
            "build-worker",
            "find-supply-sources",
            "find-build-location-near-supply",
            "dozer-construct",
            "build-supply-stash",
            "build-supply-stash-smart",
            "build-barracks-smart",
            "main-click",
            "lan-click",
            "lan-name-set",
            "lobby-mvp",
        ],
        help="Command to run.",
    )
    parser.add_argument("--pipe-name", default="zh_ai_control")
    parser.add_argument("--timeout-ms", type=int, default=8000)
    parser.add_argument("--pretty", action="store_true")
    parser.add_argument("--exe-path", default="")
    parser.add_argument("--exe-args", nargs="*", default=[])
    parser.add_argument("--session-cmd", default="")
    parser.add_argument("--control-id", default="")
    parser.add_argument("--text", default=None)
    parser.add_argument("--lan-action", choices=["main-menu", "create-game", "join-game", "direct-connect"], default="main-menu")
    parser.add_argument(
        "--main-action",
        choices=["skirmish", "multiplayer", "singleplayer", "soloplay", "solo-play"],
        default="skirmish",
    )
    parser.add_argument("--kind", choices=["all", "button", "text_entry"], default="button")
    parser.add_argument("--include-hidden", action="store_true")
    parser.add_argument("--scope", choices=["players", "allies", "everyone"], default="everyone")
    parser.add_argument(
        "--path",
        choices=[
            "game.status",
            "game.summary",
            "game.all",
            "game.local_player",
            "game.player",
            "game.players",
            "game.faction",
            "game.resources",
            "game.units",
        ],
        default="game.status",
    )
    parser.add_argument("--player-index", type=int, default=None)
    parser.add_argument("--unit-template", default="")
    parser.add_argument("--producer-object-id", type=int, default=None)
    parser.add_argument("--producer-kind", choices=["command_center", "any"], default="command_center")
    parser.add_argument("--building-template", default="")
    parser.add_argument("--supply-source-id", type=int, default=None)
    parser.add_argument("--worker-object-id", type=int, default=None)
    parser.add_argument("--anchor-object-id", type=int, default=None)
    parser.add_argument("--minimum-cash", type=int, default=1)
    parser.add_argument("--args-json", default="{}")
    parser.add_argument("--delay-ms", type=int, default=1000)
    parser.add_argument(
        "--lobby-clicks",
        nargs="*",
        default=["MainMenu.wnd:ButtonMultiplayer", "MainMenu.wnd:ButtonNetwork"],
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    handlers = {
        "launch": cmd_launch,
        "hello": cmd_hello,
        "send": cmd_send,
        "menu-click": cmd_menu_click,
        "menu-set-text": cmd_menu_set_text,
        "list-controls": cmd_list_controls,
        "chat-send": cmd_chat_send,
        "query": cmd_query,
        "status": cmd_status,
        "queue-unit": cmd_queue_unit,
        "build-worker": cmd_build_worker,
        "find-supply-sources": cmd_find_supply_sources,
        "find-build-location-near-supply": cmd_find_build_location_near_supply,
        "dozer-construct": cmd_dozer_construct,
        "build-supply-stash": cmd_build_supply_stash,
        "build-supply-stash-smart": cmd_build_supply_stash_smart,
        "build-barracks-smart": cmd_build_barracks_smart,
        "main-click": cmd_main_click,
        "lan-click": cmd_lan_click,
        "lan-name-set": cmd_lan_name_set,
        "lobby-mvp": cmd_lobby_mvp,
    }

    try:
        return handlers[args.command](args)
    except Exception as exc:  # noqa: BLE001
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
