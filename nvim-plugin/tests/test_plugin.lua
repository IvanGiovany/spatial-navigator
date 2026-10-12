-- Automated headless test suite for spatial-navigator Neovim plugin
-- Run with: nvim --headless -u NONE -c "luafile nvim-plugin/tests/test_plugin.lua"

local uv = vim.uv or vim.loop

local function log(msg)
  io.stdout:write("[TEST_LUA] " .. msg .. "\n")
  io.stdout:flush()
end

local function assert_true(cond, msg)
  if not cond then
    io.stderr:write("[TEST FAILED] " .. (msg or "Assertion failed") .. "\n")
    io.stderr:flush()
    os.exit(1)
  end
end

-- Ensure nvim-plugin/lua is in package.path and runtimepath
local info = debug.getinfo(1, "S")
local script_path = (info.source:sub(1, 1) == "@") and info.source:sub(2) or info.source
local script_dir = vim.fs.dirname(vim.fs.normalize(script_path))
local plugin_root = vim.fs.dirname(script_dir)
local lua_dir = plugin_root .. "/lua"

package.path = lua_dir .. "/?.lua;" .. lua_dir .. "/?/init.lua;" .. package.path
vim.opt.runtimepath:append(plugin_root)

log("Plugin root: " .. plugin_root)
log("Lua package.path updated with: " .. lua_dir)

--------------------------------------------------------------------------------
-- Test 1: Module Loading & Configuration
--------------------------------------------------------------------------------
log("Running Test 1: Module loading & setup verification...")
local spatial_nav = require("spatial_nav")
assert_true(spatial_nav ~= nil, "Failed to require spatial_nav")

spatial_nav.setup({
  socket_path = "/tmp/spatial_nav.sock",
  sixel_cache_path = "/tmp/spatial_nav_render_test.sixel",
  render_strategy = "auto",
})

assert_true(spatial_nav.options.socket_path == "/tmp/spatial_nav.sock", "socket_path not set")
assert_true(spatial_nav.options.sixel_cache_path == "/tmp/spatial_nav_render_test.sixel", "sixel_cache_path not set")
assert_true(spatial_nav.options.render_strategy == "auto", "render_strategy not set")
log("Test 1 PASSED: Module loading & options setup verified.")

--------------------------------------------------------------------------------
-- Test 2: Direct Host Terminal Renderer (bypassing libvterm)
--------------------------------------------------------------------------------
log("Running Test 2: Renderer module and /dev/tty alternate-screen overlay...")
local renderer = require("spatial_nav.renderer")
assert_true(renderer ~= nil, "Failed to require renderer")

-- Test cache file saving
local dummy_sixel = "\27Pq\"1;1;100;100#0;2;0;0;0#0-!100~-\27\\"
local test_cache = "/tmp/spatial_nav_render_test.sixel"
local saved = renderer.save_cache_file(test_cache, dummy_sixel)
assert_true(saved == true, "Failed to save cache file")
assert_true(vim.fn.filereadable(test_cache) == 1, "Cache file not readable on disk")

-- Test /dev/tty overlay in headless mode (is_headless = true avoids blocking on getchar)
local rendered = renderer.render("auto", dummy_sixel, test_cache, "/dummy/sample.c", true)
assert_true(rendered == true, "render() failed in headless mode")
log("Test 2 PASSED: Direct host terminal renderer verified.")

--------------------------------------------------------------------------------
-- Test 3: Resilient IPC Error Handling (Offline Socket)
--------------------------------------------------------------------------------
log("Running Test 3: IPC error handling with offline socket...")
local ipc = require("spatial_nav.ipc")

local error_fired = false
local test_done = false

ipc.request_graph("/tmp/nonexistent_test_sock_12345.sock", "/dummy/path.c", {
  on_error = function(err)
    assert_true(err ~= nil, "Expected error object")
    error_fired = true
    test_done = true
  end,
  on_complete = function()
    assert_true(false, "Should not complete on nonexistent socket")
  end,
})

-- Run event loop until error callback fires (max 3 seconds)
vim.wait(3000, function()
  return test_done
end, 20)

assert_true(error_fired == true, "Expected on_error callback to fire for nonexistent socket")
log("Test 3 PASSED: Offline socket error handled gracefully.")

--------------------------------------------------------------------------------
-- Test 4: End-to-End Live Socket Streaming (if daemon socket is active)
--------------------------------------------------------------------------------
local live_sock = "/tmp/spatial_nav.sock"
local function is_socket_live(sock_path)
  local stat = vim.uv.fs_stat(sock_path)
  if not stat or stat.type ~= "socket" then
    return false
  end
  local pipe = vim.uv.new_pipe(false)
  local ok = false
  local done = false
  pipe:connect(sock_path, function(err)
    if not err then
      ok = true
    end
    pipe:close()
    done = true
  end)
  vim.wait(200, function()
    return done
  end, 10)
  return ok
end

local is_live = is_socket_live(live_sock)

if is_live then
  log("Running Test 4: Live daemon socket end-to-end streaming...")
  local chunks = {}
  local completed = false
  local connected = false
  local error_msg = nil

  local target_sample = "/mnt/c/Users/Ivan/Documents/spatial-navigator/daemon/tests/sample_project.c"

  local handle = ipc.request_graph(live_sock, target_sample, {
    on_connect = function()
      connected = true
    end,
    on_chunk = function(chunk)
      table.insert(chunks, chunk)
    end,
    on_complete = function()
      completed = true
    end,
    on_error = function(err)
      error_msg = tostring(err)
    end,
  })

  vim.wait(5000, function()
    return completed or error_msg ~= nil
  end, 20)

  assert_true(error_msg == nil, "IPC stream encountered error: " .. tostring(error_msg))
  assert_true(connected == true, "IPC client failed to connect to daemon")
  assert_true(completed == true, "IPC stream timed out waiting for EOF")

  local payload = table.concat(chunks)
  log(string.format("Live Sixel stream received: %d bytes across %d chunks", #payload, #chunks))

  assert_true(#payload >= 500, "Payload too small: " .. tostring(#payload))
  -- Check DCS header (\x1bP)
  local dcs_header = string.sub(payload, 1, 2)
  assert_true(dcs_header == "\27P", "Payload does not start with DCS header \\x1bP")
  -- Check ST terminator (\x1b\)
  local trimmed = payload:gsub("%s+$", "")
  local st_term = string.sub(trimmed, -2)
  assert_true(st_term == "\27\\", "Payload does not end with ST terminator \\x1b\\")

  -- Render payload to test cache
  renderer.render("auto", payload, test_cache, target_sample, true)
  assert_true(vim.fn.getfsize(test_cache) == #payload, "Cache file size mismatch")

  log("Test 4 PASSED: Successfully received and validated 3D Sixel stream from daemon.")

  --------------------------------------------------------------------------------
  -- Test 5: Command Dispatch & open() API
  --------------------------------------------------------------------------------
  log("Running Test 5: Command dispatch and open() lifecycle...")
  dofile(plugin_root .. "/plugin/spatial_nav.lua")

  local cmds = vim.api.nvim_get_commands({})
  assert_true(cmds["SpatialNav"] ~= nil, ":SpatialNav command was not registered!")

  -- Execute open in non-interactive headless mode
  local req_h = spatial_nav.open(target_sample, true)
  assert_true(req_h ~= nil, "spatial_nav.open returned nil handle")

  -- Wait briefly for connection and streaming
  vim.wait(1500, function()
    return false
  end, 50)

  log("Test 5 PASSED: :SpatialNav command registration and open() verified.")
else
  log("Test 4 & 5 SKIPPED: /tmp/spatial_nav.sock is not running (will be tested in test_phase6.sh).")
end

log(">>> ALL LUA UNIT TESTS PASSED SUCCESSFULLY! <<<")
vim.cmd("qall!")
