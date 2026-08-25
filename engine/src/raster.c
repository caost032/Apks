#include "odg_internal.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float x;
    float y;
    float z;
} V3;

typedef struct {
    float x;
    float y;
    float z;
    int visible;
} SV;

typedef struct {
    V3 eye;
    V3 right;
    V3 up;
    V3 forward;
    float focal;
    float cx;
    float cy;
} Camera;

static V3 v3(float x, float y, float z) {
    V3 v = {x, y, z};
    return v;
}

static V3 vadd(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 vsub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 vmul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float vdot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 vcross(V3 a, V3 b) {
    return v3(a.y * b.z - a.z * b.y,
              a.z * b.x - a.x * b.z,
              a.x * b.y - a.y * b.x);
}
static V3 vnorm(V3 a) {
    float len = sqrtf(vdot(a, a));
    if (len < 0.00001f) return v3(0.0f, 1.0f, 0.0f);
    return vmul(a, 1.0f / len);
}

static uint32_t pack_rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    return (r & UINT32_C(255)) |
           ((g & UINT32_C(255)) << 8u) |
           ((b & UINT32_C(255)) << 16u) |
           ((a & UINT32_C(255)) << 24u);
}

static uint32_t from_rrggbbaa(uint32_t c) {
    return pack_rgba((c >> 24u) & UINT32_C(255), (c >> 16u) & UINT32_C(255),
                     (c >> 8u) & UINT32_C(255), c & UINT32_C(255));
}

static uint32_t shade(uint32_t c, float factor) {
    uint32_t r = c & UINT32_C(255);
    uint32_t g = (c >> 8u) & UINT32_C(255);
    uint32_t b = (c >> 16u) & UINT32_C(255);
    uint32_t a = (c >> 24u) & UINT32_C(255);
    float rf = odg_clampf((float)r * factor, 0.0f, 255.0f);
    float gf = odg_clampf((float)g * factor, 0.0f, 255.0f);
    float bf = odg_clampf((float)b * factor, 0.0f, 255.0f);
    return pack_rgba((uint32_t)lrintf(rf), (uint32_t)lrintf(gf), (uint32_t)lrintf(bf), a);
}

int odg_raster_init(OdgRaster *raster, uint32_t width, uint32_t height) {
    if (raster == NULL || width == 0u || height == 0u) return 0;
    (void)memset(raster, 0, sizeof(*raster));
    return odg_raster_resize(raster, width, height);
}

int odg_raster_resize(OdgRaster *raster, uint32_t width, uint32_t height) {
    size_t count;
    uint32_t *pixels;
    float *depth;
    if (raster == NULL || width == 0u || height == 0u) return 0;
    if (raster->width == width && raster->height == height && raster->pixels != NULL && raster->depth != NULL) return 1;
    count = (size_t)width * (size_t)height;
    if (count > (size_t)ODG_RENDER_MAX_W * (size_t)ODG_RENDER_MAX_H) return 0;
    pixels = (uint32_t *)malloc(count * sizeof(*pixels));
    depth = (float *)malloc(count * sizeof(*depth));
    if (pixels == NULL || depth == NULL) {
        free(pixels);
        free(depth);
        return 0;
    }
    free(raster->pixels);
    free(raster->depth);
    raster->pixels = pixels;
    raster->depth = depth;
    raster->width = width;
    raster->height = height;
    return 1;
}

void odg_raster_destroy(OdgRaster *raster) {
    if (raster == NULL) return;
    free(raster->pixels);
    free(raster->depth);
    (void)memset(raster, 0, sizeof(*raster));
}

static void clear_frame(OdgRaster *raster) {
    uint32_t y;
    for (y = 0u; y < raster->height; ++y) {
        float t = raster->height > 1u ? (float)y / (float)(raster->height - 1u) : 0.0f;
        uint32_t r = (uint32_t)lrintf(odg_clampf(48.0f + 35.0f * t, 0.0f, 255.0f));
        uint32_t g = (uint32_t)lrintf(odg_clampf(89.0f + 55.0f * t, 0.0f, 255.0f));
        uint32_t b = (uint32_t)lrintf(odg_clampf(132.0f + 45.0f * t, 0.0f, 255.0f));
        uint32_t color = pack_rgba(r, g, b, 255u);
        uint32_t x;
        for (x = 0u; x < raster->width; ++x) {
            size_t idx = (size_t)y * (size_t)raster->width + (size_t)x;
            raster->pixels[idx] = color;
            raster->depth[idx] = FLT_MAX;
        }
    }
}

static Camera make_camera(const OdgRaster *raster, const OdgPlayerState *p) {
    Camera c;
    float cp = cosf(p->camera_pitch);
    V3 forward = vnorm(v3(sinf(p->camera_yaw) * cp, sinf(p->camera_pitch), cosf(p->camera_yaw) * cp));
    V3 anchor = v3(p->x, p->y + 1.35f, p->z);
    c.forward = forward;
    c.eye = vsub(anchor, vmul(forward, p->camera_distance));
    if (c.eye.y < 0.18f) c.eye.y = 0.18f;
    c.right = vnorm(vcross(v3(0.0f, 1.0f, 0.0f), c.forward));
    c.up = vnorm(vcross(c.forward, c.right));
    c.cx = (float)raster->width * 0.5f;
    c.cy = (float)raster->height * 0.5f;
    c.focal = (float)(raster->width < raster->height ? raster->width : raster->height) * 0.92f;
    return c;
}

static SV project(const Camera *c, V3 p) {
    V3 rel = vsub(p, c->eye);
    float z = vdot(rel, c->forward);
    SV out;
    out.z = z;
    out.visible = z > 0.08f ? 1 : 0;
    if (out.visible != 0) {
        out.x = c->cx + vdot(rel, c->right) * c->focal / z;
        out.y = c->cy - vdot(rel, c->up) * c->focal / z;
    } else {
        out.x = 0.0f;
        out.y = 0.0f;
    }
    return out;
}

static float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void triangle(OdgRaster *raster, const Camera *camera, V3 a, V3 b, V3 c, uint32_t color) {
    SV pa = project(camera, a);
    SV pb = project(camera, b);
    SV pc = project(camera, c);
    float area;
    float minxf;
    float maxxf;
    float minyf;
    float maxyf;
    int minx;
    int maxx;
    int miny;
    int maxy;
    int y;
    if (pa.visible == 0 || pb.visible == 0 || pc.visible == 0) return;
    area = edge(pa.x, pa.y, pb.x, pb.y, pc.x, pc.y);
    if (fabsf(area) < 0.001f) return;
    minxf = floorf(fminf(pa.x, fminf(pb.x, pc.x)));
    maxxf = ceilf(fmaxf(pa.x, fmaxf(pb.x, pc.x)));
    minyf = floorf(fminf(pa.y, fminf(pb.y, pc.y)));
    maxyf = ceilf(fmaxf(pa.y, fmaxf(pb.y, pc.y)));
    if (maxxf < 0.0f || maxyf < 0.0f || minxf >= (float)raster->width || minyf >= (float)raster->height) return;
    minx = (int)fmaxf(0.0f, minxf);
    miny = (int)fmaxf(0.0f, minyf);
    maxx = (int)fminf((float)(raster->width - 1u), maxxf);
    maxy = (int)fminf((float)(raster->height - 1u), maxyf);
    for (y = miny; y <= maxy; ++y) {
        int x;
        for (x = minx; x <= maxx; ++x) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float w0 = edge(pb.x, pb.y, pc.x, pc.y, px, py);
            float w1 = edge(pc.x, pc.y, pa.x, pa.y, px, py);
            float w2 = edge(pa.x, pa.y, pb.x, pb.y, px, py);
            int inside = area > 0.0f ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) :
                                      (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
            if (inside != 0) {
                float inv_area = 1.0f / area;
                float z = (w0 * pa.z + w1 * pb.z + w2 * pc.z) * inv_area;
                size_t idx = (size_t)(uint32_t)y * (size_t)raster->width + (size_t)(uint32_t)x;
                if (z > 0.0f && z < raster->depth[idx]) {
                    raster->depth[idx] = z;
                    raster->pixels[idx] = color;
                }
            }
        }
    }
}

static void quad(OdgRaster *raster, const Camera *camera, V3 a, V3 b, V3 c, V3 d, uint32_t color) {
    triangle(raster, camera, a, b, c, color);
    triangle(raster, camera, a, c, d, color);
}

static V3 rotate_y(V3 p, float yaw) {
    float s = sinf(yaw);
    float c = cosf(yaw);
    return v3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}

static void box(OdgRaster *raster, const Camera *camera, V3 center, V3 half, float yaw, uint32_t rrggbbaa) {
    V3 local[8] = {
        {-half.x,-half.y,-half.z}, { half.x,-half.y,-half.z},
        { half.x, half.y,-half.z}, {-half.x, half.y,-half.z},
        {-half.x,-half.y, half.z}, { half.x,-half.y, half.z},
        { half.x, half.y, half.z}, {-half.x, half.y, half.z}
    };
    V3 v[8];
    uint32_t i;
    uint32_t base = from_rrggbbaa(rrggbbaa);
    for (i = 0u; i < 8u; ++i) v[i] = vadd(center, rotate_y(local[i], yaw));
    quad(raster, camera, v[4], v[5], v[6], v[7], shade(base, 1.02f));
    quad(raster, camera, v[1], v[0], v[3], v[2], shade(base, 0.72f));
    quad(raster, camera, v[0], v[4], v[7], v[3], shade(base, 0.82f));
    quad(raster, camera, v[5], v[1], v[2], v[6], shade(base, 0.91f));
    quad(raster, camera, v[3], v[7], v[6], v[2], shade(base, 1.10f));
    quad(raster, camera, v[0], v[1], v[5], v[4], shade(base, 0.62f));
}

static void draw_ground(OdgRaster *raster, const Camera *camera, const OdgPlayerState *p) {
    const int half = 13;
    const float cell = 2.0f;
    int gz;
    int base_x = (int)floorf(p->x / cell);
    int base_z = (int)floorf(p->z / cell);
    for (gz = -half; gz < half; ++gz) {
        int gx;
        for (gx = -half; gx < half; ++gx) {
            float x0 = (float)(base_x + gx) * cell;
            float z0 = (float)(base_z + gz) * cell;
            float x1 = x0 + cell;
            float z1 = z0 + cell;
            uint32_t c = ((base_x + gx + base_z + gz) & 1) == 0 ? UINT32_C(0x5f865cff) : UINT32_C(0x678e62ff);
            quad(raster, camera, v3(x0, -0.02f, z0), v3(x1, -0.02f, z0),
                 v3(x1, -0.02f, z1), v3(x0, -0.02f, z1), from_rrggbbaa(c));
        }
    }
}

static void draw_world_boxes(OdgRaster *raster, const Camera *camera) {
    uint32_t count = 0u;
    const OdgWorldBox *boxes = odg_world_boxes(&count);
    uint32_t i;
    for (i = 0u; i < count; ++i) {
        V3 center = v3((boxes[i].min.x + boxes[i].max.x) * 0.5f,
                       (boxes[i].min.y + boxes[i].max.y) * 0.5f,
                       (boxes[i].min.z + boxes[i].max.z) * 0.5f);
        V3 half = v3((boxes[i].max.x - boxes[i].min.x) * 0.5f,
                     (boxes[i].max.y - boxes[i].min.y) * 0.5f,
                     (boxes[i].max.z - boxes[i].min.z) * 0.5f);
        box(raster, camera, center, half, 0.0f, boxes[i].color_rgba);
    }
}

static void draw_player(OdgRaster *raster, const Camera *camera, const OdgPlayerState *p) {
    float speed = hypotf(p->vx, p->vz);
    float phase = p->gait_distance * 3.3f;
    float swing = speed > 0.15f && p->grounded != 0u ? sinf(phase) * 0.13f : 0.0f;
    float yaw = p->facing_yaw;
    V3 base = v3(p->x, p->y, p->z);
    V3 right = v3(cosf(yaw), 0.0f, -sinf(yaw));
    V3 forward = v3(sinf(yaw), 0.0f, cosf(yaw));
    uint32_t body = UINT32_C(0xd9d6c8ff);
    uint32_t accent = UINT32_C(0x355f7cff);
    box(raster, camera, vadd(base, v3(0.0f, 1.28f, 0.0f)), v3(0.30f, 0.42f, 0.18f), yaw, accent);
    box(raster, camera, vadd(base, v3(0.0f, 1.93f, 0.0f)), v3(0.24f, 0.25f, 0.23f), yaw, body);
    box(raster, camera, vadd(vadd(base, vmul(right, -0.43f)), vadd(v3(0.0f, 1.30f + swing, 0.0f), vmul(forward, swing))),
        v3(0.10f, 0.38f, 0.10f), yaw, body);
    box(raster, camera, vadd(vadd(base, vmul(right, 0.43f)), vadd(v3(0.0f, 1.30f - swing, 0.0f), vmul(forward, -swing))),
        v3(0.10f, 0.38f, 0.10f), yaw, body);
    box(raster, camera, vadd(vadd(base, vmul(right, -0.16f)), vadd(v3(0.0f, 0.55f - swing, 0.0f), vmul(forward, -swing))),
        v3(0.12f, 0.45f, 0.13f), yaw, UINT32_C(0x39444fff));
    box(raster, camera, vadd(vadd(base, vmul(right, 0.16f)), vadd(v3(0.0f, 0.55f + swing, 0.0f), vmul(forward, swing))),
        v3(0.12f, 0.45f, 0.13f), yaw, UINT32_C(0x39444fff));
    box(raster, camera, vadd(vadd(base, v3(0.0f, 1.95f, 0.0f)), vmul(forward, 0.26f)),
        v3(0.055f, 0.055f, 0.055f), yaw, UINT32_C(0x283746ff));
}

void odg_raster_render(OdgRaster *raster, const OdgRenderSnapshot *snapshot) {
    Camera camera;
    if (raster == NULL || snapshot == NULL || raster->pixels == NULL || raster->depth == NULL) return;
    clear_frame(raster);
    camera = make_camera(raster, &snapshot->player);
    draw_ground(raster, &camera, &snapshot->player);
    draw_world_boxes(raster, &camera);
    draw_player(raster, &camera, &snapshot->player);
}

uint64_t odg_raster_hash(const OdgRaster *raster) {
    uint64_t h = UINT64_C(1469598103934665603);
    size_t count;
    size_t i;
    if (raster == NULL || raster->pixels == NULL) return 0u;
    count = (size_t)raster->width * (size_t)raster->height;
    for (i = 0u; i < count; ++i) {
        h ^= (uint64_t)raster->pixels[i];
        h *= UINT64_C(1099511628211);
    }
    return h;
}
