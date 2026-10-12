#include "parser.h"
#include "graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>

static int has_edge_to(const Node *from, int to_id) {
    for (int i = 0; i < from->edge_count; ++i) {
        if (from->edges[i] == to_id) {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *sample_path = (argc > 1) ? argv[1] : "tests/sample_project.c";
    printf("[TEST] Running test_parser with sample: %s\n", sample_path);

    /* Test 1: Nonexistent file returns error */
    {
        Graph g;
        graph_init(&g);
        int rc = parse_source_file("/tmp/definitely_not_a_real_file_12345.c", &g);
        assert(rc == -1);
        assert(g.node_count == 0);
        graph_free(&g);
        printf("[TEST 1 PASSED] Non-existent file handled correctly.\n");
    }

    /* Test 2: Empty file returns empty graph */
    {
        const char *empty_path = "/tmp/spatial_empty_test.c";
        int fd = open(empty_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        assert(fd >= 0);
        close(fd);

        Graph g;
        graph_init(&g);
        int rc = parse_source_file(empty_path, &g);
        assert(rc == 0);
        assert(g.node_count == 0);
        graph_free(&g);
        unlink(empty_path);
        printf("[TEST 2 PASSED] Empty file handled cleanly.\n");
    }

    /* Test 3: Sample project with known call graph */
    {
        Graph g;
        graph_init(&g);
        int rc = parse_source_file(sample_path, &g);
        assert(rc == 0);
        printf("[TEST 3] Parsed sample project. Graph summary:\n");
        graph_print_summary(&g);

        assert(g.node_count == 5);

        int id_leaf_a = graph_find_node_by_name(&g, "leaf_a");
        int id_leaf_b = graph_find_node_by_name(&g, "leaf_b");
        int id_helper = graph_find_node_by_name(&g, "helper");
        int id_compute = graph_find_node_by_name(&g, "compute");
        int id_main = graph_find_node_by_name(&g, "main");

        assert(id_leaf_a >= 0);
        assert(id_leaf_b >= 0);
        assert(id_helper >= 0);
        assert(id_compute >= 0);
        assert(id_main >= 0);

        /* Validate initial random coordinates are strictly within [-50.0, 50.0] */
        for (int i = 0; i < g.node_count; ++i) {
            const Node *n = &g.nodes[i];
            assert(n->pos.x >= -50.0f && n->pos.x <= 50.0f);
            assert(n->pos.y >= -50.0f && n->pos.y <= 50.0f);
            assert(n->pos.z >= -50.0f && n->pos.z <= 50.0f);
            assert(n->radius >= 5.0f && n->radius <= 35.0f);
        }

        /* Validate edge connections */
        const Node *n_leaf_a = &g.nodes[id_leaf_a];
        const Node *n_leaf_b = &g.nodes[id_leaf_b];
        const Node *n_helper = &g.nodes[id_helper];
        const Node *n_compute = &g.nodes[id_compute];
        const Node *n_main = &g.nodes[id_main];

        assert(n_leaf_a->edge_count == 0);
        assert(n_leaf_b->edge_count == 0);

        /* helper calls leaf_a and leaf_b */
        assert(n_helper->edge_count == 2);
        assert(has_edge_to(n_helper, id_leaf_a));
        assert(has_edge_to(n_helper, id_leaf_b));

        /* compute calls helper (printf is external, no edge) */
        assert(n_compute->edge_count == 1);
        assert(has_edge_to(n_compute, id_helper));

        /* main calls compute */
        assert(n_main->edge_count == 1);
        assert(has_edge_to(n_main, id_compute));

        graph_free(&g);
        printf("[TEST 3 PASSED] Function definitions, call graph edges, and coords verified!\n");
    }

    /* Test 4: Parse self (main.c) */
    {
        Graph g;
        graph_init(&g);
        int rc = parse_source_file("src/main.c", &g);
        assert(rc == 0);
        assert(g.node_count >= 3); /* handle_signal, set_nonblocking, handle_client_request, main */
        printf("[TEST 4] Parsed src/main.c successfully: %d nodes found.\n", g.node_count);
        graph_print_summary(&g);
        graph_free(&g);
        printf("[TEST 4 PASSED] Real C file parsed without errors.\n");
    }

    printf("\n>>> ALL TEST_PARSER ASSERTIONS PASSED! <<<\n");
    return 0;
}
