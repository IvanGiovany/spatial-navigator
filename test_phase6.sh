#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DAEMON_DIR="$SCRIPT_DIR/daemon"
BUILD_DIR="$DAEMON_DIR/build"
DAEMON="$BUILD_DIR/spatial_daemon"
SOCKET="/tmp/spatial_nav.sock"
TEST_LUA="$SCRIPT_DIR/nvim-plugin/tests/test_plugin.lua"

echo "=== [PHASE 6 TEST] Step 1: Building daemon executable ==="
cmake -B "$BUILD_DIR" -S "$DAEMON_DIR"
cmake --build "$BUILD_DIR"

if [ ! -x "$DAEMON" ]; then
    echo "Error: Daemon executable $DAEMON not found or not executable."
    exit 1
fi

echo "=== [PHASE 6 TEST] Step 2: Testing Neovim plugin in offline mode (Unit + Offline tests) ==="
rm -f "$SOCKET"
pkill -9 spatial_daemon 2>/dev/null || true

nvim --headless -u NONE -c "luafile $TEST_LUA"

echo "=== [PHASE 6 TEST] Step 3: Starting spatial_daemon in background ==="
"$DAEMON" &
DAEMON_PID=$!

sleep 0.5

if [ ! -S "$SOCKET" ]; then
    echo "Error: Socket $SOCKET was not created!"
    kill -9 "$DAEMON_PID" || true
    exit 1
fi

echo "=== [PHASE 6 TEST] Step 4: Running Neovim headless test with active daemon (E2E Sixel streaming) ==="
nvim --headless -u NONE -c "luafile $TEST_LUA"

echo "=== [PHASE 6 TEST] Step 5: Shutting down spatial_daemon cleanly via SIGTERM ==="
kill -TERM "$DAEMON_PID"
wait "$DAEMON_PID" 2>/dev/null || true

if [ -e "$SOCKET" ]; then
    echo "Error: Socket $SOCKET was not unlinked cleanly on shutdown!"
    exit 1
fi

echo ""
echo "=========================================================="
echo ">>> ALL PHASE 6 NEOVIM FRONTEND CHECKS PASSED! <<<"
echo "=========================================================="
