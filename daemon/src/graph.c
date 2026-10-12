#include "graph.h"
#include <stdlib.h>

void graph_init(Graph *graph) {
    if (!graph) return;
    graph->nodes = NULL;
    graph->node_count = 0;
    graph->capacity = 0;
}

void graph_free(Graph *graph) {
    if (!graph) return;
    for (int i = 0; i < graph->node_count; ++i) {
        free(graph->nodes[i].edges);
        graph->nodes[i].edges = NULL;
    }
    free(graph->nodes);
    graph->nodes = NULL;
    graph->node_count = 0;
    graph->capacity = 0;
}

void calculate_forces(Node *nodes, int count) {
    (void)nodes;
    (void)count;
    /* Implemented in Phase 3 */
}
