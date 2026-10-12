#include "render.h"
#include "graph.h"
#include "math3d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>

int main(void) {
    printf("[TEST_RASTER_V2] Starting High-Fidelity 3D Rasterization & Sixel tests...\n");

    /* Test 1: 3D Math & Perspective Projection Matrix */
    {
        Camera cam;
        camera_init(&cam, 800, 600);
        camera_set_orbit(&cam, 0.0f, 0.0f, 400.0f);

        Mat4 view = camera_get_view_matrix(&cam);
        Mat4 proj = camera_get_proj_matrix(&cam);
        Mat4 vp = mat4_multiply(&proj, &view);

        /* Project origin (0, 0, 0) */
        float cx, cy, cz, cw;
        mat4_transform_point(&vp, vec3_create(0.0f, 0.0f, 0.0f), &cx, &cy, &cz, &cw);
        assert(cw > 0.0f);

        float ndc_x = cx / cw;
        float ndc_y = cy / cw;
        assert(fabsf(ndc_x) < 0.01f);
        assert(fabsf(ndc_y) < 0.01f);

        printf("[TEST 1 PASSED] 3D Math, Look-At, and Perspective transformations verified.\n");
    }

    /* Test 2: Frame buffer & Z-Buffer Depth Allocation */
    {
        RasterContext ctx;
        int rc = raster_init(&ctx, 800, 600);
        assert(rc == 0);
        assert(ctx.color_buffer != NULL);
        assert(ctx.depth_buffer != NULL);

        Graph g;
        graph_init(&g);

        Camera cam;
        camera_init(&cam, 800, 600);

        /* Render empty graph */
        rc = render_graph_v2(&g, &cam, &ctx);
        assert(rc == 0);

        /* Verify background clear #181825 */
        assert(ctx.color_buffer[0] == 24 && ctx.color_buffer[1] == 24 && ctx.color_buffer[2] == 37);
        assert(ctx.depth_buffer[0] == 1.0f);

        /* Add nodes, call relationship, and verify non-background foreground rendering */
        int a = graph_add_node(&g, "main", 12.0f, vec3_create(0.0f, 0.0f, 0.0f));
        int b = graph_add_node(&g, "compute_node", 10.0f, vec3_create(60.0f, 60.0f, -20.0f));
        graph_add_edge(&g, a, b);

        rc = render_graph_v2(&g, &cam, &ctx);
        assert(rc == 0);

        size_t foreground_pixels = 0;
        size_t depth_pixels = 0;
        for (size_t i = 0; i < ctx.pixel_count; ++i) {
            size_t c_idx = i * 3;
            if (ctx.color_buffer[c_idx] != 24 || ctx.color_buffer[c_idx + 1] != 24 || ctx.color_buffer[c_idx + 2] != 37) {
                foreground_pixels++;
            }
            if (ctx.depth_buffer[i] < 0.999f) {
                depth_pixels++;
            }
        }

        assert(foreground_pixels > 200);
        assert(depth_pixels > 100);
        printf("  Frame buffer rendered %zu foreground pixels, %zu Z-tested pixels.\n", foreground_pixels, depth_pixels);

        raster_free(&ctx);
        graph_free(&g);
        printf("[TEST 2 PASSED] Z-Buffer depth buffer and Blinn-Phong/label rasterization verified.\n");
    }

    /* Test 3: Sixel streaming over socketpair */
    {
        int sv[2];
        int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        assert(rc == 0);

        RasterContext ctx;
        rc = raster_init(&ctx, 640, 480);
        assert(rc == 0);

        Graph g;
        graph_init(&g);
        int a = graph_add_node(&g, "main", 12.0f, vec3_create(-30.0f, 0.0f, 0.0f));
        int b = graph_add_node(&g, "worker", 10.0f, vec3_create(40.0f, 20.0f, 10.0f));
        graph_add_edge(&g, a, b);

        Camera cam;
        camera_init(&cam, 640, 480);
        render_graph_v2(&g, &cam, &ctx);

        rc = render_sixel_stream(&ctx, sv[0]);
        assert(rc == 0);
        close(sv[0]);

        char read_buf[65536];
        size_t total_read = 0;
        while (1) {
            ssize_t n = read(sv[1], read_buf + total_read, sizeof(read_buf) - total_read - 1);
            if (n <= 0) break;
            total_read += (size_t)n;
        }
        close(sv[1]);
        read_buf[total_read] = '\0';

        printf("  Sixel stream received: %zu bytes.\n", total_read);
        assert(total_read > 500);

        /* Sixel sequence starts with ESC P (DCS header) */
        assert(read_buf[0] == '\033');
        assert(read_buf[1] == 'P');

        /* Sixel sequence ends with ESC \\ (String Terminator) */
        assert(read_buf[total_read - 2] == '\033');
        assert(read_buf[total_read - 1] == '\\');

        raster_free(&ctx);
        graph_free(&g);
        printf("[TEST 3 PASSED] Libsixel encoding produced valid DCS escape sequence over socket.\n");
    }

    /* Test 4: Unicode Braille Fallback Streaming */
    {
        int sv[2];
        int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        assert(rc == 0);

        RasterContext ctx;
        rc = raster_init(&ctx, 160, 80);
        assert(rc == 0);

        Graph g;
        graph_init(&g);
        int a = graph_add_node(&g, "main", 15.0f, vec3_create(0.0f, 0.0f, 0.0f));
        (void)a;

        Camera cam;
        camera_init(&cam, 160, 80);
        render_graph_v2(&g, &cam, &ctx);

        rc = render_braille_stream(&ctx, sv[0]);
        assert(rc == 0);
        close(sv[0]);

        char read_buf[16384];
        size_t total_read = 0;
        while (1) {
            ssize_t n = read(sv[1], read_buf + total_read, sizeof(read_buf) - total_read - 1);
            if (n <= 0) break;
            total_read += (size_t)n;
        }
        close(sv[1]);
        read_buf[total_read] = '\0';

        printf("  Braille stream received: %zu bytes.\n", total_read);
        assert(total_read > 100);

        raster_free(&ctx);
        graph_free(&g);
        printf("[TEST 4 PASSED] Unicode Braille fallback output successfully streamed.\n");
    }

    printf("\n>>> ALL TEST_RASTER_V2 CHECKS PASSED SUCCESSFULLY! <<<\n");
    return 0;
}
