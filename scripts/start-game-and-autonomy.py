#!/usr/bin/env python3
"""
Start game and enable autonomy - Python version
Connects to the AI Control Adapter pipe, clicks Play Game, and enables autonomy
Uses ctypes for Windows API calls (no dependencies needed)
"""

import sys
import time
import json
import argparse
import ctypes
from ctypes import wintypes


# Windows API constants
GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
FILE_ATTRIBUTE_NORMAL = 0x80
INVALID_HANDLE_VALUE = -1

# Load kernel32.dll
kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)

# Define Windows API functions
CreateFileW = kernel32.CreateFileW
CreateFileW.argtypes = [
    wintypes.LPCWSTR,  # lpFileName
    wintypes.DWORD,    # dwDesiredAccess
    wintypes.DWORD,    # dwShareMode
    wintypes.LPVOID,   # lpSecurityAttributes
    wintypes.DWORD,    # dwCreationDisposition
    wintypes.DWORD,    # dwFlagsAndAttributes
    wintypes.HANDLE    # hTemplateFile
]
CreateFileW.restype = wintypes.HANDLE

WriteFile = kernel32.WriteFile
WriteFile.argtypes = [
    wintypes.HANDLE,   # hFile
    wintypes.LPCVOID,  # lpBuffer
    wintypes.DWORD,    # nNumberOfBytesToWrite
    wintypes.LPDWORD,  # lpNumberOfBytesWritten
    wintypes.LPVOID    # lpOverlapped
]
WriteFile.restype = wintypes.BOOL

ReadFile = kernel32.ReadFile
ReadFile.argtypes = [
    wintypes.HANDLE,   # hFile
    wintypes.LPVOID,   # lpBuffer
    wintypes.DWORD,    # nNumberOfBytesToRead
    wintypes.LPDWORD,  # lpNumberOfBytesRead
    wintypes.LPVOID    # lpOverlapped
]
ReadFile.restype = wintypes.BOOL

CloseHandle = kernel32.CloseHandle
CloseHandle.argtypes = [wintypes.HANDLE]
CloseHandle.restype = wintypes.BOOL


def connect_pipe(pipe_name):
    """Connect to Windows named pipe using ctypes"""
    pipe_path = f"\\\\.\\pipe\\{pipe_name}"

    handle = CreateFileW(
        pipe_path,
        GENERIC_READ | GENERIC_WRITE,
        0,
        None,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        None
    )

    if handle == INVALID_HANDLE_VALUE:
        error = ctypes.get_last_error()
        print(f"Failed to connect to pipe: error {error}", file=sys.stderr)
        return None

    return handle


def send_command(handle, cmd_type, request_id, cmd=None, args=None):
    """Send a command to the adapter and read response"""
    msg = {
        "type": cmd_type,
        "request_id": request_id
    }
    if cmd:
        msg["cmd"] = cmd
    if args:
        msg["args"] = args

    json_str = json.dumps(msg) + '\n'
    data = json_str.encode('utf-8')

    # Write to pipe
    bytes_written = wintypes.DWORD(0)
    success = WriteFile(handle, data, len(data), ctypes.byref(bytes_written), None)
    if not success:
        return None

    # Read response
    buffer = ctypes.create_string_buffer(65536)
    bytes_read = wintypes.DWORD(0)
    success = ReadFile(handle, buffer, 65536, ctypes.byref(bytes_read), None)

    if success and bytes_read.value > 0:
        response_data = buffer.raw[:bytes_read.value].decode('utf-8').strip()
        return json.loads(response_data)

    return None


def main():
    parser = argparse.ArgumentParser(description='Start game and enable autonomy')
    parser.add_argument('--pipe-name', default='zh_ai_control', help='Named pipe name')
    parser.add_argument('--profile', default='sprawl', help='Autonomy profile')
    parser.add_argument('--sprawl-multiplier', type=int, default=10, help='Sprawl multiplier')
    parser.add_argument('--no-attacks', action='store_true', help='Disable attacks')
    args = parser.parse_args()

    attack_enabled = not args.no_attacks

    print("\n========================================")
    print("Start Game and Enable Autonomy")
    print("========================================\n")

    # Connect to pipe
    handle = connect_pipe(args.pipe_name)
    if not handle:
        print("Failed to connect to pipe!")
        sys.exit(1)

    print("Connected!")

    try:
        # Send Hello
        hello_resp = send_command(handle, "Hello", "hello-1")
        time.sleep(0.1)

        # Click "Play Game" button
        print("\n==> Clicking Play Game button...")
        click_resp = send_command(
            handle,
            "SessionCommand",
            "start-1",
            cmd="Menu.Click",
            args={"controlId": "SkirmishGameOptionsMenu.wnd:ButtonStart"}
        )

        if click_resp and click_resp.get("ok"):
            print("    Play Game clicked!")
        elif click_resp:
            print(f"    Failed: {click_resp.get('reason', 'unknown')}")
            sys.exit(1)
        else:
            print("    Play Game clicked (no response, game transitioning)")

        # Wait for game to transition from menu to in-game
        print("\n==> Waiting for game to transition (8 seconds)...")
        for i in range(8, 0, -1):
            print(f"    {i}...", end=' ', flush=True)
            time.sleep(1)
        print("\n    Transition complete!")

        # Close and reconnect to pipe
        print("\n==> Reconnecting to pipe after game load...")
        CloseHandle(handle)

        time.sleep(0.5)
        handle = connect_pipe(args.pipe_name)
        if not handle:
            print("    Failed to reconnect!")
            sys.exit(1)
        print("    Reconnected!")

        # Send Hello again
        hello_resp = send_command(handle, "Hello", "hello-2")
        time.sleep(0.2)

        # Set camera height higher to see more of the map
        print("\n==> Setting camera height...")
        camera_resp = send_command(
            handle,
            "SessionCommand",
            "camera-1",
            cmd="Game.Camera.Set",
            args={"height_multiplier": 2.5}
        )

        if camera_resp and camera_resp.get("ok"):
            print("    Camera height set to 2.5x!")
        else:
            print("    Warning: Camera adjustment failed (may not be critical)")

        time.sleep(0.2)

        # Configure autonomy
        attack_status = "enabled" if attack_enabled else "disabled"
        print(f"\n==> Configuring autonomy ({args.profile} profile, {args.sprawl_multiplier}x multiplier, attacks {attack_status})...")

        config_resp = send_command(
            handle,
            "SessionCommand",
            "config-1",
            cmd="Autonomy.Configure",
            args={
                "profile": args.profile,
                "sprawl_multiplier": args.sprawl_multiplier,
                "attack_enabled": attack_enabled
            }
        )

        if not config_resp:
            print("    Failed: No response from adapter (pipe disconnected?)")
            sys.exit(1)

        if config_resp.get("ok"):
            print("    Autonomy configured!")
        else:
            print(f"    Failed: {config_resp.get('reason', 'unknown')}")
            sys.exit(1)

        time.sleep(0.5)

        # Enable autonomy mode
        print("\n==> Enabling autonomous mode...")
        mode_resp = send_command(
            handle,
            "SessionCommand",
            "mode-1",
            cmd="Autonomy.SetMode",
            args={"mode": "autonomous"}
        )

        if not mode_resp:
            print("    Failed: No response from adapter (pipe disconnected?)")
            sys.exit(1)

        if mode_resp.get("ok"):
            print("    Autonomy mode enabled!")
        else:
            print(f"    Failed: {mode_resp.get('reason', 'unknown')}")
            sys.exit(1)

        # Success!
        print("\n========================================")
        print("Game started with autonomy!")
        print("========================================\n")
        print(f"Profile: {args.profile}")
        print(f"Sprawl multiplier: {args.sprawl_multiplier}x")
        print(f"Attacks: {attack_status}")

        if args.profile == "sprawl_balanced":
            print("\nNote: sprawl_balanced caps at 100 units and pauses production smartly")
        elif args.profile == "sprawl":
            print("\nNote: sprawl has no unit cap (9999) - will produce units continuously")

        print()

    finally:
        if handle:
            CloseHandle(handle)


if __name__ == "__main__":
    main()
