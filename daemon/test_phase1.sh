#!/usr/bin/env bash
set -e

BUILD_DIR="$(dirname "$0")/build"
DAEMON="$BUILD_DIR/spatial_daemon"
SOCKET="/tmp/spatial_nav.sock"

if [ ! -x "$DAEMON" ]; then
    echo "Error: $DAEMON not found or not executable. Build first."
    exit 1
fi

echo "[TEST] Starting spatial_daemon..."
"$DAEMON" &
DAEMON_PID=$!

# Give daemon time to bind
sleep 0.5

if [ ! -S "$SOCKET" ]; then
    echo "Error: Socket file $SOCKET was not created!"
    kill -9 "$DAEMON_PID" || true
    exit 1
fi
echo "[TEST] Socket $SOCKET successfully created."

echo "[TEST] Sending request via python3 client..."
TEST_PATH="/home/user/project/main.c"
RESPONSE=$(/usr/bin/python3 - <<EOF
import socket
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect("$SOCKET")
s.sendall(b"$TEST_PATH\n")
data = s.recv(4096)
print(data.decode("utf-8").strip())
s.close()
EOF
)

echo "[TEST] Received response: '$RESPONSE'"

EXPECTED="ACK: $TEST_PATH"
if [ "$RESPONSE" != "$EXPECTED" ]; then
    echo "[TEST] FAILED: Expected '$EXPECTED', got '$RESPONSE'"
    kill -9 "$DAEMON_PID" || true
    exit 1
fi

echo "[TEST] Response matched expected ACK!"

echo "[TEST] Stopping daemon with SIGTERM..."
kill -TERM "$DAEMON_PID"
wait "$DAEMON_PID" 2>/dev/null || true

if [ -e "$SOCKET" ]; then
    echo "[TEST] FAILED: Socket was not unlinked on shutdown!"
    exit 1
fi

echo "[TEST] Socket unlinked cleanly."
echo "[TEST] ALL PHASE 1 CHECKS PASSED SUCCESSFULLY!"
