#include "graph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

void graph_recenter(Node *nodes, int count) {
    if (!nodes || count <= 0) return;

    Vec3 center = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < count; ++i) {
        center.x += nodes[i].pos.x;
        center.y += nodes[i].pos.y;
        center.z += nodes[i].pos.z;
    }
    center.x /= (float)count;
    center.y /= (float)count;
    center.z /= (float)count;

    for (int i = 0; i < count; ++i) {
        nodes[i].pos.x -= center.x;
        nodes[i].pos.y -= center.y;
        nodes[i].pos.z -= center.z;
    }
}

void calculate_forces(Node *nodes, int count) {
    if (!nodes || count <= 1) {
        return;
    }

    const float k = PHYSICS_K;
    const float k_sq = k * k;
    float temperature = PHYSICS_INITIAL_TEMP;

    for (int iter = 0; iter < PHYSICS_ITERATIONS; ++iter) {
        /* Step 1: Reset displacement accumulators */
        for (int i = 0; i < count; ++i) {
            nodes[i].disp = (Vec3){0.0f, 0.0f, 0.0f};
        }

        /* Step 2: Repulsive forces (O(N^2) pairwise) */
        for (int u = 0; u < count - 1; ++u) {
            for (int v = u + 1; v < count; ++v) {
                float dx = nodes[u].pos.x - nodes[v].pos.x;
                float dy = nodes[u].pos.y - nodes[v].pos.y;
                float dz = nodes[u].pos.z - nodes[v].pos.z;
                float dist_sq = dx * dx + dy * dy + dz * dz;

                /* Epsilon guard against division by zero */
                if (dist_sq < 0.0001f) {
                    dx = 0.01f;
                    dy = 0.01f;
                    dz = 0.01f;
                    dist_sq = dx * dx + dy * dy + dz * dz;
                }

                /* f_r = k^2 / dist; force_factor = f_r / dist = k^2 / dist_sq */
                float force_factor = k_sq / dist_sq;

                float fx = dx * force_factor;
                float fy = dy * force_factor;
                float fz = dz * force_factor;

                nodes[u].disp.x += fx;
                nodes[u].disp.y += fy;
                nodes[u].disp.z += fz;

                nodes[v].disp.x -= fx;
                nodes[v].disp.y -= fy;
                nodes[v].disp.z -= fz;
            }
        }

        /* Step 3: Attractive forces (Edges) */
        for (int u = 0; u < count; ++u) {
            for (int j = 0; j < nodes[u].edge_count; ++j) {
                int v = nodes[u].edges[j];
                if (v < 0 || v >= count || u == v) {
                    continue;
                }

                float dx = nodes[v].pos.x - nodes[u].pos.x;
                float dy = nodes[v].pos.y - nodes[u].pos.y;
                float dz = nodes[v].pos.z - nodes[u].pos.z;
                float dist_sq = dx * dx + dy * dy + dz * dz;

                if (dist_sq < 0.0001f) {
                    continue;
                }

                float dist = sqrtf(dist_sq);
                /* f_a = dist^2 / k; force_factor = f_a / dist = dist / k */
                float force_factor = dist / k;

                float fx = dx * force_factor;
                float fy = dy * force_factor;
                float fz = dz * force_factor;

                nodes[u].disp.x += fx;
                nodes[u].disp.y += fy;
                nodes[u].disp.z += fz;

                nodes[v].disp.x -= fx;
                nodes[v].disp.y -= fy;
                nodes[v].disp.z -= fz;
            }
        }

        /* Step 4: Apply displacement capped by current temperature */
        for (int i = 0; i < count; ++i) {
            float disp_len_sq = nodes[i].disp.x * nodes[i].disp.x +
                                nodes[i].disp.y * nodes[i].disp.y +
                                nodes[i].disp.z * nodes[i].disp.z;

            if (disp_len_sq > 0.0001f) {
                float disp_len = sqrtf(disp_len_sq);
                float step = (disp_len < temperature) ? disp_len : temperature;
                float scale = step / disp_len;

                nodes[i].pos.x += nodes[i].disp.x * scale;
                nodes[i].pos.y += nodes[i].disp.y * scale;
                nodes[i].pos.z += nodes[i].disp.z * scale;
            }
        }

        /* Step 5: Cool temperature */
        temperature *= PHYSICS_COOLING_FACTOR;
    }

    /* Centroid re-centering for camera positioning */
    graph_recenter(nodes, count);
}
