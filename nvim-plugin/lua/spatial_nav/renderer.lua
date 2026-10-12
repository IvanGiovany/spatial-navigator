local M = {}

--- Wrap escape sequences for tmux passthrough if running inside tmux
--- @param data string Raw escape sequence
--- @return string Escaped data
local function wrap_passthrough(data)
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

--- Render interactive 3D navigation overlay directly on the controlling TTY
--- Incorporates Version 2:
---  1. Zero-clear cursor homing (\x1b[H) without \x1b[?1049l exit corruption.
---  2. Synchronized Output (\x1b[?2026h / \x1b[?2026l) to eliminate tearing.
---  3. Interactive input navigation loop: h/j/k/l (rotate), +/- (zoom), r (reset), q/Esc (exit).
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
    return false
  end

  local tty, err = io.open(tty_path, "wb")
  if not tty then
    vim.notify(string.format("[SpatialNav] Cannot open %s: %s", tty_path, tostring(err)), vim.log.levels.ERROR)
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
  local enter_seq = wrap_passthrough("\27[?25l\27[2J\27[H")
  tty:write(enter_seq)

  local function draw_frame(payload)
    -- Synchronized output wrapper + zero-clear cursor home
    local sync_start = "\27[?2026h\27[H"
    local hud = string.format(
      "\r\n\27[1;30;46m Spatial Codebase Navigator V2 \27[0m \27[97m%s\27[0m \27[90m| [h/j/k/l] Orbit | [+/-] Zoom | [r] Reset | [q] Exit\27[0m\r\n",
      filename
    )
    local sync_end = "\27[?2026l"

    tty:write(wrap_passthrough(sync_start))
    tty:write(wrap_passthrough(payload))
    tty:write(wrap_passthrough(hud))
    tty:write(wrap_passthrough(sync_end))
    tty:flush()
  end

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
      -- Synchronously fetch next frame from local socket for crisp response
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

      -- Wait for frame chunks to arrive
      vim.wait(300, function() return done end, 5)

      if #next_chunks > 0 then
        draw_frame(table.concat(next_chunks))
      end
    end
  end

  -- Clean exit: show cursor, clear screen to avoid remnant artifacts, redraw Neovim grid
  -- CRITICAL: DO NOT EMIT \x1b[?1049l (Neovim owns alternate screen)
  local exit_seq = wrap_passthrough("\27[?25h\27[2J\27[H")
  tty:write(exit_seq)
  tty:flush()
  tty:close()

  -- Repaint Neovim cleanly
  vim.cmd("redraw!")
  return true
end

return M
