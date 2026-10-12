#ifndef SPATIAL_GRAPH_H
#define SPATIAL_GRAPH_H

#include <stddef.h>

typedef struct {
    float x;
    float y;
    float z;
} Vec3;

typedef struct {
    int id;
    char name[128];
    Vec3 pos;
    Vec3 disp;
    float radius;
    int *edges;
    int edge_count;
    int edge_capacity;
} Node;

typedef struct {
    Node *nodes;
    int node_count;
    int capacity;
} Graph;

#define PHYSICS_K 50.0f
#define PHYSICS_INITIAL_TEMP 100.0f
#define PHYSICS_COOLING_FACTOR 0.95f
#define PHYSICS_ITERATIONS 150

void graph_init(Graph *graph);
void graph_free(Graph *graph);
int graph_add_node(Graph *graph, const char *name, float radius, Vec3 pos);
int graph_find_node_by_name(const Graph *graph, const char *name);
int graph_add_edge(Graph *graph, int from_id, int to_id);
void graph_print_summary(const Graph *graph);
void calculate_forces(Node *nodes, int count);
void graph_recenter(Node *nodes, int count);

#endif /* SPATIAL_GRAPH_H */
