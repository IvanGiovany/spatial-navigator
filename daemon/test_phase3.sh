#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
DAEMON="$BUILD_DIR/spatial_daemon"
TEST_PHYSICS="$BUILD_DIR/test_physics"
TEST_PARSER="$BUILD_DIR/test_parser"
SAMPLE_C="$SCRIPT_DIR/tests/sample_project.c"
SOCKET="/tmp/spatial_nav.sock"

echo "=== [PHASE 3 TEST] Step 1: Building CMake project ==="
cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR"
cmake --build "$BUILD_DIR"

if [ ! -x "$TEST_PHYSICS" ]; then
    echo "Error: $TEST_PHYSICS not found or not executable."
    exit 1
fi

echo "=== [PHASE 3 TEST] Step 2: Running Physics Unit Tests (test_physics) ==="
cd "$SCRIPT_DIR"
"$TEST_PHYSICS"

echo "=== [PHASE 3 TEST] Step 3: Starting spatial_daemon for IPC Physics Verification ==="
rm -f "$SOCKET"
"$DAEMON" &
DAEMON_PID=$!

sleep 0.5

if [ ! -S "$SOCKET" ]; then
    echo "Error: Socket $SOCKET was not created!"
    kill -9 "$DAEMON_PID" || true
    exit 1
fi

echo "=== [PHASE 3 TEST] Step 4: Sending sample file request over socket ==="
RESPONSE=$(/usr/bin/python3 - <<EOF
import socket
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect("$SOCKET")
s.sendall(b"$SAMPLE_C\n")
data = s.recv(4096)
print(data.decode("utf-8").strip())
s.close()
EOF
)

echo "Received response: '$RESPONSE'"
EXPECTED="ACK: $SAMPLE_C"
if [ "$RESPONSE" != "$EXPECTED" ]; then
    echo "FAILED: Expected '$EXPECTED', got '$RESPONSE'"
    kill -9 "$DAEMON_PID" || true
    exit 1
fi

echo "=== [PHASE 3 TEST] Step 5: Shutting down daemon via SIGTERM ==="
kill -TERM "$DAEMON_PID"
wait "$DAEMON_PID" 2>/dev/null || true

if [ -e "$SOCKET" ]; then
    echo "FAILED: Socket was not unlinked on shutdown!"
    exit 1
fi

echo "=== [PHASE 3 TEST] Step 6: Regression check on Phase 1 & 2 tests ==="
bash "$SCRIPT_DIR/test_phase1.sh"
bash "$SCRIPT_DIR/test_phase2.sh"

echo ""
echo "=========================================================="
echo ">>> ALL PHASE 3 3D PHYSICS ENGINE CHECKS PASSED! <<<"
echo "=========================================================="
