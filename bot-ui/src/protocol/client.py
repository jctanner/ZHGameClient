from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
import json
import os
import queue
import threading
import time
from typing import Any

from protocol.messages import compact_json


PIPE_ACCESS_DUPLEX = 0xC0000000
OPEN_EXISTING = 3
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
ERROR_SEM_TIMEOUT = 121
ERROR_PIPE_BUSY = 231


class PipeClient:
    def __init__(self, pipe_name: str = "zh_ai_control", timeout_ms: int = 15000) -> None:
        self.pipe_name = pipe_name
        self.timeout_ms = timeout_ms
        self._stream: Any | None = None
        self._reader_thread: threading.Thread | None = None
        self._writer_thread: threading.Thread | None = None
        self._running = False
        self.incoming: "queue.Queue[dict[str, Any]]" = queue.Queue()
        self.errors: "queue.Queue[str]" = queue.Queue()
        self._outgoing: "queue.Queue[dict[str, Any] | None]" = queue.Queue()

    @property
    def is_connected(self) -> bool:
        return self._stream is not None and self._running

    def connect(self) -> None:
        if self.is_connected:
            return
        self._stream = self._open_pipe(self.pipe_name, self.timeout_ms)
        self._running = True
        self._writer_thread = threading.Thread(target=self._writer_loop, name="botui-pipe-writer", daemon=True)
        self._writer_thread.start()
        self._reader_thread = threading.Thread(target=self._reader_loop, name="botui-pipe-reader", daemon=True)
        self._reader_thread.start()

    def disconnect(self) -> None:
        self._running = False
        self._outgoing.put(None)
        stream = self._stream
        self._stream = None
        if stream is not None:
            try:
                stream.close()
            except OSError:
                pass

    def send_json(self, payload: dict[str, Any]) -> str:
        if not self.is_connected:
            raise RuntimeError("Not connected to adapter.")
        request_id = str(payload.get("request_id", ""))
        self._outgoing.put(payload)
        return request_id

    def _writer_loop(self) -> None:
        while self._running:
            item = self._outgoing.get()
            if item is None:
                return
            stream = self._stream
            if stream is None:
                return
            try:
                data = (compact_json(item) + "\n").encode("utf-8")
                stream.write(data)
                stream.flush()
            except OSError as exc:
                self.errors.put(f"Pipe write error: {exc}")
                self._running = False
                return
            except Exception as exc:  # noqa: BLE001
                self.errors.put(f"Unexpected pipe writer error: {exc}")
                self._running = False
                return

    def _reader_loop(self) -> None:
        while self._running:
            stream = self._stream
            if stream is None:
                return
            try:
                line = stream.readline()
                if not line:
                    self.errors.put("Adapter pipe closed.")
                    self._running = False
                    return
                text = line.decode("utf-8", errors="replace").strip()
                if not text:
                    continue
                try:
                    message = json.loads(text)
                except json.JSONDecodeError:
                    self.errors.put(f"Invalid JSON from adapter: {text[:200]}")
                    continue
                if isinstance(message, dict):
                    self.incoming.put(message)
            except OSError as exc:
                self.errors.put(f"Pipe read error: {exc}")
                self._running = False
                return
            except Exception as exc:  # noqa: BLE001
                self.errors.put(f"Unexpected pipe reader error: {exc}")
                self._running = False
                return

    def _open_pipe(self, pipe_name: str, timeout_ms: int) -> Any:
        if os.name != "nt":
            raise RuntimeError("bot-ui supports Windows named pipes only.")
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

        deadline = time.monotonic() + (timeout_ms / 1000.0)
        handle = INVALID_HANDLE_VALUE
        last_error = 0
        while time.monotonic() < deadline:
            wait_ok = kernel32.WaitNamedPipeW(pipe_path, wt.DWORD(max(1, int((deadline - time.monotonic()) * 1000))))
            if not wait_ok:
                last_error = ctypes.get_last_error()
                if last_error == ERROR_SEM_TIMEOUT:
                    break
                if last_error == ERROR_PIPE_BUSY:
                    time.sleep(0.02)
                    continue
                raise RuntimeError(f"WaitNamedPipe failed for {pipe_path} (winerr={last_error}).")

            handle = kernel32.CreateFileW(pipe_path, PIPE_ACCESS_DUPLEX, 0, None, OPEN_EXISTING, 0, None)
            if handle != INVALID_HANDLE_VALUE:
                break

            last_error = ctypes.get_last_error()
            if last_error == ERROR_PIPE_BUSY:
                time.sleep(0.02)
                continue
            raise RuntimeError(f"CreateFile failed for {pipe_path} (winerr={last_error}).")

        if handle == INVALID_HANDLE_VALUE:
            if last_error == 0:
                last_error = ERROR_SEM_TIMEOUT
            raise RuntimeError(f"CreateFile failed for {pipe_path} (winerr={last_error}).")

        import msvcrt  # noqa: PLC0415

        fd = msvcrt.open_osfhandle(handle, 0)
        return os.fdopen(fd, "r+b", buffering=0)

    def request_once(self, payload: dict[str, Any], timeout_ms: int | None = None) -> dict[str, Any]:
        request_id = str(payload.get("request_id", ""))
        if not request_id:
            raise RuntimeError("payload.request_id is required")
        timeout = timeout_ms if timeout_ms is not None else self.timeout_ms
        stream = self._open_pipe(self.pipe_name, timeout)
        try:
            data = (compact_json(payload) + "\n").encode("utf-8")
            stream.write(data)
            stream.flush()
            deadline = time.monotonic() + (timeout / 1000.0)
            recv_buffer = b""
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(f"Timed out waiting for reply request_id={request_id}")
                # Use PeekNamedPipe polling to avoid blocking indefinitely on readline().
                chunk = self._read_available_with_timeout(stream, int(remaining * 1000))
                if chunk:
                    recv_buffer += chunk
                while True:
                    newline = recv_buffer.find(b"\n")
                    if newline < 0:
                        break
                    line_bytes = recv_buffer[:newline]
                    recv_buffer = recv_buffer[newline + 1 :]
                    line = line_bytes.decode("utf-8", errors="replace").strip()
                    if not line:
                        continue
                    try:
                        obj = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if isinstance(obj, dict) and str(obj.get("request_id", "")) == request_id:
                        return obj
        finally:
            try:
                stream.close()
            except OSError:
                pass

    @staticmethod
    def _read_available_with_timeout(stream: Any, timeout_ms: int) -> bytes:
        deadline = time.monotonic() + (timeout_ms / 1000.0)
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.PeekNamedPipe.argtypes = [wt.HANDLE, wt.LPVOID, wt.DWORD, wt.LPVOID, ctypes.POINTER(wt.DWORD), wt.LPVOID]
        kernel32.PeekNamedPipe.restype = wt.BOOL

        import msvcrt  # noqa: PLC0415

        handle = msvcrt.get_osfhandle(stream.fileno())
        while time.monotonic() < deadline:
            available = wt.DWORD(0)
            ok = kernel32.PeekNamedPipe(handle, None, 0, None, ctypes.byref(available), None)
            if not ok:
                winerr = ctypes.get_last_error()
                raise RuntimeError(f"PeekNamedPipe failed (winerr={winerr})")
            if int(available.value) > 0:
                return stream.read(int(available.value))
            time.sleep(0.01)
        return b""
