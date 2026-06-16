import os

from dotenv import load_dotenv

load_dotenv()

# Shared secret used both as the MCP endpoint path segment (/mcp/<TOKEN>)
# and as the ?token= query param for the ESP32 endpoints.
TOKEN = os.environ["STACKCHAN_TOKEN"]

HOST = os.environ.get("STACKCHAN_HOST", "127.0.0.1")
PORT = int(os.environ.get("STACKCHAN_PORT", "8011"))

# If the ESP32 hasn't polled within this many seconds, consider it offline.
ONLINE_THRESHOLD = float(os.environ.get("STACKCHAN_ONLINE_THRESHOLD", "5"))
