#ifndef SPATIAL_RENDER_H
#define SPATIAL_RENDER_H

#include "graph.h"

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600

typedef struct {
    float focal_length;
    float camera_z;
    int width;
    int height;
} Camera;

void render_graph_to_sixel(const Graph *graph, int client_fd);

#endif /* SPATIAL_RENDER_H */
