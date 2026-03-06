from __future__ import annotations

import json
import uuid
from typing import Any


def new_request_id() -> str:
    return uuid.uuid4().hex


def compact_json(payload: dict[str, Any]) -> str:
    return json.dumps(payload, separators=(",", ":"))


def hello_message(client_name: str = "bot-ui", client_version: str = "0.1.0") -> dict[str, Any]:
    return {
        "type": "Hello",
        "request_id": new_request_id(),
        "protocol": "zh-ai-control-v1",
        "client_name": client_name,
        "client_version": client_version,
    }


def session_command_message(cmd: str, args: dict[str, Any] | None = None) -> dict[str, Any]:
    return {
        "type": "SessionCommand",
        "request_id": new_request_id(),
        "cmd": cmd,
        "args": args or {},
    }


def subscribe_message(streams: list[str]) -> dict[str, Any]:
    return {"type": "Subscribe", "request_id": new_request_id(), "streams": streams}


def unsubscribe_message(streams: list[str]) -> dict[str, Any]:
    return {"type": "Unsubscribe", "request_id": new_request_id(), "streams": streams}
