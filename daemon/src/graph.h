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
} Node;

typedef struct {
    Node *nodes;
    int node_count;
    int capacity;
} Graph;

void graph_init(Graph *graph);
void graph_free(Graph *graph);
void calculate_forces(Node *nodes, int count);

#endif /* SPATIAL_GRAPH_H */
