#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "render.h"
#include "font.h"
#include "math3d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sixel.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* --- Catppuccin Mocha Palette Specification --- */
static const ColorRGB CATPPUCCIN_MANTLE   = { 24,  24,  37  }; /* #181825 Canvas background */
static const ColorRGB CATPPUCCIN_CRUST    = { 17,  17,  27  }; /* #11111b Label pill background */
static const ColorRGB CATPPUCCIN_SURFACE2 = { 88,  91,  112 }; /* #585b70 Calling edges */
static const ColorRGB CATPPUCCIN_TEXT     = { 205, 214, 244 }; /* #cdd6f4 Label typography */
static const ColorRGB CATPPUCCIN_PEACH    = { 250, 179, 135 }; /* #fab387 'main' root node */
static const ColorRGB CATPPUCCIN_BLUE     = { 137, 180, 250 }; /* #89b4fa Hub nodes (out-deg >= 4) */
static const ColorRGB CATPPUCCIN_CYAN     = { 137, 220, 235 }; /* #89dceb Intermediate callers */
static const ColorRGB CATPPUCCIN_GREEN    = { 166, 227, 161 }; /* #a6e3a1 Leaf workers */

/* --- Raster Context Lifecycle --- */

int raster_init(RasterContext *ctx, int width, int height) {
    if (!ctx || width <= 0 || height <= 0) return -1;
    ctx->width = width;
    ctx->height = height;
    ctx->pixel_count = (size_t)width * (size_t)height;
    ctx->color_buffer = malloc(ctx->pixel_count * 3);
    ctx->depth_buffer = malloc(ctx->pixel_count * sizeof(float));
    if (!ctx->color_buffer || !ctx->depth_buffer) {
        free(ctx->color_buffer);
        free(ctx->depth_buffer);
        ctx->color_buffer = NULL;
        ctx->depth_buffer = NULL;
        return -1;
    }
    raster_clear(ctx, CATPPUCCIN_MANTLE);
    return 0;
}

int raster_resize(RasterContext *ctx, int width, int height) {
    if (!ctx || width <= 0 || height <= 0) return -1;
    if (ctx->width == width && ctx->height == height && ctx->color_buffer && ctx->depth_buffer) {
        return 0;
    }
    size_t new_count = (size_t)width * (size_t)height;
    uint8_t *new_color = realloc(ctx->color_buffer, new_count * 3);
    if (!new_color) return -1;
    ctx->color_buffer = new_color;

    float *new_depth = realloc(ctx->depth_buffer, new_count * sizeof(float));
    if (!new_depth) return -1;
    ctx->depth_buffer = new_depth;

    ctx->width = width;
    ctx->height = height;
    ctx->pixel_count = new_count;
    return 0;
}

void raster_clear(RasterContext *ctx, ColorRGB bg_color) {
    if (!ctx || !ctx->color_buffer || !ctx->depth_buffer) return;

    /* Fill color buffer */
    uint8_t *c = ctx->color_buffer;
    for (size_t i = 0; i < ctx->pixel_count; ++i) {
        *c++ = bg_color.r;
        *c++ = bg_color.g;
        *c++ = bg_color.b;
    }

    /* Fill depth buffer with 1.0f (far plane) */
    for (size_t i = 0; i < ctx->pixel_count; ++i) {
        ctx->depth_buffer[i] = 1.0f;
    }
}

void raster_free(RasterContext *ctx) {
    if (!ctx) return;
    free(ctx->color_buffer);
    free(ctx->depth_buffer);
    ctx->color_buffer = NULL;
    ctx->depth_buffer = NULL;
    ctx->width = 0;
    ctx->height = 0;
    ctx->pixel_count = 0;
}

/* --- Camera Setup --- */

void camera_init(Camera *cam, int width, int height) {
    if (!cam) return;
    cam->target = vec3_create(0.0f, 0.0f, 0.0f);
    cam->distance = 450.0f;
    cam->orientation = quat_identity();
    cam->fov_y_rad = (float)(M_PI / 3.0); /* 60 degrees */
    cam->near_z = 10.0f;
    cam->far_z = 3000.0f;
    cam->width = width > 0 ? width : DEFAULT_VIEW_WIDTH;
    cam->height = height > 0 ? height : DEFAULT_VIEW_HEIGHT;
}

void camera_set_orbit(Camera *cam, float yaw_rad, float pitch_rad, float distance) {
    if (!cam) return;
    cam->distance = distance > 20.0f ? distance : 20.0f;
    cam->orientation = quat_from_euler(pitch_rad, yaw_rad, 0.0f);
}

Mat4 camera_get_view_matrix(const Camera *cam) {
    /* Compute eye position by rotating (0, 0, distance) by orientation quaternion */
    Mat4 rot = mat4_from_quat(cam->orientation);
    Vec3 forward = { rot.m[0][2], rot.m[1][2], rot.m[2][2] };
    Vec3 eye = vec3_add(cam->target, vec3_scale(forward, cam->distance));
    Vec3 up = { rot.m[0][1], rot.m[1][1], rot.m[2][1] };
    return mat4_look_at(eye, cam->target, up);
}

Mat4 camera_get_proj_matrix(const Camera *cam) {
    float aspect = (float)cam->width / (float)(cam->height > 0 ? cam->height : 1);
    return mat4_perspective(cam->fov_y_rad, aspect, cam->near_z, cam->far_z);
}

/* --- Low-Level Pixel Operations --- */

static inline void put_pixel_z(RasterContext *ctx, int x, int y, float depth, ColorRGB color) {
    if (x < 0 || x >= ctx->width || y < 0 || y >= ctx->height) return;
    int idx = y * ctx->width + x;
    if (depth <= ctx->depth_buffer[idx]) {
        ctx->depth_buffer[idx] = depth;
        int c_idx = idx * 3;
        ctx->color_buffer[c_idx + 0] = color.r;
        ctx->color_buffer[c_idx + 1] = color.g;
        ctx->color_buffer[c_idx + 2] = color.b;
    }
}

static inline void blend_pixel_z(RasterContext *ctx, int x, int y, float depth, ColorRGB color, float alpha) {
    if (x < 0 || x >= ctx->width || y < 0 || y >= ctx->height) return;
    int idx = y * ctx->width + x;

    /* Depth bias prevents z-fighting against co-planar/adjacent surfaces */
    const float DEPTH_BIAS = 0.001f;
    if (depth > ctx->depth_buffer[idx] + DEPTH_BIAS) {
        return; /* Occluded */
    }

    int c_idx = idx * 3;
    float inv_a = 1.0f - alpha;
    ctx->color_buffer[c_idx + 0] = (uint8_t)(color.r * alpha + ctx->color_buffer[c_idx + 0] * inv_a);
    ctx->color_buffer[c_idx + 1] = (uint8_t)(color.g * alpha + ctx->color_buffer[c_idx + 1] * inv_a);
    ctx->color_buffer[c_idx + 2] = (uint8_t)(color.b * alpha + ctx->color_buffer[c_idx + 2] * inv_a);

    if (alpha > 0.65f && depth < ctx->depth_buffer[idx]) {
        ctx->depth_buffer[idx] = depth;
    }
}

/* --- Sub-Pixel Anti-Aliased Lines (Xiaolin Wu with Z-Buffer) --- */

static void draw_line_wu_z(RasterContext *ctx,
                           float x0, float y0, float z0,
                           float x1, float y1, float z1,
                           ColorRGB color) {
    bool steep = fabsf(y1 - y0) > fabsf(x1 - x0);
    if (steep) {
        float tmp;
        tmp = x0; x0 = y0; y0 = tmp;
        tmp = x1; x1 = y1; y1 = tmp;
    }
    if (x0 > x1) {
        float tmp;
        tmp = x0; x0 = x1; x1 = tmp;
        tmp = y0; y0 = y1; y1 = tmp;
        tmp = z0; z0 = z1; z1 = tmp;
    }

    float dx = x1 - x0;
    float dy = y1 - y0;
    float dz = z1 - z0;
    float gradient = (dx == 0.0f) ? 1.0f : (dy / dx);
    float z_gradient = (dx == 0.0f) ? 0.0f : (dz / dx);

    /* Endpoint 1 */
    float xend = roundf(x0);
    float yend = y0 + gradient * (xend - x0);
    float zend = z0 + z_gradient * (xend - x0);
    float xgap = 1.0f - (x0 + 0.5f - floorf(x0 + 0.5f));
    int xpxl1 = (int)xend;
    int ypxl1 = (int)floorf(yend);

    if (steep) {
        blend_pixel_z(ctx, ypxl1,     xpxl1, zend, color, (1.0f - (yend - floorf(yend))) * xgap);
        blend_pixel_z(ctx, ypxl1 + 1, xpxl1, zend, color, (yend - floorf(yend)) * xgap);
    } else {
        blend_pixel_z(ctx, xpxl1, ypxl1,     zend, color, (1.0f - (yend - floorf(yend))) * xgap);
        blend_pixel_z(ctx, xpxl1, ypxl1 + 1, zend, color, (yend - floorf(yend)) * xgap);
    }

    float intery = yend + gradient;
    float interz = zend + z_gradient;

    /* Endpoint 2 */
    xend = roundf(x1);
    yend = y1 + gradient * (xend - x1);
    zend = z1 + z_gradient * (xend - x1);
    xgap = (x1 + 0.5f - floorf(x1 + 0.5f));
    int xpxl2 = (int)xend;
    int ypxl2 = (int)floorf(yend);

    if (steep) {
        blend_pixel_z(ctx, ypxl2,     xpxl2, zend, color, (1.0f - (yend - floorf(yend))) * xgap);
        blend_pixel_z(ctx, ypxl2 + 1, xpxl2, zend, color, (yend - floorf(yend)) * xgap);
    } else {
        blend_pixel_z(ctx, xpxl2, ypxl2,     zend, color, (1.0f - (yend - floorf(yend))) * xgap);
        blend_pixel_z(ctx, xpxl2, ypxl2 + 1, zend, color, (yend - floorf(yend)) * xgap);
    }

    /* Main scanline loop */
    if (steep) {
        for (int x = xpxl1 + 1; x < xpxl2; ++x) {
            int y_floor = (int)floorf(intery);
            float f = intery - (float)y_floor;
            blend_pixel_z(ctx, y_floor,     x, interz, color, 1.0f - f);
            blend_pixel_z(ctx, y_floor + 1, x, interz, color, f);
            intery += gradient;
            interz += z_gradient;
        }
    } else {
        for (int x = xpxl1 + 1; x < xpxl2; ++x) {
            int y_floor = (int)floorf(intery);
            float f = intery - (float)y_floor;
            blend_pixel_z(ctx, x, y_floor,     interz, color, 1.0f - f);
            blend_pixel_z(ctx, x, y_floor + 1, interz, color, f);
            intery += gradient;
            interz += z_gradient;
        }
    }
}

/* --- Volumetric Sphere Shading (Blinn-Phong Illumination Model) --- */

static void draw_sphere_blinn_phong(RasterContext *ctx,
                                    float cx, float cy, float cz,
                                    float radius, ColorRGB base_color) {
    int min_x = (int)floorf(cx - radius - 1.0f);
    int max_x = (int)ceilf(cx + radius + 1.0f);
    int min_y = (int)floorf(cy - radius - 1.0f);
    int max_y = (int)ceilf(cy + radius + 1.0f);

    if (min_x < 0) min_x = 0;
    if (max_x >= ctx->width) max_x = ctx->width - 1;
    if (min_y < 0) min_y = 0;
    if (max_y >= ctx->height) max_y = ctx->height - 1;

    float r_sq = radius * radius;
    float inv_r = (radius > 0.0f) ? (1.0f / radius) : 1.0f;

    /* Light vector: normalized key light (-0.45, -0.65, 0.60) in screen-space */
    Vec3 L = vec3_normalize(vec3_create(-0.45f, -0.65f, 0.60f));
    Vec3 V = vec3_create(0.0f, 0.0f, 1.0f); /* Eye vector */
    Vec3 H = vec3_normalize(vec3_add(L, V)); /* Halfway vector */

    const float k_ambient = 0.22f;
    const float k_diffuse = 0.62f;
    const float k_specular = 0.45f;
    const float shininess = 28.0f;

    for (int y = min_y; y <= max_y; ++y) {
        float dy = (float)y - cy;
        for (int x = min_x; x <= max_x; ++x) {
            float dx = (float)x - cx;
            float dist_sq = dx * dx + dy * dy;

            if (dist_sq <= (radius + 0.6f) * (radius + 0.6f)) {
                float dist = sqrtf(dist_sq);
                float alpha = 1.0f;

                /* Sub-pixel anti-aliased edge smoothing */
                if (dist > radius - 0.5f) {
                    alpha = (radius + 0.5f - dist);
                    if (alpha < 0.0f) alpha = 0.0f;
                    if (alpha > 1.0f) alpha = 1.0f;
                }

                /* Spherical surface normal computation */
                float dz = 0.0f;
                if (dist <= radius) {
                    dz = sqrtf(r_sq - dist_sq);
                }
                Vec3 N = { dx * inv_r, dy * inv_r, dz * inv_r };

                /* Calculate depth in camera space: cap front surface closer to eye */
                float z_pixel = cz - (dz * inv_r) * 0.005f;

                /* Early Z-check */
                int idx = y * ctx->width + x;
                if (z_pixel > ctx->depth_buffer[idx] + 0.001f) {
                    continue;
                }

                /* Blinn-Phong equations */
                float NdotL = fmaxf(0.0f, vec3_dot(N, L));
                float NdotH = fmaxf(0.0f, vec3_dot(N, H));
                float spec = powf(NdotH, shininess) * k_specular;
                float fresnel = powf(1.0f - fmaxf(0.0f, N.z), 2.5f) * 0.25f;

                float intensity = k_ambient + k_diffuse * NdotL + spec + fresnel;

                ColorRGB lit_color;
                lit_color.r = (uint8_t)fminf(255.0f, (float)base_color.r * intensity);
                lit_color.g = (uint8_t)fminf(255.0f, (float)base_color.g * intensity);
                lit_color.b = (uint8_t)fminf(255.0f, (float)base_color.b * intensity);

                blend_pixel_z(ctx, x, y, z_pixel, lit_color, alpha);
            }
        }
    }
}

/* --- 3D In-Scene Label Rendering --- */

static void draw_label_pill(RasterContext *ctx, int x, int y, int w, int h, float depth) {
    /* Draw rounded pill background rectangle */
    for (int py = y; py < y + h; ++py) {
        for (int px = x; px < x + w; ++px) {
            blend_pixel_z(ctx, px, py, depth, CATPPUCCIN_CRUST, 0.78f);
        }
    }
}

static void draw_in_scene_label(RasterContext *ctx,
                                int start_x, int start_y, float depth,
                                const char *text, ColorRGB text_color) {
    if (!text || text[0] == '\0') return;
    size_t len = strlen(text);
    if (len > 24) len = 24; /* Truncate long identifiers gracefully */

    int pill_w = (int)len * FONT_GLYPH_WIDTH + 8;
    int pill_h = FONT_GLYPH_HEIGHT + 4;

    /* Draw contrast pill behind text */
    draw_label_pill(ctx, start_x - 4, start_y - 2, pill_w, pill_h, depth);

    /* Render bitmap glyphs */
    int cur_x = start_x;
    for (size_t i = 0; i < len; ++i) {
        const uint8_t *glyph = font_get_glyph(text[i]);
        for (int row = 0; row < FONT_GLYPH_HEIGHT; ++row) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < FONT_GLYPH_WIDTH; ++col) {
                if (bits & (0x80 >> col)) {
                    put_pixel_z(ctx, cur_x + col, start_y + row, depth - 0.0005f, text_color);
                }
            }
        }
        cur_x += FONT_GLYPH_WIDTH;
    }
}

/* --- Semantic Node Color Resolution --- */

static ColorRGB resolve_node_color(const Node *node) {
    if (strcmp(node->name, "main") == 0) {
        return CATPPUCCIN_PEACH;
    } else if (node->edge_count >= 4) {
        return CATPPUCCIN_BLUE;
    } else if (node->edge_count > 0) {
        return CATPPUCCIN_CYAN;
    } else {
        return CATPPUCCIN_GREEN;
    }
}

/* --- Main High-Fidelity Rendering Pipeline (Version 2) --- */

int render_graph_v2(const Graph *graph, const Camera *cam, RasterContext *ctx) {
    if (!ctx || !cam || ctx->width <= 0 || ctx->height <= 0) {
        return -1;
    }

    /* Step 1: Clear color and depth buffers */
    raster_clear(ctx, CATPPUCCIN_MANTLE);

    if (!graph || graph->node_count <= 0) {
        return 0;
    }

    /* Step 2: Compute View-Projection Transformation Matrix */
    Mat4 view = camera_get_view_matrix(cam);
    Mat4 proj = camera_get_proj_matrix(cam);
    Mat4 vp = mat4_multiply(&proj, &view);

    ProjectedNode *proj_nodes = malloc((size_t)graph->node_count * sizeof(ProjectedNode));
    if (!proj_nodes) {
        return -1;
    }

    /* Step 3: Transform all graph nodes to Screen Coordinates with Perspective Division */
    for (int i = 0; i < graph->node_count; ++i) {
        const Node *n = &graph->nodes[i];
        proj_nodes[i].node_id = i;

        float cx, cy, cz, cw;
        mat4_transform_point(&vp, n->pos, &cx, &cy, &cz, &cw);

        if (cw > 0.001f && cz >= -cw && cz <= cw) {
            /* Normalized Device Coordinates (NDC) in [-1, 1] */
            float ndc_x = cx / cw;
            float ndc_y = cy / cw;
            float ndc_z = cz / cw;

            proj_nodes[i].screen_x = ((ndc_x + 1.0f) * 0.5f) * (float)ctx->width;
            proj_nodes[i].screen_y = ((1.0f - ndc_y) * 0.5f) * (float)ctx->height;
            proj_nodes[i].depth = (ndc_z + 1.0f) * 0.5f; /* Depth in [0.0, 1.0] */

            /* Radius perspective projection */
            float screen_r = (n->radius * (float)ctx->height * 0.85f) / cw;
            if (screen_r < 3.5f) screen_r = 3.5f;
            if (screen_r > 50.0f) screen_r = 50.0f;
            proj_nodes[i].screen_r = screen_r;
            proj_nodes[i].is_visible = true;
        } else {
            proj_nodes[i].is_visible = false;
        }
    }

    /* Step 4: Rasterize Call Graph Edges (Xiaolin Wu Anti-Aliased Lines with Z-Buffer) */
    for (int u = 0; u < graph->node_count; ++u) {
        if (!proj_nodes[u].is_visible) continue;
        const Node *from_node = &graph->nodes[u];

        for (int j = 0; j < from_node->edge_count; ++j) {
            int v = from_node->edges[j];
            if (v >= 0 && v < graph->node_count && u != v && proj_nodes[v].is_visible) {
                draw_line_wu_z(ctx,
                               proj_nodes[u].screen_x, proj_nodes[u].screen_y, proj_nodes[u].depth,
                               proj_nodes[v].screen_x, proj_nodes[v].screen_y, proj_nodes[v].depth,
                               CATPPUCCIN_SURFACE2);
            }
        }
    }

    /* Step 5: Rasterize 3D Spheres with Volumetric Blinn-Phong Shading */
    for (int i = 0; i < graph->node_count; ++i) {
        if (!proj_nodes[i].is_visible) continue;
        const Node *n = &graph->nodes[i];
        ColorRGB base_color = resolve_node_color(n);

        draw_sphere_blinn_phong(ctx,
                                proj_nodes[i].screen_x,
                                proj_nodes[i].screen_y,
                                proj_nodes[i].depth,
                                proj_nodes[i].screen_r,
                                base_color);
    }

    /* Step 6: Render Crisp In-Scene Function Labels */
    for (int i = 0; i < graph->node_count; ++i) {
        if (!proj_nodes[i].is_visible) continue;
        const Node *n = &graph->nodes[i];

        int label_x = (int)roundf(proj_nodes[i].screen_x + proj_nodes[i].screen_r + 6.0f);
        int label_y = (int)roundf(proj_nodes[i].screen_y - 6.0f);

        /* Avoid clipping labels beyond screen borders */
        if (label_x < ctx->width - 16 && label_y > 4 && label_y < ctx->height - 16) {
            draw_in_scene_label(ctx, label_x, label_y, proj_nodes[i].depth, n->name, CATPPUCCIN_TEXT);
        }
    }

    free(proj_nodes);
    return 0;
}

/* --- Sixel Stream Output --- */

static int sixel_stream_write(char *data, int size, void *priv) {
    int client_fd = *(int *)priv;
    int total_written = 0;

    while (total_written < size) {
        ssize_t written = write(client_fd, data + total_written, (size_t)(size - total_written));
        if (written < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = { .fd = client_fd, .events = POLLOUT, .revents = 0 };
                int ret = poll(&pfd, 1, 1000);
                if (ret <= 0) return -1;
                continue;
            }
            return -1;
        }
        total_written += (int)written;
    }
    return 0;
}

int render_sixel_stream(const RasterContext *ctx, int client_fd) {
    if (!ctx || !ctx->color_buffer || client_fd < 0) return -1;

    sixel_output_t *output = NULL;
    sixel_dither_t *dither = NULL;

    SIXELSTATUS status = sixel_output_new(&output, sixel_stream_write, &client_fd, NULL);
    if (status != SIXEL_OK) return -1;

    status = sixel_dither_new(&dither, 256, NULL);
    if (status != SIXEL_OK) {
        sixel_output_unref(output);
        return -1;
    }

    status = sixel_dither_initialize(dither,
                                    ctx->color_buffer,
                                    ctx->width,
                                    ctx->height,
                                    SIXEL_PIXELFORMAT_RGB888,
                                    SIXEL_LARGE_AUTO,
                                    SIXEL_REP_AUTO,
                                    SIXEL_DIFFUSE_FS);
    if (status != SIXEL_OK) {
        sixel_dither_unref(dither);
        sixel_output_unref(output);
        return -1;
    }

    status = sixel_encode(ctx->color_buffer,
                          ctx->width,
                          ctx->height,
                          SIXEL_PIXELFORMAT_RGB888,
                          dither,
                          output);

    sixel_dither_unref(dither);
    sixel_output_unref(output);

    return (status == SIXEL_OK) ? 0 : -1;
}

/* --- Unicode Braille Fallback Rendering --- */

int render_braille_stream(const RasterContext *ctx, int client_fd) {
    if (!ctx || !ctx->color_buffer || client_fd < 0) return -1;

    int cols = ctx->width / 2;
    int rows = ctx->height / 4;
    if (cols <= 0 || rows <= 0) return -1;

    /* Braille dot mapping:
     * (0,0)->1(0x01)  (1,0)->4(0x08)
     * (0,1)->2(0x02)  (1,1)->5(0x10)
     * (0,2)->3(0x04)  (1,2)->6(0x20)
     * (0,3)->7(0x40)  (1,3)->8(0x80)
     */
    static const uint8_t dot_map[4][2] = {
        { 0x01, 0x08 },
        { 0x02, 0x10 },
        { 0x04, 0x20 },
        { 0x40, 0x80 }
    };

    char line_buf[4096];

    for (int r = 0; r < rows; ++r) {
        int pos = 0;
        for (int c = 0; c < cols; ++c) {
            uint16_t braille_code = 0x2800;
            int px_base = c * 2;
            int py_base = r * 4;

            for (int dy = 0; dy < 4; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    int px = px_base + dx;
                    int py = py_base + dy;
                    if (px < ctx->width && py < ctx->height) {
                        int idx = (py * ctx->width + px) * 3;
                        uint8_t cr = ctx->color_buffer[idx];
                        uint8_t cg = ctx->color_buffer[idx + 1];
                        uint8_t cb = ctx->color_buffer[idx + 2];
                        /* If luminance is significantly above background mantle (24, 24, 37) */
                        if (cr > 40 || cg > 40 || cb > 50) {
                            braille_code |= dot_map[dy][dx];
                        }
                    }
                }
            }

            /* Convert UTF-16 code point (0x2800..0x28FF) to UTF-8 (3 bytes) */
            line_buf[pos++] = (char)(0xE0 | ((braille_code >> 12) & 0x0F));
            line_buf[pos++] = (char)(0x80 | ((braille_code >> 6) & 0x3F));
            line_buf[pos++] = (char)(0x80 | (braille_code & 0x3F));

            if (pos >= (int)sizeof(line_buf) - 8) {
                write(client_fd, line_buf, (size_t)pos);
                pos = 0;
            }
        }
        line_buf[pos++] = '\r';
        line_buf[pos++] = '\n';
        write(client_fd, line_buf, (size_t)pos);
    }

    return 0;
}
