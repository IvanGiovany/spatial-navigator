#include "render.h"
#include "graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>

int main(void) {
    printf("[TEST_RASTER] Starting Software Rasterization & Sixel tests...\n");

    /* Test 1: Perspective projection mathematics & near-plane clipping */
    {
        Camera cam = {
            .focal_length = 600.0f,
            .camera_z = 350.0f,
            .width = 800,
            .height = 600
        };

        float px, py, pr;
        int rc = project_3d_to_2d((Vec3){0.0f, 0.0f, 0.0f}, 10.0f, &cam, &px, &py, &pr);
        assert(rc == 0);
        assert(fabsf(px - 400.0f) < 0.01f);
        assert(fabsf(py - 300.0f) < 0.01f);
        assert(fabsf(pr - (10.0f * 600.0f / 350.0f)) < 0.01f);

        /* Far node */
        float far_x, far_y, far_r;
        rc = project_3d_to_2d((Vec3){0.0f, 0.0f, 650.0f}, 10.0f, &cam, &far_x, &far_y, &far_r);
        assert(rc == 0);
        assert(far_r < pr); /* Distant node has smaller projected radius */

        /* Node behind camera: near-plane clipping keeps Z_eff >= 20.0 */
        float clip_x, clip_y, clip_r;
        rc = project_3d_to_2d((Vec3){0.0f, 0.0f, -500.0f}, 10.0f, &cam, &clip_x, &clip_y, &clip_r);
        assert(rc == 0);
        assert(!isnan(clip_x) && !isnan(clip_y) && !isnan(clip_r));
        assert(clip_r <= 45.0f);

        printf("[TEST 1 PASSED] 3D perspective projection and near-plane clipping verified.\n");
    }

    /* Test 2: Frame buffer rasterization */
    {
        size_t sz = (size_t)(SCREEN_WIDTH * SCREEN_HEIGHT * 3);
        unsigned char *buf = malloc(sz);
        assert(buf != NULL);

        Graph g;
        graph_init(&g);

        /* Render empty graph */
        int rc = render_graph_to_buffer(&g, buf, SCREEN_WIDTH, SCREEN_HEIGHT);
        assert(rc == 0);
        /* Check background color #181825 */
        assert(buf[0] == 24 && buf[1] == 24 && buf[2] == 37);

        /* Add nodes and an edge */
        int a = graph_add_node(&g, "main", 12.0f, (Vec3){0.0f, 0.0f, 0.0f});
        int b = graph_add_node(&g, "compute", 10.0f, (Vec3){50.0f, 50.0f, 0.0f});
        graph_add_edge(&g, a, b);

        rc = render_graph_to_buffer(&g, buf, SCREEN_WIDTH, SCREEN_HEIGHT);
        assert(rc == 0);

        /* Count non-background pixels */
        size_t non_bg = 0;
        for (size_t i = 0; i < sz; i += 3) {
            if (buf[i] != 24 || buf[i + 1] != 24 || buf[i + 2] != 37) {
                non_bg++;
            }
        }
        assert(non_bg > 100);
        printf("  Frame buffer rendered %zu foreground pixels for 2 nodes + edge.\n", non_bg);

        free(buf);
        graph_free(&g);
        printf("[TEST 2 PASSED] Frame buffer rasterization correctly populated foreground pixels.\n");
    }

    /* Test 3: Sixel streaming over socketpair */
    {
        int sv[2];
        int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        assert(rc == 0);

        Graph g;
        graph_init(&g);
        int a = graph_add_node(&g, "main", 12.0f, (Vec3){-20.0f, 0.0f, 0.0f});
        int b = graph_add_node(&g, "helper", 8.0f, (Vec3){30.0f, 0.0f, 0.0f});
        graph_add_edge(&g, a, b);

        /* Stream Sixel directly into socket sv[0] */
        rc = render_graph_to_sixel(&g, sv[0]);
        assert(rc == 0);
        close(sv[0]);

        /* Read all sixel bytes from sv[1] */
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

        /* Sixel sequence ends with ESC \ (String Terminator) */
        assert(read_buf[total_read - 2] == '\033');
        assert(read_buf[total_read - 1] == '\\');

        graph_free(&g);
        printf("[TEST 3 PASSED] Libsixel encoding produced valid DCS escape sequence over socket.\n");
    }

    printf("\n>>> ALL TEST_RASTER CHECKS PASSED SUCCESSFULLY! <<<\n");
    return 0;
}
