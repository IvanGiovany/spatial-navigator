local ui = require("spatial_nav.ui")
local ipc = require("spatial_nav.ipc")

local M = {}

M.defaults = {
  socket_path = "/tmp/spatial_nav.sock",
  width_ratio = 0.85,
  height_ratio = 0.85,
  border = "rounded",
}

M.options = vim.deepcopy(M.defaults)

--- Configure plugin options
--- @param opts table|nil User configuration options
function M.setup(opts)
  if opts then
    M.options = vim.tbl_deep_extend("force", M.defaults, opts)
  end
end

--- Open Spatial Navigator floating window for the specified file or current buffer
--- @param filepath_override string|nil Optional target file path
--- @return table|nil win_info Handle for window and terminal channel
function M.open(filepath_override)
  local filepath = filepath_override
  if not filepath or filepath == "" then
    filepath = vim.api.nvim_buf_get_name(0)
  end

  if not filepath or filepath == "" then
    vim.notify("[SpatialNav] Current buffer has no file path. Please open a C file or specify a path.", vim.log.levels.WARN)
    return nil
  end

  -- Expand to normalized absolute path
  filepath = vim.fn.fnamemodify(filepath, ":p")

  -- Check if file is readable
  if vim.fn.filereadable(filepath) ~= 1 then
    vim.notify(string.format("[SpatialNav] File not found or unreadable: '%s'", filepath), vim.log.levels.WARN)
  end

  -- Open centered floating terminal window
  local win_info = ui.create_window(M.options)
  if not win_info then
    return nil
  end

  -- Display initial progress status in terminal window
  ui.append_text(win_info.chan_id, "\x1b[?25l\x1b[2J\x1b[H") -- Hide cursor, clear screen, home
  ui.append_text(win_info.chan_id, string.format("== Spatial Codebase Navigator ==\r\nTarget: %s\r\nConnecting to daemon at %s...\r\n", filepath, M.options.socket_path))

  -- Start async IPC streaming from the C daemon
  local req_handle
  req_handle = ipc.request_graph(M.options.socket_path, filepath, {
    on_connect = function()
      ui.append_text(win_info.chan_id, "Connected to daemon. Parsing AST and running 3D physics...\r\n")
    end,
    on_chunk = function(chunk)
      ui.append_raw(win_info.chan_id, chunk)
    end,
    on_complete = function()
      -- Daemon completed streaming and closed the socket connection
    end,
    on_error = function(err)
      local msg = string.format("\r\n[SpatialNav] IPC failed: %s\r\nEnsure spatial_daemon is running at %s\r\n", tostring(err), M.options.socket_path)
      ui.append_text(win_info.chan_id, msg)
      vim.notify(string.format("[SpatialNav] Error connecting to daemon: %s", tostring(err)), vim.log.levels.ERROR)
    end,
  })

  -- Attach abort handler when the floating window is dismissed
  ui.set_close_callback(win_info, function()
    if req_handle then
      ipc.abort(req_handle)
      req_handle = nil
    end
  end)

  return win_info
end

return M
