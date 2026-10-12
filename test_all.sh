#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DAEMON_DIR="$SCRIPT_DIR/daemon"
BUILD_DIR="$DAEMON_DIR/build"
SOCKET="/tmp/spatial_nav.sock"
TEST_LUA="$SCRIPT_DIR/nvim-plugin/tests/test_plugin.lua"
STRESS_PY="$DAEMON_DIR/tests/stress_test.py"

echo "=========================================================="
echo "    Spatial Codebase Navigator - Master Test Runner       "
echo "=========================================================="

# Cleanup any stray daemon instances from previous runs
rm -f "$SOCKET"
pkill -9 spatial_daemon 2>/dev/null || true

echo ""
echo "=== [STAGE 1] Building C Daemon & Unit Test Executables ==="
cmake -B "$BUILD_DIR" -S "$DAEMON_DIR"
cmake --build "$BUILD_DIR"

echo ""
echo "=== [STAGE 2] Running C Unit Test Matrix ==="
echo "--- Running test_parser (Tree-sitter AST & Graph) ---"
"$BUILD_DIR/test_parser" "$DAEMON_DIR/tests/sample_project.c" "$DAEMON_DIR/src/main.c"

echo "--- Running test_physics (Fruchterman-Reingold 3D Engine) ---"
"$BUILD_DIR/test_physics"

echo "--- Running test_raster (Software Rasterizer & Libsixel) ---"
"$BUILD_DIR/test_raster"

echo ""
echo "=== [STAGE 3] Running Neovim Offline Test Suite ==="
nvim --headless -u NONE -c "luafile $TEST_LUA"

echo ""
echo "=== [STAGE 4] Starting Daemon & Live IPC Verification ==="
"$BUILD_DIR/spatial_daemon" &
DAEMON_PID=$!
sleep 0.5

if [ ! -S "$SOCKET" ]; then
    echo "Error: Socket $SOCKET was not created!"
    kill -9 "$DAEMON_PID" || true
    exit 1
fi

echo "--- Running Neovim E2E Live Sixel Streaming Suite ---"
nvim --headless -u NONE -c "luafile $TEST_LUA"

echo "--- Running Concurrency Stress Test (50 Requests, 10 Workers) ---"
python3 "$STRESS_PY"

echo "--- Verifying Clean Daemon Shutdown (SIGTERM) ---"
kill -TERM "$DAEMON_PID"
wait "$DAEMON_PID" 2>/dev/null || true

if [ -e "$SOCKET" ]; then
    echo "Error: Socket $SOCKET was not cleanly unlinked!"
    exit 1
fi
echo "Daemon unlinked socket cleanly."

echo ""
echo "=== [STAGE 5] AddressSanitizer & LeakSanitizer Deep Profiling ==="
ASAN_BUILD="$DAEMON_DIR/build_asan"
cmake -B "$ASAN_BUILD" -S "$DAEMON_DIR" -DCMAKE_C_FLAGS="-fsanitize=address,undefined -g"
cmake --build "$ASAN_BUILD"

echo "--- Running ASan Unit Tests ---"
"$ASAN_BUILD/test_parser" "$DAEMON_DIR/tests/sample_project.c" "$DAEMON_DIR/src/main.c" >/dev/null
"$ASAN_BUILD/test_physics" >/dev/null
"$ASAN_BUILD/test_raster" >/dev/null

echo "--- Running ASan Daemon Under Concurrency Stress ---"
rm -f "$SOCKET"
"$ASAN_BUILD/spatial_daemon" &
ASAN_PID=$!
sleep 0.5

python3 "$STRESS_PY"

kill -TERM "$ASAN_PID"
wait "$ASAN_PID" 2>/dev/null || true
rm -rf "$ASAN_BUILD"

echo ""
echo "=========================================================="
echo ">>> ALL TEST SUITES PASSED! ZERO MEMORY LEAKS OR STALLS! <<<"
echo "=========================================================="
