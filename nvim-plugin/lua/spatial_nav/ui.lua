local M = {}

--- Create and center a floating window containing a terminal buffer
--- @param opts table Configuration options (width_ratio, height_ratio, border)
--- @return table|nil win_info Handle with buf, win, chan_id or nil on error
function M.create_window(opts)
  opts = opts or {}
  local width_ratio = opts.width_ratio or 0.85
  local height_ratio = opts.height_ratio or 0.85
  local border = opts.border or "rounded"

  local total_cols = vim.o.columns
  local total_lines = vim.o.lines - vim.o.cmdheight - 1
  if total_lines < 10 then total_lines = 10 end

  local width = math.max(40, math.floor(total_cols * width_ratio))
  local height = math.max(10, math.floor(total_lines * height_ratio))
  local col = math.floor((vim.o.columns - width) / 2)
  local row = math.floor((vim.o.lines - height) / 2)

  -- Create scratch unlisted buffer
  local buf = vim.api.nvim_create_buf(false, true)
  if not buf or buf == 0 then
    vim.notify("[SpatialNav] Failed to create terminal buffer", vim.log.levels.ERROR)
    return nil
  end

  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  vim.bo[buf].swapfile = false

  -- Floating window configuration
  local win_config = {
    relative = "editor",
    width = width,
    height = height,
    col = col,
    row = row,
    style = "minimal",
    border = border,
    title = " 3D Spatial Codebase Navigator ",
    title_pos = "center",
  }

  local win = vim.api.nvim_open_win(buf, true, win_config)
  if not win or win == 0 then
    vim.api.nvim_buf_delete(buf, { force = true })
    vim.notify("[SpatialNav] Failed to open floating window", vim.log.levels.ERROR)
    return nil
  end

  -- Spawn pseudo-terminal stream channel on the buffer
  local chan_id = vim.api.nvim_open_term(buf, {})

  local win_info = {
    buf = buf,
    win = win,
    chan_id = chan_id,
    on_close = nil,
  }

  local function close_window()
    if win_info.on_close then
      win_info.on_close()
      win_info.on_close = nil
    end
    if vim.api.nvim_win_is_valid(win) then
      vim.api.nvim_win_close(win, true)
    end
  end

  -- Interactive keybindings to dismiss window
  local key_opts = { buffer = buf, nowait = true, noremap = true, silent = true }
  vim.keymap.set("n", "q", close_window, key_opts)
  vim.keymap.set("n", "<Esc>", close_window, key_opts)
  vim.keymap.set("n", "<C-c>", close_window, key_opts)

  -- Clean up resources when buffer or window is closed
  local group = vim.api.nvim_create_augroup("SpatialNavCleanup_" .. buf, { clear = true })
  vim.api.nvim_create_autocmd({ "BufWipeout", "WinClosed" }, {
    group = group,
    buffer = buf,
    once = true,
    callback = function()
      if win_info.on_close then
        win_info.on_close()
        win_info.on_close = nil
      end
    end,
  })

  return win_info
end

--- Register a callback triggered when the window is closed
--- @param win_info table
--- @param callback function
function M.set_close_callback(win_info, callback)
  if win_info then
    win_info.on_close = callback
  end
end

--- Append formatted text to the terminal channel
--- @param chan_id number
--- @param text string
function M.append_text(chan_id, text)
  if chan_id and chan_id > 0 and text then
    vim.schedule(function()
      pcall(vim.api.nvim_chan_send, chan_id, text)
    end)
  end
end

--- Append raw byte stream (e.g. Sixel escape sequence) to terminal channel
--- @param chan_id number
--- @param data string
function M.append_raw(chan_id, data)
  if chan_id and chan_id > 0 and data then
    vim.schedule(function()
      pcall(vim.api.nvim_chan_send, chan_id, data)
    end)
  end
end

return M
