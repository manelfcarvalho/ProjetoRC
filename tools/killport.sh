/* =========================== tools/killport.sh ============================ */
#!/usr/bin/env bash
PORT="$1"
if [[ -z "$PORT" ]]; then
  echo "Usage: killport <port>" >&2
  exit 1
fi
PIDS=$(lsof -t -i tcp:"$PORT")
if [[ -n "$PIDS" ]]; then
  echo "[killport] Killing processes on port $PORT: $PIDS"
  kill $PIDS
else
  echo "[killport] No process is listening on port $PORT"
fi

