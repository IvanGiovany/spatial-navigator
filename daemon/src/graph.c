#include "graph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        graph->nodes[i].edge_count = 0;
        graph->nodes[i].edge_capacity = 0;
    }
    free(graph->nodes);
    graph->nodes = NULL;
    graph->node_count = 0;
    graph->capacity = 0;
}

int graph_find_node_by_name(const Graph *graph, const char *name) {
    if (!graph || !name) return -1;
    for (int i = 0; i < graph->node_count; ++i) {
        if (strncmp(graph->nodes[i].name, name, sizeof(graph->nodes[i].name)) == 0) {
            return i;
        }
    }
    return -1;
}

int graph_add_node(Graph *graph, const char *name, float radius, Vec3 pos) {
    if (!graph || !name) return -1;

    int existing_id = graph_find_node_by_name(graph, name);
    if (existing_id >= 0) {
        return existing_id;
    }

    if (graph->node_count >= graph->capacity) {
        int new_capacity = (graph->capacity == 0) ? 16 : graph->capacity * 2;
        Node *new_nodes = realloc(graph->nodes, (size_t)new_capacity * sizeof(Node));
        if (!new_nodes) {
            return -1;
        }
        graph->nodes = new_nodes;
        graph->capacity = new_capacity;
    }

    int id = graph->node_count++;
    Node *node = &graph->nodes[id];
    node->id = id;
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->name[sizeof(node->name) - 1] = '\0';
    node->pos = pos;
    node->disp = (Vec3){0.0f, 0.0f, 0.0f};
    node->radius = radius;
    node->edges = NULL;
    node->edge_count = 0;
    node->edge_capacity = 0;

    return id;
}

int graph_add_edge(Graph *graph, int from_id, int to_id) {
    if (!graph) return -1;
    if (from_id < 0 || from_id >= graph->node_count) return -1;
    if (to_id < 0 || to_id >= graph->node_count) return -1;

    Node *from = &graph->nodes[from_id];

    /* De-duplicate edges */
    for (int i = 0; i < from->edge_count; ++i) {
        if (from->edges[i] == to_id) {
            return 0;
        }
    }

    if (from->edge_count >= from->edge_capacity) {
        int new_cap = (from->edge_capacity == 0) ? 4 : from->edge_capacity * 2;
        int *new_edges = realloc(from->edges, (size_t)new_cap * sizeof(int));
        if (!new_edges) {
            return -1;
        }
        from->edges = new_edges;
        from->edge_capacity = new_cap;
    }

    from->edges[from->edge_count++] = to_id;
    return 0;
}

void graph_print_summary(const Graph *graph) {
    if (!graph) return;
    int total_edges = 0;
    for (int i = 0; i < graph->node_count; ++i) {
        total_edges += graph->nodes[i].edge_count;
    }
    printf("[graph] Nodes: %d, Directed Edges: %d\n", graph->node_count, total_edges);
    for (int i = 0; i < graph->node_count; ++i) {
        const Node *n = &graph->nodes[i];
        printf("  Node[%d] '%s' (r=%.2f, pos=[%.1f, %.1f, %.1f]) -> calls:",
               n->id, n->name, n->radius, n->pos.x, n->pos.y, n->pos.z);
        for (int j = 0; j < n->edge_count; ++j) {
            int callee_id = n->edges[j];
            printf(" %s(#%d)", graph->nodes[callee_id].name, callee_id);
        }
        printf("\n");
    }
}

void calculate_forces(Node *nodes, int count) {
    (void)nodes;
    (void)count;
    /* Implemented in Phase 3 */
}
