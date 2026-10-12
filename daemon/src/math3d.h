#ifndef SPATIAL_MATH3D_H
#define SPATIAL_MATH3D_H

#include <stdbool.h>

typedef struct {
    float x;
    float y;
    float z;
} Vec3;

typedef struct {
    float w;
    float x;
    float y;
    float z;
} Quat;

/* 4x4 Row-Major Matrix */
typedef struct {
    float m[4][4];
} Mat4;

/* Vector 3D operations */
static inline Vec3 vec3_create(float x, float y, float z) {
    Vec3 v = {x, y, z};
    return v;
}

Vec3 vec3_add(Vec3 a, Vec3 b);
Vec3 vec3_sub(Vec3 a, Vec3 b);
Vec3 vec3_scale(Vec3 v, float s);
float vec3_dot(Vec3 a, Vec3 b);
Vec3 vec3_cross(Vec3 a, Vec3 b);
float vec3_length(Vec3 v);
Vec3 vec3_normalize(Vec3 v);

/* Quaternion operations */
Quat quat_identity(void);
Quat quat_from_axis_angle(Vec3 axis, float angle_rad);
Quat quat_from_euler(float pitch_rad, float yaw_rad, float roll_rad);
Quat quat_multiply(Quat q1, Quat q2);
Quat quat_normalize(Quat q);
Quat quat_slerp(Quat q1, Quat q2, float t);

/* 4x4 Matrix transformations */
Mat4 mat4_identity(void);
Mat4 mat4_multiply(const Mat4 *a, const Mat4 *b);
Mat4 mat4_from_quat(Quat q);
Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up);
Mat4 mat4_perspective(float fov_y_rad, float aspect, float near_z, float far_z);

/* Transform 3D point through Mat4 to Clip Space (homogeneous coordinates) */
void mat4_transform_point(const Mat4 *m, Vec3 in, float *out_x, float *out_y, float *out_z, float *out_w);

#endif /* SPATIAL_MATH3D_H */
