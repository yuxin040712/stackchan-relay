import secrets

from fastapi import Body, FastAPI, HTTPException, Query
from starlette.middleware.base import BaseHTTPMiddleware

from app import state
from app.config import TOKEN
from app.mcp_tools import mcp

mcp_app = mcp.streamable_http_app()


async def _force_host(request, call_next):
    request.scope["headers"] = [
        (k, v) for (k, v) in request.scope["headers"] if k != b"host"
    ] + [(b"host", b"127.0.0.1:8011")]
    return await call_next(request)

mcp_app.add_middleware(BaseHTTPMiddleware, dispatch=_force_host)



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
    """ESP32 short-polls this for the next command (returns immediately)."""
    _check_token(token)
    command = await state.get_command()
    if command is None:
        return {"action": None}
    return command


@app.post("/result")
async def result(token: str = Query(...), payload: dict = Body(...)):
    """ESP32 reports back the outcome of the last command it ran."""
    _check_token(token)
    await state.set_result(payload)
    return {"ok": True}
