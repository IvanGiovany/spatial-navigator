#include "graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

static float distance_3d(Vec3 a, Vec3 b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static void assert_no_nan_or_inf(const Node *nodes, int count) {
    for (int i = 0; i < count; ++i) {
        assert(!isnan(nodes[i].pos.x));
        assert(!isnan(nodes[i].pos.y));
        assert(!isnan(nodes[i].pos.z));
        assert(!isinf(nodes[i].pos.x));
        assert(!isinf(nodes[i].pos.y));
        assert(!isinf(nodes[i].pos.z));
    }
}

static void assert_centroid_is_zero(const Node *nodes, int count) {
    Vec3 sum = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < count; ++i) {
        sum.x += nodes[i].pos.x;
        sum.y += nodes[i].pos.y;
        sum.z += nodes[i].pos.z;
    }
    float cx = sum.x / (float)count;
    float cy = sum.y / (float)count;
    float cz = sum.z / (float)count;
    assert(fabsf(cx) < 0.01f);
    assert(fabsf(cy) < 0.01f);
    assert(fabsf(cz) < 0.01f);
}

int main(void) {
    printf("[TEST_PHYSICS] Starting Fruchterman-Reingold 3D simulation tests...\n");

    /* Test 1: Edge cases (0 or 1 node) */
    {
        calculate_forces(NULL, 0);
        Node single = {
            .id = 0,
            .name = "single",
            .pos = {10.0f, 20.0f, 30.0f},
            .disp = {0.0f, 0.0f, 0.0f},
            .radius = 10.0f,
            .edges = NULL,
            .edge_count = 0,
            .edge_capacity = 0
        };
        calculate_forces(&single, 1);
        assert(single.pos.x == 10.0f);
        assert(single.pos.y == 20.0f);
        assert(single.pos.z == 30.0f);
        printf("[TEST 1 PASSED] 0 and 1 node edge cases handled safely.\n");
    }

    /* Test 2: Two disconnected nodes repel each other */
    {
        Graph g;
        graph_init(&g);
        graph_add_node(&g, "node_a", 10.0f, (Vec3){0.0f, 0.0f, 0.0f});
        graph_add_node(&g, "node_b", 10.0f, (Vec3){1.0f, 0.0f, 0.0f});

        float initial_d = distance_3d(g.nodes[0].pos, g.nodes[1].pos);
        assert(fabsf(initial_d - 1.0f) < 0.001f);

        calculate_forces(g.nodes, g.node_count);

        float final_d = distance_3d(g.nodes[0].pos, g.nodes[1].pos);
        assert_no_nan_or_inf(g.nodes, g.node_count);
        assert_centroid_is_zero(g.nodes, g.node_count);

        printf("  Disconnected nodes: initial dist = %.2f, final dist = %.2f\n", initial_d, final_d);
        assert(final_d > initial_d);
        assert(final_d > 40.0f);

        graph_free(&g);
        printf("[TEST 2 PASSED] Disconnected nodes repelled successfully.\n");
    }

    /* Test 3: Two connected nodes attract toward equilibrium distance k (~50.0) */
    {
        Graph g;
        graph_init(&g);
        int a = graph_add_node(&g, "caller", 10.0f, (Vec3){-250.0f, 0.0f, 0.0f});
        int b = graph_add_node(&g, "callee", 10.0f, (Vec3){250.0f, 0.0f, 0.0f});
        graph_add_edge(&g, a, b);

        float initial_d = distance_3d(g.nodes[a].pos, g.nodes[b].pos);
        assert(fabsf(initial_d - 500.0f) < 0.001f);

        calculate_forces(g.nodes, g.node_count);

        float final_d = distance_3d(g.nodes[a].pos, g.nodes[b].pos);
        assert_no_nan_or_inf(g.nodes, g.node_count);
        assert_centroid_is_zero(g.nodes, g.node_count);

        printf("  Connected nodes: initial dist = %.2f, final dist = %.2f (target k=%.1f)\n",
               initial_d, final_d, PHYSICS_K);
        assert(final_d < initial_d);
        /* Should settle near equilibrium k = 50.0 */
        assert(final_d >= 30.0f && final_d <= 75.0f);

        graph_free(&g);
        printf("[TEST 3 PASSED] Connected nodes attracted to equilibrium distance.\n");
    }

    /* Test 4: Multi-node interconnected graph */
    {
        Graph g;
        graph_init(&g);
        int n_main = graph_add_node(&g, "main", 12.0f, (Vec3){-30.0f, 40.0f, 10.0f});
        int n_comp = graph_add_node(&g, "compute", 10.0f, (Vec3){20.0f, -10.0f, -30.0f});
        int n_help = graph_add_node(&g, "helper", 8.0f, (Vec3){5.0f, 50.0f, -15.0f});
        int n_la   = graph_add_node(&g, "leaf_a", 6.0f, (Vec3){-45.0f, -20.0f, 35.0f});
        int n_lb   = graph_add_node(&g, "leaf_b", 6.0f, (Vec3){10.0f, -40.0f, 40.0f});

        graph_add_edge(&g, n_main, n_comp);
        graph_add_edge(&g, n_comp, n_help);
        graph_add_edge(&g, n_help, n_la);
        graph_add_edge(&g, n_help, n_lb);

        printf("  Pre-simulation summary:\n");
        graph_print_summary(&g);

        calculate_forces(g.nodes, g.node_count);

        printf("  Post-simulation summary:\n");
        graph_print_summary(&g);

        assert_no_nan_or_inf(g.nodes, g.node_count);
        assert_centroid_is_zero(g.nodes, g.node_count);

        /* Validate helper is closer to leaf_a and leaf_b than distant unlinked pairs */
        float d_help_la = distance_3d(g.nodes[n_help].pos, g.nodes[n_la].pos);
        float d_help_lb = distance_3d(g.nodes[n_help].pos, g.nodes[n_lb].pos);
        assert(d_help_la < 120.0f);
        assert(d_help_lb < 120.0f);

        graph_free(&g);
        printf("[TEST 4 PASSED] 5-node interconnected graph stabilized with centroid at origin.\n");
    }

    printf("\n>>> ALL TEST_PHYSICS CHECKS PASSED SUCCESSFULLY! <<<\n");
    return 0;
}
