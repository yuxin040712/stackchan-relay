from mcp.server.fastmcp import FastMCP

from app import state

mcp = FastMCP(
    "stackchan",
    instructions=(
        "Tools to control a StackChan robot (M5Stack CoreS3). "
        "Commands are queued latest-only: sending a new command replaces "
        "any command the robot hasn't picked up yet."
    ),
    streamable_http_path="/",
    stateless_http=True,
    json_response=True,
)


@mcp.tool()
async def stackchan_speak(text: str) -> str:
    """Make StackChan say something out loud.

    Args:
        text: The text for StackChan to speak (Japanese or Chinese ok).
    """
    command = await state.push_command("speak", text=text)
    return f"Queued speak command {command['id']}: {text!r}"


@mcp.tool()
async def stackchan_emote(expression: str) -> str:
    """Change StackChan's facial expression.

    Args:
        expression: One of: happy, sad, angry, shy, thinking, surprised,
            sleepy, normal.
    """
    command = await state.push_command("emote", expression=expression)
    return f"Queued emote command {command['id']}: {expression!r}"


@mcp.tool()
async def stackchan_move_head(pitch: float, yaw: float) -> str:
    """Move StackChan's head/servo.

    Args:
        pitch: Up/down angle in degrees (negative = down, positive = up).
        yaw: Left/right angle in degrees (negative = left, positive = right).
    """
    command = await state.push_command("move_head", pitch=pitch, yaw=yaw)
    return f"Queued move_head command {command['id']}: pitch={pitch}, yaw={yaw}"


@mcp.tool()
async def stackchan_wiggle() -> str:
    """Make StackChan wiggle its head left and right."""
    command = await state.push_command("wiggle")
    return f"Queued wiggle command {command['id']}"


@mcp.tool()
async def get_stackchan_status() -> dict:
    """Get StackChan's connection status and the result of the last command."""
    return await state.get_status()
