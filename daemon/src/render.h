#ifndef SPATIAL_RENDER_H
#define SPATIAL_RENDER_H

#include "graph.h"
#include "math3d.h"
#include <stdint.h>
#include <stddef.h>

#define DEFAULT_VIEW_WIDTH  800
#define DEFAULT_VIEW_HEIGHT 600

/* Color structure (RGB24) */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} ColorRGB;

/* Camera specification supporting Euler angles, Quaternions, and dynamic FOV */
typedef struct {
    Vec3 target;            /* Centroid/focus point */
    float distance;         /* Distance from target */
    Quat orientation;       /* Camera rotation quaternion */
    float fov_y_rad;        /* Field of view in radians (default: pi/3 = 60 deg) */
    float near_z;           /* Near clipping plane (default: 10.0f) */
    float far_z;            /* Far clipping plane (default: 2500.0f) */
    int width;              /* Viewport pixel width */
    int height;             /* Viewport pixel height */
} Camera;

/* Software Rasterizer Context */
typedef struct {
    int width;
    int height;
    size_t pixel_count;
    uint8_t *color_buffer;  /* RGB24: 3 bytes per pixel */
    float *depth_buffer;    /* 32-bit floating point depth (0.0f near, 1.0f far) */
} RasterContext;

/* Projected 2D Node metadata */
typedef struct {
    int node_id;
    float screen_x;
    float screen_y;
    float screen_r;
    float depth;            /* NDC normalized depth [0.0, 1.0] */
    bool is_visible;
} ProjectedNode;

/* Raster Context Lifetime */
int raster_init(RasterContext *ctx, int width, int height);
int raster_resize(RasterContext *ctx, int width, int height);
void raster_clear(RasterContext *ctx, ColorRGB bg_color);
void raster_free(RasterContext *ctx);

/* 3D Camera Helpers */
void camera_init(Camera *cam, int width, int height);
void camera_set_orbit(Camera *cam, float yaw_rad, float pitch_rad, float distance);
Mat4 camera_get_view_matrix(const Camera *cam);
Mat4 camera_get_proj_matrix(const Camera *cam);

/* High-Fidelity Rendering Pipeline */
int render_graph_v2(const Graph *graph, const Camera *cam, RasterContext *ctx);

/* Sixel Output Streaming */
int render_sixel_stream(const RasterContext *ctx, int client_fd);

/* Unicode Braille Fallback Rendering (for terminals without Sixel support) */
int render_braille_stream(const RasterContext *ctx, int client_fd);

#endif /* SPATIAL_RENDER_H */
