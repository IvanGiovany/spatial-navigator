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
  width_ratio = 0.8,
  height_ratio = 0.7,
})

assert_true(spatial_nav.options.socket_path == "/tmp/spatial_nav.sock", "socket_path not set")
assert_true(spatial_nav.options.width_ratio == 0.8, "width_ratio not set")
assert_true(spatial_nav.options.height_ratio == 0.7, "height_ratio not set")
log("Test 1 PASSED: Module loading & options setup verified.")

--------------------------------------------------------------------------------
-- Test 2: UI Floating Window & Terminal Channel Lifecycle
--------------------------------------------------------------------------------
log("Running Test 2: Floating window and terminal lifecycle...")
local ui = require("spatial_nav.ui")
local win_info = ui.create_window({ width_ratio = 0.8, height_ratio = 0.7, border = "rounded" })

assert_true(win_info ~= nil, "create_window returned nil")
assert_true(win_info.buf and vim.api.nvim_buf_is_valid(win_info.buf), "Buffer is not valid")
assert_true(win_info.win and vim.api.nvim_win_is_valid(win_info.win), "Window is not valid")
assert_true(win_info.chan_id and win_info.chan_id > 0, "Terminal channel ID invalid")

local closed_called = false
ui.set_close_callback(win_info, function()
  closed_called = true
end)

-- Append text into terminal channel safely
ui.append_text(win_info.chan_id, "Hello from SpatialNav Test\r\n")
ui.append_raw(win_info.chan_id, "\x1b[32mColored Output\x1b[0m\r\n")

-- Close window
vim.api.nvim_win_close(win_info.win, true)
assert_true(closed_called == true, "Close callback was not called")
log("Test 2 PASSED: UI window, terminal channel, and cleanup verified.")

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
local is_live = vim.fn.filereadable(live_sock) == 1 or vim.uv.fs_stat(live_sock) ~= nil

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

  log("Test 4 PASSED: Successfully received and validated 3D Sixel stream from daemon.")

  --------------------------------------------------------------------------------
  -- Test 5: Command Dispatch & open() API
  --------------------------------------------------------------------------------
  log("Running Test 5: Command dispatch and open() lifecycle...")
  dofile(plugin_root .. "/plugin/spatial_nav.lua")

  local cmds = vim.api.nvim_get_commands({})
  assert_true(cmds["SpatialNav"] ~= nil, ":SpatialNav command was not registered!")

  local win_info_cmd = spatial_nav.open(target_sample)
  assert_true(win_info_cmd ~= nil, "spatial_nav.open returned nil")
  assert_true(vim.api.nvim_win_is_valid(win_info_cmd.win), "Window from open() is not valid")

  -- Wait briefly for connection and streaming
  vim.wait(1500, function()
    return false
  end, 50)

  -- Cleanly close window
  vim.api.nvim_win_close(win_info_cmd.win, true)
  log("Test 5 PASSED: :SpatialNav command registration and open() verified.")
else
  log("Test 4 & 5 SKIPPED: /tmp/spatial_nav.sock is not running (will be tested in test_phase6.sh).")
end

log(">>> ALL LUA UNIT TESTS PASSED SUCCESSFULLY! <<<")
vim.cmd("qall!")
