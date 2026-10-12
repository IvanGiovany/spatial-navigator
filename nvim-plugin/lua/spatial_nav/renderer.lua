local M = {}

--- Wrap escape sequences for tmux/screen passthrough if running inside a multiplexer
--- @param data string Raw escape sequence
--- @return string Escaped data
function M.wrap_passthrough(data)
  if vim.env.TMUX and vim.env.TMUX ~= "" then
    local escaped = data:gsub("\27", "\27\27")
    return "\27Ptmux;" .. escaped .. "\27\\"
  elseif vim.env.TERM and vim.startswith(vim.env.TERM, "screen") then
    local escaped = data:gsub("\27", "\27\27")
    return "\27P" .. escaped .. "\27\\"
  end
  return data
end

--- Save raw Sixel payload to cache file on disk
--- @param sixel_path string Target file path
--- @param payload string Raw Sixel bytes
--- @return boolean success
function M.save_cache_file(sixel_path, payload)
  local f, err = io.open(sixel_path, "wb")
  if not f then
    vim.notify(string.format("[SpatialNav] Failed to write cache file %s: %s", sixel_path, tostring(err)), vim.log.levels.WARN)
    return false
  end
  f:write(payload)
  f:close()
  return true
end

--- Dynamically resolve the controlling pseudo-terminal device path (e.g., /dev/pts/0 in WSL)
--- @return string|nil tty_path
function M.get_tty_device()
  local p = io.popen("tty 2>/dev/null")
  if p then
    local out = p:read("*a")
    p:close()
    if out then
      local trimmed = vim.trim(out)
      if trimmed ~= "" and trimmed ~= "not a tty" and vim.startswith(trimmed, "/dev/") then
        local f = io.open(trimmed, "wb")
        if f then
          f:close()
          return trimmed
        end
      end
    end
  end

  for _, candidate in ipairs({ "/dev/tty", "/dev/pts/0", "/dev/stdout" }) do
    local f = io.open(candidate, "wb")
    if f then
      f:close()
      return candidate
    end
  end

  return nil
end

--- Forcefully restore host terminal state and invalidate/repaint Neovim's UI grid
--- Guaranteed to execute cleanly on user exit (q, Esc), socket drops, or errors
--- @param tty file*|nil Optional open TTY handle
function M.cleanup_terminal(tty)
  -- 1. Send ANSI escape sequence to clear screen, home cursor, and show cursor
  local reset_seq = M.wrap_passthrough("\27[2J\27[H\27[?25h")

  if tty then
    pcall(function()
      tty:write(reset_seq)
      tty:flush()
      tty:close()
    end)
  else
    -- Fallback: open TTY device if handle was not provided or already closed
    local tty_path = M.get_tty_device()
    if tty_path then
      local f = io.open(tty_path, "wb")
      if f then
        pcall(function()
          f:write(reset_seq)
          f:flush()
          f:close()
        end)
      end
    end
  end

  -- 2. Force Neovim to completely invalidate and repaint its UI grid inside vim.schedule
  vim.schedule(function()
    pcall(function()
      vim.cmd("mode | redraw!")
    end)
  end)
end

--- Render interactive 3D navigation overlay directly on the controlling TTY
--- @param ipc table IPC module
--- @param socket_path string Socket address
--- @param filepath string Target source file path
--- @param initial_payload string Initial rendered Sixel or Braille frame
--- @param is_headless boolean|nil If true, test mode
--- @return boolean success
function M.render_interactive_overlay(ipc, socket_path, filepath, initial_payload, is_headless)
  local has_ui = (#vim.api.nvim_list_uis() > 0) and not is_headless
  if not has_ui then
    return true
  end

  local tty_path = M.get_tty_device()
  if not tty_path then
    vim.notify("[SpatialNav] No accessible TTY device found.", vim.log.levels.ERROR)
    M.cleanup_terminal(nil)
    return false
  end

  local tty, err = io.open(tty_path, "wb")
  if not tty then
    vim.notify(string.format("[SpatialNav] Cannot open %s: %s", tty_path, tostring(err)), vim.log.levels.ERROR)
    M.cleanup_terminal(nil)
    return false
  end

  local filename = vim.fs.basename(filepath) or filepath
  local discovery = require("spatial_nav.discovery")
  local term_info = discovery.probe_terminal()

  local cam_state = {
    yaw = 0.0,
    pitch = 0.0,
    zoom = 450.0,
    width = term_info.width,
    height = term_info.height,
  }

  -- Prepare terminal canvas: hide cursor, clear screen to home
  local enter_seq = M.wrap_passthrough("\27[?25l\27[2J\27[H")
  tty:write(enter_seq)

  local function draw_frame(payload)
    local sync_start = "\27[?2026h\27[H"
    local hud = string.format(
      "\r\n\27[1;30;46m Spatial Codebase Navigator V2 \27[0m \27[97m%s\27[0m \27[90m| [h/j/k/l] Orbit | [+/-] Zoom | [r] Reset | [q/Esc] Exit\27[0m\r\n",
      filename
    )
    local sync_end = "\27[?2026l"

    tty:write(M.wrap_passthrough(sync_start))
    tty:write(M.wrap_passthrough(payload))
    tty:write(M.wrap_passthrough(hud))
    tty:write(M.wrap_passthrough(sync_end))
    tty:flush()
  end

  -- Wrap interaction in protected call so terminal is ALWAYS restored even on unexpected errors
  local run_status, run_err = pcall(function()
    -- Display initial frame
    draw_frame(initial_payload)

    -- Interactive event loop
    local running = true
    while running do
      local ok, char = pcall(vim.fn.getchar)
      if not ok then break end

      local key = type(char) == "number" and vim.fn.nr2char(char) or tostring(char)

      local dirty = false
      if key == "q" or key == "\27" then -- 'q' or Esc
        running = false
      elseif key == "h" or key == "Left" then
        cam_state.yaw = cam_state.yaw - 0.15
        dirty = true
      elseif key == "l" or key == "Right" then
        cam_state.yaw = cam_state.yaw + 0.15
        dirty = true
      elseif key == "j" or key == "Down" then
        cam_state.pitch = cam_state.pitch + 0.12
        dirty = true
      elseif key == "k" or key == "Up" then
        cam_state.pitch = cam_state.pitch - 0.12
        dirty = true
      elseif key == "+" or key == "=" then
        cam_state.zoom = math.max(100.0, cam_state.zoom - 30.0)
        dirty = true
      elseif key == "-" or key == "_" then
        cam_state.zoom = math.min(1200.0, cam_state.zoom + 30.0)
        dirty = true
      elseif key == "r" then
        cam_state.yaw = 0.0
        cam_state.pitch = 0.0
        cam_state.zoom = 450.0
        dirty = true
      end

      if dirty and running then
        local next_chunks = {}
        local done = false
        ipc.request_graph(socket_path, {
          filepath = filepath,
          width = cam_state.width,
          height = cam_state.height,
          yaw = cam_state.yaw,
          pitch = cam_state.pitch,
          zoom = cam_state.zoom,
        }, {
          on_chunk = function(c) table.insert(next_chunks, c) end,
          on_complete = function() done = true end,
          on_error = function() done = true end,
        })

        vim.wait(300, function() return done end, 5)

        if #next_chunks > 0 then
          draw_frame(table.concat(next_chunks))
        end
      end
    end
  end)

  -- Always clean up host terminal and force Neovim mode | redraw!
  M.cleanup_terminal(tty)

  if not run_status then
    vim.notify("[SpatialNav] Error during navigation: " .. tostring(run_err), vim.log.levels.ERROR)
    return false
  end

  return true
end

return M
