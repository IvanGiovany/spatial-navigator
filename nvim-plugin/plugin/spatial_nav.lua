-- Spatial Codebase Navigator Neovim Plugin
-- Entry point for :SpatialNav command

if vim.g.loaded_spatial_nav == 1 then
  return
end
vim.g.loaded_spatial_nav = 1

vim.api.nvim_create_user_command("SpatialNav", function(opts)
  local path_arg = (opts.args and opts.args ~= "") and opts.args or nil
  require("spatial_nav").open(path_arg)
end, {
  desc = "Open 3D Spatial Codebase Navigator for current buffer or target path",
  nargs = "?",
  complete = "file",
})
