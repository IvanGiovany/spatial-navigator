#ifndef SPATIAL_RENDER_H
#define SPATIAL_RENDER_H

#include "graph.h"

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define CAMERA_FOCAL_LENGTH 600.0f
#define CAMERA_Z 350.0f

typedef struct {
    float focal_length;
    float camera_z;
    int width;
    int height;
} Camera;

int project_3d_to_2d(Vec3 pos, float radius, const Camera *cam, float *out_x, float *out_y, float *out_r);
int render_graph_to_buffer(const Graph *graph, unsigned char *buffer, int width, int height);
int render_graph_to_sixel(const Graph *graph, int client_fd);

#endif /* SPATIAL_RENDER_H */
