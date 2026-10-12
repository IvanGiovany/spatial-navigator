local M = {}

--- Query device attributes (DA1) and XTWINOPS to detect Sixel support and pixel geometry
--- @return table { has_sixel: boolean, width: number, height: number, cell_w: number, cell_h: number }
function M.probe_terminal()
  local cols = vim.o.columns
  local rows = vim.o.lines

  -- Calibrated default cell dimensions in modern terminal emulators (e.g., WezTerm, Alacritty)
  local cell_w = 9
  local cell_h = 18

  -- Invariant: Sixel height must leave 3 text rows for status bar and cursor to eliminate scroll overrun
  local max_pixel_h = math.max(200, (rows - 3) * cell_h)
  local max_pixel_w = math.max(300, cols * cell_w)

  local has_sixel = false

  -- Step 1: Check known Sixel-capable terminal indicators
  local term = vim.env.TERM or ""
  local term_program = vim.env.TERM_PROGRAM or ""
  local wezterm_pane = vim.env.WEZTERM_PANE or ""

  if wezterm_pane ~= "" or term_program == "WezTerm" or term_program == "ghostty" or term_program == "foot" then
    has_sixel = true
  elseif term:find("sixel") or term:find("mlterm") or term:find("xterm") then
    has_sixel = true
  end

  return {
    has_sixel = has_sixel,
    width = max_pixel_w,
    height = max_pixel_h,
    cell_w = cell_w,
    cell_h = cell_h,
  }
end

return M
