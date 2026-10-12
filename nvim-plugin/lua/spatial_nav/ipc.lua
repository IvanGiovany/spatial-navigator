local M = {}

--- Asynchronously requests 3D spatial graph from the C daemon over Unix domain socket
--- Supports Version 2 parameter headers: REQ W=<w> H=<h> YAW=<yaw> PITCH=<pitch> ZOOM=<zoom> PATH=<path>
--- @param socket_path string Path to Unix domain socket
--- @param params table Request parameters { filepath, width, height, yaw, pitch, zoom, braille }
--- @param callbacks table Table with on_connect, on_chunk, on_complete, on_error callbacks
--- @return table handle Cancellation handle with abort() method
function M.request_graph(socket_path, params, callbacks)
  callbacks = callbacks or {}
  local on_connect = callbacks.on_connect or function() end
  local on_chunk = callbacks.on_chunk or function() end
  local on_complete = callbacks.on_complete or function() end
  local on_error = callbacks.on_error or function() end

  local filepath = type(params) == "table" and params.filepath or tostring(params)
  local width = (type(params) == "table" and params.width) or 800
  local height = (type(params) == "table" and params.height) or 600
  local yaw = (type(params) == "table" and params.yaw) or 0.0
  local pitch = (type(params) == "table" and params.pitch) or 0.0
  local zoom = (type(params) == "table" and params.zoom) or 450.0
  local braille = (type(params) == "table" and params.braille) and 1 or 0

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

    -- Build Version 2 REQ protocol payload
    local req_header = string.format(
      "REQ W=%d H=%d YAW=%.3f PITCH=%.3f ZOOM=%.1f BRAILLE=%d PATH=%s\n",
      width,
      height,
      yaw,
      pitch,
      zoom,
      braille,
      filepath
    )

    pipe:write(req_header, function(write_err)
      if write_err then
        safe_close()
        vim.schedule(function()
          on_error(write_err)
        end)
        return
      end

      -- Stream incoming Sixel/Braille chunks
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
          -- EOF: daemon finished streaming and closed connection
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
