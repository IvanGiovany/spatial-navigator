local M = {}

--- Wrap escape sequences for tmux passthrough if running inside tmux
--- @param data string Raw escape sequence
--- @return string Escaped data
local function wrap_tmux(data)
  if not vim.env.TMUX or vim.env.TMUX == "" then
    return data
  end
  -- In tmux, escape sequences must be wrapped in DCS tmux passthrough,
  -- and internal ESC (0x1B) characters must be doubled.
  local escaped = data:gsub("\27", "\27\27")
  return "\27Ptmux;\27" .. escaped .. "\27\\"
end

--- Save raw Sixel payload to a persistent cache file on disk
--- @param sixel_path string Target file path (e.g. /tmp/spatial_nav_render.sixel)
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

--- Render Sixel directly to the host terminal using the alternate screen buffer on /dev/tty
--- This completely bypasses Neovim's internal libvterm which would otherwise strip DCS sequences.
--- @param payload string Raw Sixel escape stream (\x1bP... \x1b\)
--- @param filepath string Source file path for status footer
--- @param is_headless boolean|nil If true, do not block on getchar()
--- @return boolean success
function M.render_tty_overlay(payload, filepath, is_headless)
  local has_ui = (#vim.api.nvim_list_uis() > 0) and not is_headless
  if not has_ui then
    -- Headless mode or test execution without attached UI:
    -- Payload is already safely preserved in the cache file.
    return true
  end

  local tty, err = io.open("/dev/tty", "wb")
  if not tty then
    vim.notify(string.format("[SpatialNav] Cannot open /dev/tty: %s. Falling back to suspend mode.", tostring(err)), vim.log.levels.WARN)
    return false
  end

  local filename = vim.fs.basename(filepath) or filepath

  -- Build alternate screen activation and canvas preparation
  -- \x1b[?1049h: Switch to terminal alternate screen buffer
  -- \x1b[?25l: Hide cursor
  -- \x1b[2J\x1b[H: Clear screen, home cursor to row 1, col 1
  local enter_seq = wrap_tmux("\27[?1049h\27[?25l\27[2J\27[H")
  tty:write(enter_seq)

  -- Write the raw Sixel graphic stream directly to the host terminal
  local sixel_data = wrap_tmux(payload)
  tty:write(sixel_data)

  -- Render an intuitive footer prompt below the Sixel graphic
  local footer = string.format(
    "\r\n\27[1;30;46m Spatial Codebase Navigator \27[0m \27[90m| Target: %s | Press [q], [Esc], or [Enter] to return\27[0m\r\n",
    filename
  )
  tty:write(wrap_tmux(footer))
  tty:flush()

  -- Block cleanly waiting for user acknowledgement
  pcall(vim.fn.getchar)

  -- Restore normal terminal screen buffer and show cursor
  -- \x1b[?1049l: Restore normal screen buffer
  -- \x1b[?25h: Restore cursor
  local exit_seq = wrap_tmux("\27[?1049l\27[?25h")
  tty:write(exit_seq)
  tty:flush()
  tty:close()

  -- Request Neovim to repaint its UI cleanly
  vim.cmd("redraw!")

  return true
end

--- Render using WezTerm CLI to spawn a split pane or overlay
--- @param sixel_path string Path to cached Sixel file
--- @return boolean success
function M.render_wezterm(sixel_path)
  if vim.fn.executable("wezterm") ~= 1 then
    return false
  end

  local pane_id = vim.env.WEZTERM_PANE
  local cmd
  if pane_id and pane_id ~= "" then
    cmd = string.format(
      "wezterm cli split-pane --right --percent 50 -- bash -c 'cat %s; echo -e \"\\n\\033[36m[SpatialNav]\\033[0m Press Enter to close\"; read -r'",
      vim.fn.shellescape(sixel_path)
    )
  else
    cmd = string.format(
      "wezterm cli spawn -- bash -c 'cat %s; echo -e \"\\n\\033[36m[SpatialNav]\\033[0m Press Enter to close\"; read -r'",
      vim.fn.shellescape(sixel_path)
    )
  end

  local job_id = vim.fn.jobstart(cmd, { detach = true })
  return job_id > 0
end

--- Render by suspending Neovim TUI temporarily
--- @param sixel_path string Path to cached Sixel file
--- @return boolean success
function M.render_suspend(sixel_path)
  local cmd = string.format(
    "silent !cat %s && read -n 1 -s -r -p $'\n\\e[36m[SpatialNav]\\e[0m Press any key to return...'",
    vim.fn.shellescape(sixel_path)
  )
  vim.cmd(cmd)
  vim.cmd("redraw!")
  return true
end

--- Main renderer entrypoint
--- @param strategy string 'auto' | 'tty_overlay' | 'wezterm' | 'suspend'
--- @param payload string Raw Sixel bytes
--- @param sixel_path string Path to cache file
--- @param filepath string Target source file path
--- @param is_headless boolean|nil
--- @return boolean success
function M.render(strategy, payload, sixel_path, filepath, is_headless)
  -- Step 1: Always persist payload to cache file
  M.save_cache_file(sixel_path, payload)

  -- Step 2: Route according to strategy
  if strategy == "wezterm" then
    if M.render_wezterm(sixel_path) then
      return true
    end
    -- Fallback to tty_overlay if wezterm failed
    return M.render_tty_overlay(payload, filepath, is_headless)
  elseif strategy == "suspend" then
    return M.render_suspend(sixel_path)
  elseif strategy == "auto" then
    -- If running under WezTerm with wezterm CLI available, prefer native pane
    if vim.env.WEZTERM_PANE and vim.fn.executable("wezterm") == 1 then
      if M.render_wezterm(sixel_path) then
        return true
      end
    end
    -- Default universal strategy
    return M.render_tty_overlay(payload, filepath, is_headless)
  else
    -- Default: tty_overlay
    return M.render_tty_overlay(payload, filepath, is_headless)
  end
end

return M
