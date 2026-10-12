#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "render.h"
#include "graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sixel.h>

typedef struct {
    float x;
    float y;
    float r;
    float z;
    int node_id;
} ProjectedNode;

int project_3d_to_2d(Vec3 pos, float radius, const Camera *cam, float *out_x, float *out_y, float *out_r) {
    if (!cam || !out_x || !out_y || !out_r) {
        return -1;
    }

    float z_eff = pos.z + cam->camera_z;
    if (z_eff < 20.0f) {
        z_eff = 20.0f; /* Near plane clipping */
    }

    *out_x = (pos.x * cam->focal_length) / z_eff + (float)cam->width / 2.0f;
    *out_y = (pos.y * cam->focal_length) / z_eff + (float)cam->height / 2.0f;

    float pr = (radius * cam->focal_length) / z_eff;
    if (pr < 3.0f) pr = 3.0f;
    if (pr > 45.0f) pr = 45.0f;
    *out_r = pr;

    return 0;
}

static inline void set_pixel(unsigned char *buf, int width, int height,
                             int x, int y,
                             unsigned char r, unsigned char g, unsigned char b) {
    if (x < 0 || x >= width || y < 0 || y >= height) {
        return;
    }
    int idx = (y * width + x) * 3;
    buf[idx]     = r;
    buf[idx + 1] = g;
    buf[idx + 2] = b;
}

static void draw_line_bresenham(unsigned char *buf, int width, int height,
                                int x0, int y0, int x1, int y1,
                                unsigned char r, unsigned char g, unsigned char b) {
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        set_pixel(buf, width, height, x0, y0, r, g, b);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void get_node_color(const Node *node, unsigned char *out_r, unsigned char *out_g, unsigned char *out_b) {
    if (strcmp(node->name, "main") == 0) {
        /* Amber Gold for main entrypoint */
        *out_r = 255;
        *out_g = 190;
        *out_b = 60;
    } else if (node->edge_count > 0) {
        /* Cyber Cyan for intermediate calling functions */
        *out_r = 70;
        *out_g = 200;
        *out_b = 255;
    } else {
        /* Emerald Mint for leaf functions */
        *out_r = 80;
        *out_g = 230;
        *out_b = 150;
    }
}

static void draw_shaded_disc(unsigned char *buf, int width, int height,
                             float cx, float cy, float radius,
                             unsigned char base_r, unsigned char base_g, unsigned char base_b) {
    int min_x = (int)floorf(cx - radius);
    int max_x = (int)ceilf(cx + radius);
    int min_y = (int)floorf(cy - radius);
    int max_y = (int)ceilf(cy + radius);

    if (min_x < 0) min_x = 0;
    if (max_x >= width) max_x = width - 1;
    if (min_y < 0) min_y = 0;
    if (max_y >= height) max_y = height - 1;

    float r_sq = radius * radius;
    float inv_r = (radius > 0.0f) ? (1.0f / radius) : 1.0f;

    for (int y = min_y; y <= max_y; ++y) {
        float dy = (float)y - cy;
        for (int x = min_x; x <= max_x; ++x) {
            float dx = (float)x - cx;
            float d_sq = dx * dx + dy * dy;
            if (d_sq <= r_sq) {
                /* Directional light offset (-0.35R, -0.35R) gives 3D spherical volume */
                float lx = dx + 0.35f * radius;
                float ly = dy + 0.35f * radius;
                float l_dist_sq = (lx * lx + ly * ly) * inv_r * inv_r;
                float intensity = 0.45f + 0.55f * sqrtf(fmaxf(0.0f, 1.0f - l_dist_sq * 0.8f));

                if (intensity > 1.3f) intensity = 1.3f;
                if (intensity < 0.25f) intensity = 0.25f;

                int pr = (int)((float)base_r * intensity);
                int pg = (int)((float)base_g * intensity);
                int pb = (int)((float)base_b * intensity);

                set_pixel(buf, width, height, x, y,
                          (unsigned char)(pr > 255 ? 255 : pr),
                          (unsigned char)(pg > 255 ? 255 : pg),
                          (unsigned char)(pb > 255 ? 255 : pb));
            }
        }
    }
}

static int compare_projected_depth(const void *a, const void *b) {
    const ProjectedNode *pa = (const ProjectedNode *)a;
    const ProjectedNode *pb = (const ProjectedNode *)b;
    /* Descending Z: largest Z (farthest) rendered first */
    if (pa->z > pb->z) return -1;
    if (pa->z < pb->z) return 1;
    return 0;
}

int render_graph_to_buffer(const Graph *graph, unsigned char *buffer, int width, int height) {
    if (!buffer || width <= 0 || height <= 0) {
        return -1;
    }

    /* Dark editor background: #181825 (Catppuccin Mantle) */
    const unsigned char bg_r = 24;
    const unsigned char bg_g = 24;
    const unsigned char bg_b = 37;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = (y * width + x) * 3;
            buffer[idx]     = bg_r;
            buffer[idx + 1] = bg_g;
            buffer[idx + 2] = bg_b;
        }
    }

    if (!graph || graph->node_count <= 0) {
        return 0;
    }

    Camera cam = {
        .focal_length = CAMERA_FOCAL_LENGTH,
        .camera_z = CAMERA_Z,
        .width = width,
        .height = height
    };

    ProjectedNode *proj = malloc((size_t)graph->node_count * sizeof(ProjectedNode));
    if (!proj) {
        return -1;
    }

    for (int i = 0; i < graph->node_count; ++i) {
        const Node *n = &graph->nodes[i];
        proj[i].node_id = i;
        proj[i].z = n->pos.z + cam.camera_z;
        project_3d_to_2d(n->pos, n->radius, &cam, &proj[i].x, &proj[i].y, &proj[i].r);
    }

    /* Step 1: Draw all connecting edges behind nodes */
    const unsigned char edge_r = 90;
    const unsigned char edge_g = 110;
    const unsigned char edge_b = 140;

    for (int u = 0; u < graph->node_count; ++u) {
        const Node *from_node = &graph->nodes[u];
        int x0 = (int)roundf(proj[u].x);
        int y0 = (int)roundf(proj[u].y);

        for (int j = 0; j < from_node->edge_count; ++j) {
            int v = from_node->edges[j];
            if (v >= 0 && v < graph->node_count && u != v) {
                int x1 = (int)roundf(proj[v].x);
                int y1 = (int)roundf(proj[v].y);
                draw_line_bresenham(buffer, width, height, x0, y0, x1, y1, edge_r, edge_g, edge_b);
            }
        }
    }

    /* Step 2: Sort nodes by depth (farthest first) for painter's algorithm */
    qsort(proj, (size_t)graph->node_count, sizeof(ProjectedNode), compare_projected_depth);

    /* Step 3: Draw depth-sorted shaded discs */
    for (int i = 0; i < graph->node_count; ++i) {
        int nid = proj[i].node_id;
        const Node *n = &graph->nodes[nid];
        unsigned char nr = 70, ng = 200, nb = 255;
        get_node_color(n, &nr, &ng, &nb);
        draw_shaded_disc(buffer, width, height, proj[i].x, proj[i].y, proj[i].r, nr, ng, nb);
    }

    free(proj);
    return 0;
}

static int sixel_socket_write(char *data, int size, void *priv) {
    int client_fd = *(int *)priv;
    int total_written = 0;

    while (total_written < size) {
        ssize_t written = write(client_fd, data + total_written, (size_t)(size - total_written));
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Non-blocking socket buffer full: wait for writable event */
                struct pollfd pfd;
                pfd.fd = client_fd;
                pfd.events = POLLOUT;
                pfd.revents = 0;
                int ret = poll(&pfd, 1, 1000);
                if (ret <= 0) {
                    perror("[spatial_render] poll timeout or error");
                    return -1;
                }
                continue;
            }
            perror("[spatial_render] write to socket");
            return -1;
        }
        total_written += (int)written;
    }
    return size;
}

int render_graph_to_sixel(const Graph *graph, int client_fd) {
    if (!graph || client_fd < 0) {
        return -1;
    }

    size_t buf_size = (size_t)(SCREEN_WIDTH * SCREEN_HEIGHT * 3);
    unsigned char *frame_buf = malloc(buf_size);
    if (!frame_buf) {
        fprintf(stderr, "[spatial_render] Failed to allocate frame buffer\n");
        return -1;
    }

    if (render_graph_to_buffer(graph, frame_buf, SCREEN_WIDTH, SCREEN_HEIGHT) < 0) {
        fprintf(stderr, "[spatial_render] Failed to render graph into buffer\n");
        free(frame_buf);
        return -1;
    }

    sixel_output_t *output = NULL;
    SIXELSTATUS st = sixel_output_new(&output, sixel_socket_write, &client_fd, NULL);
    if (SIXEL_FAILED(st)) {
        fprintf(stderr, "[spatial_render] sixel_output_new failed: %d\n", st);
        free(frame_buf);
        return -1;
    }

    sixel_dither_t *dither = NULL;
    st = sixel_dither_new(&dither, 256, NULL);
    if (SIXEL_FAILED(st)) {
        fprintf(stderr, "[spatial_render] sixel_dither_new failed: %d\n", st);
        sixel_output_unref(output);
        free(frame_buf);
        return -1;
    }

    st = sixel_dither_initialize(dither, frame_buf, SCREEN_WIDTH, SCREEN_HEIGHT,
                                PIXELFORMAT_RGB888, SIXEL_LARGE_AUTO, SIXEL_REP_AUTO, QUALITY_AUTO);
    if (SIXEL_FAILED(st)) {
        fprintf(stderr, "[spatial_render] sixel_dither_initialize failed: %d\n", st);
        sixel_dither_unref(dither);
        sixel_output_unref(output);
        free(frame_buf);
        return -1;
    }

    st = sixel_encode(frame_buf, SCREEN_WIDTH, SCREEN_HEIGHT, 3, dither, output);
    if (SIXEL_FAILED(st)) {
        fprintf(stderr, "[spatial_render] sixel_encode failed: %d\n", st);
    }

    sixel_dither_unref(dither);
    sixel_output_unref(output);
    free(frame_buf);

    return SIXEL_SUCCEEDED(st) ? 0 : -1;
}
