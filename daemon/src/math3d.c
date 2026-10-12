#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "math3d.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* --- Vec3 Implementation --- */

Vec3 vec3_add(Vec3 a, Vec3 b) {
    Vec3 r = { a.x + b.x, a.y + b.y, a.z + b.z };
    return r;
}

Vec3 vec3_sub(Vec3 a, Vec3 b) {
    Vec3 r = { a.x - b.x, a.y - b.y, a.z - b.z };
    return r;
}

Vec3 vec3_scale(Vec3 v, float s) {
    Vec3 r = { v.x * s, v.y * s, v.z * s };
    return r;
}

float vec3_dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 vec3_cross(Vec3 a, Vec3 b) {
    Vec3 r = {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
    return r;
}

float vec3_length(Vec3 v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vec3 vec3_normalize(Vec3 v) {
    float len = vec3_length(v);
    if (len > 1e-6f) {
        float inv = 1.0f / len;
        Vec3 r = { v.x * inv, v.y * inv, v.z * inv };
        return r;
    }
    Vec3 zero = { 0.0f, 0.0f, 0.0f };
    return zero;
}

/* --- Quaternion Implementation --- */

Quat quat_identity(void) {
    Quat q = { 1.0f, 0.0f, 0.0f, 0.0f };
    return q;
}

Quat quat_from_axis_angle(Vec3 axis, float angle_rad) {
    Vec3 norm_axis = vec3_normalize(axis);
    float half = angle_rad * 0.5f;
    float s = sinf(half);
    Quat q = {
        cosf(half),
        norm_axis.x * s,
        norm_axis.y * s,
        norm_axis.z * s
    };
    return q;
}

Quat quat_from_euler(float pitch_rad, float yaw_rad, float roll_rad) {
    /* Tait-Bryan angles: Yaw (Y), Pitch (X), Roll (Z) */
    float cy = cosf(yaw_rad * 0.5f);
    float sy = sinf(yaw_rad * 0.5f);
    float cp = cosf(pitch_rad * 0.5f);
    float sp = sinf(pitch_rad * 0.5f);
    float cr = cosf(roll_rad * 0.5f);
    float sr = sinf(roll_rad * 0.5f);

    Quat q;
    q.w = cr * cp * cy + sr * sp * sy;
    q.x = sr * cp * cy - cr * sp * sy;
    q.y = cr * sp * cy + sr * cp * sy;
    q.z = cr * cp * sy - sr * sp * cy;
    return quat_normalize(q);
}

Quat quat_multiply(Quat q1, Quat q2) {
    Quat q;
    q.w = q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z;
    q.x = q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y;
    q.y = q1.w * q2.y - q1.x * q2.z + q1.y * q2.w + q1.z * q2.x;
    q.z = q1.w * q2.z + q1.x * q2.y - q1.y * q2.x + q1.z * q2.w;
    return q;
}

Quat quat_normalize(Quat q) {
    float mag = sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (mag > 1e-6f) {
        float inv = 1.0f / mag;
        Quat r = { q.w * inv, q.x * inv, q.y * inv, q.z * inv };
        return r;
    }
    return quat_identity();
}

Quat quat_slerp(Quat q1, Quat q2, float t) {
    q1 = quat_normalize(q1);
    q2 = quat_normalize(q2);

    float dot = q1.w * q2.w + q1.x * q2.x + q1.y * q2.y + q1.z * q2.z;

    /* If dot < 0, take shortest path around sphere */
    if (dot < 0.0f) {
        q2.w = -q2.w;
        q2.x = -q2.x;
        q2.y = -q2.y;
        q2.z = -q2.z;
        dot = -dot;
    }

    if (dot > 0.9995f) {
        /* Very close: use linear interpolation to avoid division by zero */
        Quat res = {
            q1.w + t * (q2.w - q1.w),
            q1.x + t * (q2.x - q1.x),
            q1.y + t * (q2.y - q1.y),
            q1.z + t * (q2.z - q1.z)
        };
        return quat_normalize(res);
    }

    float theta = acosf(dot);
    float sin_theta = sinf(theta);
    float w1 = sinf((1.0f - t) * theta) / sin_theta;
    float w2 = sinf(t * theta) / sin_theta;

    Quat res = {
        w1 * q1.w + w2 * q2.w,
        w1 * q1.x + w2 * q2.x,
        w1 * q1.y + w2 * q2.y,
        w1 * q1.z + w2 * q2.z
    };
    return quat_normalize(res);
}

/* --- Mat4 Implementation --- */

Mat4 mat4_identity(void) {
    Mat4 res = {{{0}}};
    res.m[0][0] = 1.0f;
    res.m[1][1] = 1.0f;
    res.m[2][2] = 1.0f;
    res.m[3][3] = 1.0f;
    return res;
}

Mat4 mat4_multiply(const Mat4 *a, const Mat4 *b) {
    Mat4 res = {{{0}}};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a->m[r][k] * b->m[k][c];
            }
            res.m[r][c] = sum;
        }
    }
    return res;
}

Mat4 mat4_from_quat(Quat q) {
    Mat4 res = mat4_identity();
    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    res.m[0][0] = 1.0f - 2.0f * (yy + zz);
    res.m[0][1] = 2.0f * (xy - wz);
    res.m[0][2] = 2.0f * (xz + wy);
    res.m[0][3] = 0.0f;

    res.m[1][0] = 2.0f * (xy + wz);
    res.m[1][1] = 1.0f - 2.0f * (xx + zz);
    res.m[1][2] = 2.0f * (yz - wx);
    res.m[1][3] = 0.0f;

    res.m[2][0] = 2.0f * (xz - wy);
    res.m[2][1] = 2.0f * (yz + wx);
    res.m[2][2] = 1.0f - 2.0f * (xx + yy);
    res.m[2][3] = 0.0f;

    res.m[3][0] = 0.0f;
    res.m[3][1] = 0.0f;
    res.m[3][2] = 0.0f;
    res.m[3][3] = 1.0f;

    return res;
}

Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 f = vec3_normalize(vec3_sub(target, eye));
    Vec3 s = vec3_normalize(vec3_cross(f, up));
    Vec3 u = vec3_cross(s, f);

    Mat4 res = mat4_identity();
    res.m[0][0] = s.x;
    res.m[0][1] = s.y;
    res.m[0][2] = s.z;
    res.m[0][3] = -vec3_dot(s, eye);

    res.m[1][0] = u.x;
    res.m[1][1] = u.y;
    res.m[1][2] = u.z;
    res.m[1][3] = -vec3_dot(u, eye);

    res.m[2][0] = -f.x;
    res.m[2][1] = -f.y;
    res.m[2][2] = -f.z;
    res.m[2][3] = vec3_dot(f, eye);

    res.m[3][0] = 0.0f;
    res.m[3][1] = 0.0f;
    res.m[3][2] = 0.0f;
    res.m[3][3] = 1.0f;

    return res;
}

Mat4 mat4_perspective(float fov_y_rad, float aspect, float near_z, float far_z) {
    Mat4 res = {{{0}}};
    float f = 1.0f / tanf(fov_y_rad * 0.5f);

    res.m[0][0] = f / aspect;
    res.m[1][1] = f;
    res.m[2][2] = (far_z + near_z) / (near_z - far_z);
    res.m[2][3] = (2.0f * far_z * near_z) / (near_z - far_z);
    res.m[3][2] = -1.0f;
    res.m[3][3] = 0.0f;

    return res;
}

void mat4_transform_point(const Mat4 *m, Vec3 in, float *out_x, float *out_y, float *out_z, float *out_w) {
    float x = in.x;
    float y = in.y;
    float z = in.z;

    *out_x = m->m[0][0] * x + m->m[0][1] * y + m->m[0][2] * z + m->m[0][3];
    *out_y = m->m[1][0] * x + m->m[1][1] * y + m->m[1][2] * z + m->m[1][3];
    *out_z = m->m[2][0] * x + m->m[2][1] * y + m->m[2][2] * z + m->m[2][3];
    *out_w = m->m[3][0] * x + m->m[3][1] * y + m->m[3][2] * z + m->m[3][3];
}
