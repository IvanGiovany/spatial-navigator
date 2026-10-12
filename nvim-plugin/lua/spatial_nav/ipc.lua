local M = {}

--- Asynchronously requests 3D spatial graph from the C daemon over Unix domain socket
--- @param socket_path string Path to Unix domain socket (e.g. /tmp/spatial_nav.sock)
--- @param filepath string Target C source file path to parse and visualize
--- @param callbacks table Table with on_connect, on_chunk, on_complete, on_error callbacks
--- @return table handle Cancellation handle with abort() method
function M.request_graph(socket_path, filepath, callbacks)
  callbacks = callbacks or {}
  local on_connect = callbacks.on_connect or function() end
  local on_chunk = callbacks.on_chunk or function() end
  local on_complete = callbacks.on_complete or function() end
  local on_error = callbacks.on_error or function() end

  local pipe = vim.uv.new_pipe(false)
  if not pipe then
    on_error("Failed to create libuv pipe")
    return nil
  end

  local handle = {
    pipe = pipe,
    is_closed = false,
  }

  local function safe_close()
    if not handle.is_closed then
      handle.is_closed = true
      if pipe and not pipe:is_closing() then
        pipe:close()
      end
    end
  end

  pipe:connect(socket_path, function(conn_err)
    if conn_err then
      safe_close()
      vim.schedule(function()
        on_error(conn_err)
      end)
      return
    end

    vim.schedule(function()
      on_connect()
    end)

    -- Send target filepath ending with newline
    local payload = filepath .. "\n"
    pipe:write(payload, function(write_err)
      if write_err then
        safe_close()
        vim.schedule(function()
          on_error(write_err)
        end)
        return
      end

      -- Stream incoming Sixel payload chunks
      pipe:read_start(function(read_err, chunk)
        if read_err then
          safe_close()
          vim.schedule(function()
            on_error(read_err)
          end)
          return
        end

        if chunk then
          on_chunk(chunk)
        else
          -- EOF: daemon finished streaming and closed client socket
          safe_close()
          vim.schedule(function()
            on_complete()
          end)
        end
      end)
    end)
  end)

  return handle
end

--- Abort an active socket request
--- @param handle table Request handle returned by request_graph
function M.abort(handle)
  if handle and not handle.is_closed then
    handle.is_closed = true
    if handle.pipe and not handle.pipe:is_closing() then
      handle.pipe:close()
    end
  end
end

return M
