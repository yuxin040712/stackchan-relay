import asyncio
import time

from app.config import ONLINE_THRESHOLD

_lock = asyncio.Lock()
_command_ready = asyncio.Event()

_next_id = 1
_pending_command: dict | None = None
_last_command: dict | None = None  # last command handed out, kept for status
_last_result: dict | None = None
_last_poll_at: float | None = None


def _new_id() -> str:
    global _next_id
    cmd_id = f"cmd_{_next_id:06d}"
    _next_id += 1
    return cmd_id


async def push_command(action: str, **fields) -> dict:
    """Replace any pending command with this one (latest-only)."""
    global _pending_command, _last_command
    command = {"id": _new_id(), "action": action, **fields}
    async with _lock:
        _pending_command = command
        _last_command = command
        _command_ready.set()
    return command


async def wait_for_command(timeout: float) -> dict | None:
    """Wait up to `timeout` seconds for a pending command, then return and
    clear it. Returns None if nothing arrived in time."""
    global _pending_command, _last_poll_at

    async with _lock:
        _last_poll_at = time.time()
        if _pending_command is not None:
            command = _pending_command
            _pending_command = None
            _command_ready.clear()
            return command

    try:
        await asyncio.wait_for(_command_ready.wait(), timeout=timeout)
    except asyncio.TimeoutError:
        async with _lock:
            _last_poll_at = time.time()
        return None

    async with _lock:
        _last_poll_at = time.time()
        command = _pending_command
        _pending_command = None
        _command_ready.clear()
        return command


async def set_result(result: dict) -> None:
    global _last_result
    async with _lock:
        _last_result = {**result, "received_at": time.time()}


async def get_status() -> dict:
    async with _lock:
        now = time.time()
        online = _last_poll_at is not None and (now - _last_poll_at) <= ONLINE_THRESHOLD
        return {
            "online": online,
            "last_seen_seconds_ago": (now - _last_poll_at) if _last_poll_at else None,
            "pending_command": _pending_command,
            "last_command": _last_command,
            "last_result": _last_result,
        }
