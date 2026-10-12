#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
DAEMON="$BUILD_DIR/spatial_daemon"
TEST_RASTER="$BUILD_DIR/test_raster"
SAMPLE_C="$SCRIPT_DIR/tests/sample_project.c"
SOCKET="/tmp/spatial_nav.sock"

echo "=== [PHASE 4 TEST] Step 1: Building CMake project ==="
cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR"
cmake --build "$BUILD_DIR"

if [ ! -x "$TEST_RASTER" ]; then
    echo "Error: $TEST_RASTER not found or not executable."
    exit 1
fi

echo "=== [PHASE 4 TEST] Step 2: Running Rasterizer Unit Tests (test_raster) ==="
cd "$SCRIPT_DIR"
"$TEST_RASTER"

echo "=== [PHASE 4 TEST] Step 3: Starting spatial_daemon for IPC Sixel Verification ==="
rm -f "$SOCKET"
"$DAEMON" &
DAEMON_PID=$!

sleep 0.5

if [ ! -S "$SOCKET" ]; then
    echo "Error: Socket $SOCKET was not created!"
    kill -9 "$DAEMON_PID" || true
    exit 1
fi

echo "=== [PHASE 4 TEST] Step 4: Requesting 3D graph sixel stream over socket ==="
/usr/bin/python3 - <<EOF
import socket
import sys

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect("$SOCKET")
s.sendall(b"$SAMPLE_C\n")

chunks = []
while True:
    data = s.recv(16384)
    if not data:
        break
    chunks.append(data)
s.close()

payload = b"".join(chunks)
print(f"[CLIENT] Received {len(payload)} bytes of Sixel data from daemon.")

if len(payload) < 500:
    print(f"FAILED: Sixel payload too short ({len(payload)} bytes)")
    sys.exit(1)

if not payload.startswith(bytes([0x1B, ord('P')])):
    print(f"FAILED: Sixel payload missing DCS header (starts with {payload[:10]})")
    sys.exit(1)

if not payload.rstrip().endswith(bytes([0x1B, 0x5C])):
    print(f"FAILED: Sixel payload missing ST terminator (ends with {payload[-10:]})")
    sys.exit(1)

print("[CLIENT] SUCCESS: Received valid DCS Sixel escape stream!")
EOF

echo "=== [PHASE 4 TEST] Step 5: Shutting down daemon via SIGTERM ==="
kill -TERM "$DAEMON_PID"
wait "$DAEMON_PID" 2>/dev/null || true

if [ -e "$SOCKET" ]; then
    echo "FAILED: Socket was not unlinked on shutdown!"
    exit 1
fi

echo "=== [PHASE 4 TEST] Step 6: Regression verification of Phase 2 & 3 tests ==="
"$BUILD_DIR/test_parser" "$SAMPLE_C"
"$BUILD_DIR/test_physics"

echo ""
echo "=========================================================="
echo ">>> ALL PHASE 4 SIXEL RASTERIZATION CHECKS PASSED! <<<"
echo "=========================================================="
