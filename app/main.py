import secrets

from fastapi import Body, FastAPI, HTTPException, Query

from app import state
from app.config import POLL_TIMEOUT, TOKEN
from app.mcp_tools import mcp

mcp_app = mcp.streamable_http_app()

app = FastAPI(lifespan=lambda _app: mcp_app.router.lifespan_context(_app))
app.mount(f"/mcp/{TOKEN}", mcp_app)


def _check_token(token: str) -> None:
    if not secrets.compare_digest(token, TOKEN):
        raise HTTPException(status_code=403, detail="invalid token")


@app.get("/healthz")
async def healthz():
    return {"ok": True}


@app.get("/poll")
async def poll(token: str = Query(...)):
    """ESP32 long-polls this for the next command (latest-only)."""
    _check_token(token)
    command = await state.wait_for_command(timeout=POLL_TIMEOUT)
    if command is None:
        return {"action": None}
    return command


@app.post("/result")
async def result(token: str = Query(...), payload: dict = Body(...)):
    """ESP32 reports back the outcome of the last command it ran."""
    _check_token(token)
    await state.set_result(payload)
    return {"ok": True}
