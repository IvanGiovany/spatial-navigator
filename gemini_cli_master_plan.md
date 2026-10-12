# Spatial Codebase Navigator: Master Implementation Plan

## Agent Instructions
You are Gemini, acting as a Senior C Systems Programmer and Neovim Plugin Architect. Your objective is to build a low-level, zero-dependency (aside from `tree-sitter` and `libsixel`) spatial codebase navigator. 
You will execute this plan phase by phase. DO NOT skip ahead. DO NOT introduce high-level abstractions like OpenGL, game engines, or external Python/Node.js runtimes. 
Adhere strictly to C11, POSIX standards, and Neovim's native Lua `vim.uv` APIs.

## Architectural Constraints
1.  **Backend Daemon (`daemon/`)**: Written in strict C11. Uses `CMake`. Must manually manage memory (no leaks). 
2.  **IPC (Inter-Process Communication)**: Uses POSIX Unix Domain Sockets (`/tmp/spatial_nav.sock`). 
3.  **Parsing**: Uses `tree-sitter` and `tree-sitter-c` C APIs via `mmap` for zero-copy file reading.
4.  **Physics**: Custom Fruchterman-Reingold 3D simulation. O(N^2) loops are acceptable for Phase 1.
5.  **Rendering**: Software rasterizer in C that maps 3D coordinates to a 2D pixel buffer, output via `libsixel` directly over the socket.
6.  **Frontend (`nvim-plugin/`)**: Neovim plugin written in Lua. Network I/O must be non-blocking via `vim.uv` (libuv). Rendering happens in a native Neovim floating terminal (`vim.api.nvim_open_term`).

---

## Phase 1: Core Daemon & IPC Infrastructure
**Goal:** Establish the build system and the Unix Domain Socket server.

*   **Step 1.1 - CMake Setup:** Create `daemon/CMakeLists.txt`. Require C11. Locate and link `pthread`, `tree-sitter`, and `sixel`. Define the executable `spatial_daemon` compiled from `src/main.c`, `src/parser.c`, `src/graph.c`, and `src/render.c`.
*   **Step 1.2 - Socket Server Initialization:** In `src/main.c`, create a Unix Domain Socket at `/tmp/spatial_nav.sock`. Use `unlink()` before binding to ensure the path is clear.
*   **Step 1.3 - Event Loop:** Implement a non-blocking `epoll` or `select` loop to listen for incoming connections.
*   **Step 1.4 - Request Handling:** When a client connects, read a string payload (which will be an absolute file path). For now, write back "ACK: [filepath]" to the socket and close the client connection.

## Phase 2: Tree-sitter AST Parsing & Graph Building
**Goal:** Parse C source code and extract function definitions and call graphs.

*   **Step 2.1 - Parser Initialization:** In `src/parser.c`, initialize a `TSParser` and set the language to `tree_sitter_c()`.
*   **Step 2.2 - File Reading:** Implement a function that takes the file path received from the socket, opens it, and maps it into memory using `mmap()`.
*   **Step 2.3 - AST Traversal:** Parse the `mmap`ed buffer to generate a `TSTree`. Write a recursive function using `TSTreeCursor` to traverse the AST.
*   **Step 2.4 - Node Extraction:** Look for `function_definition` nodes. Calculate their byte size (proxy for complexity/node volume). Inside definitions, look for `call_expression` nodes.
*   **Step 2.5 - Adjacency List:** Populate a C struct array (`Node` from `graph.h`) representing the graph. Each node gets a random initial 3D coordinate (x,y,z) between -50.0 and 50.0. Establish edges based on the `call_expression` relationships.

## Phase 3: 3D Physics Engine (Fruchterman-Reingold)
**Goal:** Apply physical forces to the graph nodes to organize them in 3D space.

*   **Step 3.1 - Graph Structs:** In `src/graph.h`, define `Vec3 { float x, y, z; }` and `Node { int id; Vec3 pos; Vec3 disp; float radius; int* edges; int edge_count; }`.
*   **Step 3.2 - Force Loop Initialization:** In `src/graph.c`, create `void calculate_forces(Node* nodes, int count)`. Define optimal distance `k = 50.0f` and initial `temperature = 100.0f`.
*   **Step 3.3 - Repulsion Math:** Implement an $O(N^2)$ loop over all node pairs. Calculate 3D Euclidean distance $d$. Apply repulsive force $f_r = k^2 / d$ along the normalized displacement vector to both nodes' `disp` accumulator.
*   **Step 3.4 - Attraction Math:** Loop over all edges. Calculate 3D distance $d$. Apply attractive force $f_a = d^2 / k$ pulling the connected nodes together, added to their `disp` accumulators.
*   **Step 3.5 - Displacement & Cooling:** Apply the accumulators to the `pos` vectors, capped by the current `temperature`. Multiply `temperature` by a cooling factor (e.g., `0.95`) and run this loop for 150 iterations.

## Phase 4: Software Rasterization & 3D Projection
**Goal:** Map 3D coordinates to a 2D pixel buffer.

*   **Step 4.1 - Camera Setup:** In `src/render.c`, define a camera structure. Focal length $F$, screen width $W=800$, screen height $H=600$. 
*   **Step 4.2 - 3D to 2D Projection:** Write a function `project_3d_to_2d(Vec3 pos)`. Calculate $x_{proj} = (pos.x * F) / (pos.z + CameraZ) + (W / 2)$ and $y_{proj} = (pos.y * F) / (pos.z + CameraZ) + (H / 2)$.
*   **Step 4.3 - Pixel Buffer:** Allocate a 1D array of bytes representing an RGB image buffer `unsigned char buffer[W * H * 3]`. Clear it to a dark background color (e.g., `0x1E1E1E`).
*   **Step 4.4 - Drawing Primitives:** Write a basic `draw_circle(buffer, x, y, radius, color)` to render the projected nodes. Write a `Bresenham_3D` or standard 2D line-drawing algorithm to draw lines between projected node coordinates to represent edges.

## Phase 5: Sixel Encoding & IPC Response
**Goal:** Convert the pixel buffer to a terminal-readable image and send it to Neovim.

*   **Step 5.1 - Libsixel Integration:** In `src/render.c`, initialize a `sixel_output_t` context. 
*   **Step 5.2 - Buffer Encoding:** Pass the RGB pixel buffer generated in Phase 4 to the Sixel encoder. 
*   **Step 5.3 - Memory Stream:** Instead of writing the Sixel output to standard out, write it to an in-memory string buffer or directly to the client's socket file descriptor.
*   **Step 5.4 - Pipeline Assembly:** Update `src/main.c`. The request flow is now: Read filepath -> `parse_file()` -> `calculate_forces(150)` -> `render_to_buffer()` -> `encode_and_send_sixel()`.

## Phase 6: Neovim Lua Frontend & Async IPC
**Goal:** Create the editor UI that requests and displays the 3D map.

*   **Step 6.1 - Plugin Setup:** In `nvim-plugin/lua/spatial_nav/init.lua`, create the entry point and a user command `:SpatialNav`.
*   **Step 6.2 - Async Sockets (`vim.uv`):** Use `vim.uv.new_pipe(false)` to create a named pipe client. Connect it to `/tmp/spatial_nav.sock`. 
*   **Step 6.3 - Requesting the Graph:** On connection, send `vim.api.nvim_buf_get_name(0)` (the absolute path of the current buffer) to the pipe.
*   **Step 6.4 - Floating Terminal UI:** In `nvim-plugin/lua/spatial_nav/ui.lua`, calculate the center of the editor and open a floating window (`vim.api.nvim_open_win`). Inside this window, open a terminal buffer using `vim.api.nvim_open_term`.
*   **Step 6.5 - Streaming Sixel Data:** As data chunks arrive from the `vim.uv` pipe, append them directly to the terminal channel created by `nvim_open_term`. Because Neovim supports Sixel natively, the 3D graph will instantly render in the floating window.

---
**Execution trigger for AI:** "Begin Phase 1. Provide the code for `CMakeLists.txt` and `main.c`."