local ipc = require("spatial_nav.ipc")
local renderer = require("spatial_nav.renderer")

local M = {}

M.defaults = {
  socket_path = "/tmp/spatial_nav.sock",
  sixel_cache_path = "/tmp/spatial_nav_render.sixel",
  render_strategy = "auto", -- "auto" | "tty_overlay" | "wezterm" | "suspend"
}

M.options = vim.deepcopy(M.defaults)

--- Configure plugin options
--- @param opts table|nil User configuration options
function M.setup(opts)
  if opts then
    M.options = vim.tbl_deep_extend("force", M.defaults, opts)
  end
end

--- Open Spatial Navigator and render 3D call graph directly to the host terminal
--- @param filepath_override string|nil Optional target file path
--- @param is_headless boolean|nil If true, non-interactive mode for testing
--- @return table|nil handle Request handle
function M.open(filepath_override, is_headless)
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

  local filename = vim.fs.basename(filepath)
  vim.notify(string.format("[SpatialNav] Generating 3D spatial map for %s...", filename), vim.log.levels.INFO)

  local chunks = {}

  -- Start async IPC streaming from the C daemon
  local req_handle = ipc.request_graph(M.options.socket_path, filepath, {
    on_connect = function()
      -- Daemon accepted connection
    end,
    on_chunk = function(chunk)
      table.insert(chunks, chunk)
    end,
    on_complete = function()
      local payload = table.concat(chunks)
      if #payload == 0 then
        vim.notify("[SpatialNav] Received empty response from daemon.", vim.log.levels.WARN)
        return
      end

      -- Route directly to host terminal bypassing libvterm
      renderer.render(
        M.options.render_strategy,
        payload,
        M.options.sixel_cache_path,
        filepath,
        is_headless
      )
    end,
    on_error = function(err)
      local msg = string.format("[SpatialNav] Socket IPC failed: %s\nEnsure spatial_daemon is running at %s", tostring(err), M.options.socket_path)
      vim.notify(msg, vim.log.levels.ERROR)
    end,
  })

  return req_handle
end

return M
