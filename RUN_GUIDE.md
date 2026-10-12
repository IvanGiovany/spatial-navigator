# Beginner's Guide: Running Spatial Codebase Navigator

This guide will walk you through running the 3D Spatial Codebase Navigator step-by-step. No prior experience with background services or Linux sockets is required!

---

## What You Need Before Starting

- **Windows Terminal** (or any terminal supporting WSL).
- **WSL (Windows Subsystem for Linux)** installed.

---

## Step 1: Open Your WSL Terminal

1. Open the **Windows Terminal** app on your PC (search for "Terminal" in the Windows Start menu).
2. Open a **WSL / Ubuntu** tab (press `Ctrl + Shift + 2` or click the downward arrow `v` at the top bar and select **Ubuntu** or your default WSL distribution).

---

## Step 2: Start the 3D Engine (Daemon)

The visualizer uses a lightweight background program (called a "daemon") that reads C files, calculates 3D physics forces, and draws the 3D map.

1. In your WSL terminal, navigate to the project directory:
   ```bash
   cd /mnt/c/Users/Ivan/Documents/spatial-navigator
   ```

2. Start the daemon by running:
   ```bash
   ./daemon/build/spatial_daemon
   ```

3. You should see a message like:
   ```text
   [spatial_daemon] Server listening on /tmp/spatial_nav.sock (epoll loop started)
   ```
   **Leave this terminal window open!** The daemon is now listening and waiting for requests from Neovim.

---

## Step 3: Open Neovim in a Second Terminal Tab

Now we will open Neovim in a separate tab to view our code.

1. In Windows Terminal, open a **new tab** (press `Ctrl + Shift + T` or click the `+` button, making sure it opens in WSL).
2. Go to the project folder:
   ```bash
   cd /mnt/c/Users/Ivan/Documents/spatial-navigator
   ```

3. Open a sample C file in Neovim while loading the Spatial Navigator plugin:
   ```bash
   nvim --cmd "set rtp+=./nvim-plugin" daemon/tests/sample_project.c
   ```

---

## Step 4: Open the 3D Map in Neovim

Once Neovim is open showing the C code:

1. Make sure you are in **Normal Mode** (press `Esc` once or twice).
2. Type a colon (`:`) followed by `SpatialNav` and press **Enter**:
   ```vim
   :SpatialNav
   ```
3. The display will seamlessly switch to the terminal's full 3D canvas showing:
   - Your functions rendered as **volumetric 3D shaded spheres** with Blinn-Phong lighting highlights.
   - **Crisp in-scene function labels** (`main`, `compute`, `helper`) projected into 3D world space.
   - **Sub-pixel anti-aliased lines** connecting functions that call each other with true depth occlusion.
   - Professional **Catppuccin Mocha** visual hierarchy (Peach for `main`, Blue for call hubs, Cyan for callers, and Green for leaf functions).
   - An interactive HUD status bar at the bottom.

---

## Step 5: Interactive 3D Navigation Controls

Inside the 3D visualization canvas:

| Key | Action | Description |
|:---:|:---|:---|
| **`h`** / **`Left`** | Orbit Left | Rotates camera yaw counter-clockwise around the graph |
| **`l`** / **`Right`** | Orbit Right | Rotates camera yaw clockwise around the graph |
| **`j`** / **`Down`** | Orbit Down | Pitches camera downward |
| **`k`** / **`Up`** | Orbit Up | Pitches camera upward |
| **`+`** / **`=`** | Zoom In | Smoothly brings camera closer to the codebase centroid |
| **`-`** / **`_`** | Zoom Out | Expands camera field to view the broader architecture |
| **`r`** | Reset Camera | Restores default orbit orientation and zoom |
| **`q`** / **`Esc`** | Exit Canvas | Returns cleanly to your exact Neovim buffer without redrawing glitches |

---

## Quick Reference Summary

| Action | Command / Shortcut |
|---|---|
| **Start Daemon** | `cd /mnt/c/Users/Ivan/Documents/spatial-navigator && ./daemon/build/spatial_daemon` |
| **Open Neovim** | `nvim --cmd "set rtp+=./nvim-plugin" <path-to-file.c>` |
| **View 3D Graph** | `:SpatialNav` (inside Neovim, then press `Enter`) |
| **Orbit Camera** | `h` / `j` / `k` / `l` or Arrow keys |
| **Zoom In / Out** | `+` / `-` |
| **Reset Orientation** | `r` |
| **Close 3D View** | Press `q` or `Esc` |
| **Stop Daemon** | Click in the daemon terminal and press `Ctrl + C` |

