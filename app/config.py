import os

from dotenv import load_dotenv

load_dotenv()

# Shared secret used both as the MCP endpoint path segment (/mcp/<TOKEN>)
# and as the ?token= query param for the ESP32 endpoints.
TOKEN = os.environ["STACKCHAN_TOKEN"]

HOST = os.environ.get("STACKCHAN_HOST", "127.0.0.1")
PORT = int(os.environ.get("STACKCHAN_PORT", "8011"))

# How long /poll waits for a new command before returning empty (seconds).
POLL_TIMEOUT = float(os.environ.get("STACKCHAN_POLL_TIMEOUT", "25"))

# If the ESP32 hasn't polled within this many seconds, consider it offline.
ONLINE_THRESHOLD = float(os.environ.get("STACKCHAN_ONLINE_THRESHOLD", "30"))
