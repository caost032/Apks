#include "odwd_render.h"

#include "odwd_core.h"
#include "odwd_simple.h"
#include "odwd_billboard_textures.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define OD_RENDER_MAX_WIDTH 800u
#define OD_RENDER_MAX_HEIGHT 800u
/* Landscape 800x450 and portrait 450x800 use the same fixed arena. */
#define OD_RENDER_MAX_PIXELS UINT32_C(360000)
#define OD_RENDER_MAX_ROAD 288u
#define OD_RENDER_MAX_SKIDS 384u
#define OD_RENDER_MAX_PARTICLES 96u
#define OD_PI 3.14159265358979323846f
#define OD_NEAR_PLANE 0.08f
#define OD_BRANCH_MIN_HALF_WIDTH 1.12f
#define OD_BRANCH_DRAW_EPSILON 0.02f

#define OD_COLOR(r, g, b) \
    ((uint32_t)(r) | ((uint32_t)(g) << 8u) | \
     ((uint32_t)(b) << 16u) | UINT32_C(0xff000000))

static const uint32_t COL_PAPER = OD_COLOR(244, 244, 240);
static const uint32_t COL_BRIGHT = OD_COLOR(252, 252, 249);
static const uint32_t COL_ROAD = OD_COLOR(233, 233, 228);
static const uint32_t COL_SHOULDER = OD_COLOR(242, 242, 237);
static const uint32_t COL_SHELL = OD_COLOR(250, 250, 247);
static const uint32_t COL_MID = OD_COLOR(236, 236, 231);
static const uint32_t COL_SHADOW = OD_COLOR(216, 216, 209);
static const uint32_t COL_INK = OD_COLOR(11, 12, 14);
static const uint32_t COL_TIRE = OD_COLOR(16, 17, 19);
static const uint32_t COL_FOG_INK = OD_COLOR(119, 120, 115);
/* Selective accents keep WhiteLine's ink/paper identity while making gameplay
 * geometry readable at a glance. They are world materials, not screen FX. */
static const uint32_t COL_ARENA = OD_COLOR(48, 54, 62);
static const uint32_t COL_ARENA_ALT = OD_COLOR(61, 68, 78);
static const uint32_t COL_OBSTACLE = OD_COLOR(176, 184, 194);
static const uint32_t COL_OBSTACLE_ALT = OD_COLOR(121, 132, 145);
static const uint32_t COL_WARNING = OD_COLOR(244, 177, 48);
static const uint32_t COL_LAVA = OD_COLOR(224, 64, 24);
static const uint32_t COL_LAVA_HOT = OD_COLOR(255, 177, 43);
static const uint32_t COL_SAFE = OD_COLOR(62, 150, 137);
static const uint32_t COL_ELECTRIC = OD_COLOR(70, 157, 238);
static const uint32_t COL_METEOR = OD_COLOR(104, 57, 43);
static const uint32_t COL_SHOCK = OD_COLOR(151, 103, 219);
static const uint32_t COL_CHALLENGE_SKY = OD_COLOR(207, 215, 223);

typedef struct od_v3 {
    float x;
    float y;
    float z;
} od_v3;

typedef struct od_screen_vertex {
    float x;
    float y;
    float inverse_z;
    float camera_z;
} od_screen_vertex;

typedef struct od_view {
    od_v3 position;
    od_v3 forward;
    od_v3 right;
    od_v3 up;
    float focal;
    float roll;
} od_view;

typedef struct od_render_road {
    od_v3 center;
    od_v3 alternate;
    od_v3 tangent;
    od_v3 alternate_tangent;
    float half_width;
    float alternate_half_width;
    float progress;
    float curvature;
    int64_t global_index;
    uint32_t flags;
} od_render_road;

typedef struct od_skid {
    od_v3 a;
    od_v3 b;
    float life;
} od_skid;

typedef struct od_particle {
    od_v3 position;
    od_v3 velocity;
    float life;
    float total_life;
} od_particle;

static uint32_t g_framebuffer[OD_RENDER_MAX_PIXELS];
static float g_depth[OD_RENDER_MAX_PIXELS];
static uint32_t g_width = 640u;
static uint32_t g_height = 360u;
static uint32_t g_portrait;
static od_view g_view;
static od_render_road g_road[OD_RENDER_MAX_ROAD];
static uint32_t g_road_count;
typedef struct od_fx_context {
    od_skid skids[OD_RENDER_MAX_SKIDS];
    od_particle particles[OD_RENDER_MAX_PARTICLES];
    uint32_t skid_cursor;
    uint32_t particle_cursor;
    uint64_t last_tick;
    uint64_t last_beat_tick;
    int last_rear_valid;
    od_v3 last_rear_left;
    od_v3 last_rear_right;
    double previous_origin_x;
    double previous_origin_y;
    double previous_origin_z;
} od_fx_context;

static od_fx_context g_fx_contexts[3];
static od_fx_context *g_fx = &g_fx_contexts[0];
#define g_skids (g_fx->skids)
#define g_particles (g_fx->particles)
#define g_skid_cursor (g_fx->skid_cursor)
#define g_particle_cursor (g_fx->particle_cursor)
#define g_last_tick (g_fx->last_tick)
#define g_last_beat_tick (g_fx->last_beat_tick)
#define g_last_rear_valid (g_fx->last_rear_valid)
#define g_last_rear_left (g_fx->last_rear_left)
#define g_last_rear_right (g_fx->last_rear_right)
#define g_previous_origin_x (g_fx->previous_origin_x)
#define g_previous_origin_y (g_fx->previous_origin_y)
#define g_previous_origin_z (g_fx->previous_origin_z)
static double g_origin_x;
static double g_origin_y;
static double g_origin_z;

static float od_clampf(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}

static float od_absf(float value) { return value < 0.0f ? -value : value; }

static int64_t od_floor_i64_signed(double value) {
    int64_t integer = (int64_t)value;
    if ((double)integer > value) --integer;
    return integer;
}

static od_v3 od_v3_make(float x, float y, float z) {
    od_v3 result;
    result.x = x;
    result.y = y;
    result.z = z;
    return result;
}

static od_v3 od_world_v3(double x, double y, double z) {
    return od_v3_make((float)(x - g_origin_x),
                      (float)(y - g_origin_y),
                      (float)(z - g_origin_z));
}

static od_v3 od_v3_add(od_v3 a, od_v3 b) {
    return od_v3_make(a.x + b.x, a.y + b.y, a.z + b.z);
}

static od_v3 od_v3_sub(od_v3 a, od_v3 b) {
    return od_v3_make(a.x - b.x, a.y - b.y, a.z - b.z);
}

static od_v3 od_v3_scale(od_v3 value, float scale) {
    return od_v3_make(value.x * scale, value.y * scale, value.z * scale);
}

static float od_v3_dot(od_v3 a, od_v3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static od_v3 od_v3_cross(od_v3 a, od_v3 b) {
    return od_v3_make(a.y * b.z - a.z * b.y,
                      a.z * b.x - a.x * b.z,
                      a.x * b.y - a.y * b.x);
}

static float od_v3_length(od_v3 value) {
    return sqrtf(od_v3_dot(value, value));
}

static od_v3 od_v3_normalize(od_v3 value) {
    float length = od_v3_length(value);
    if (length < 1.0e-6f) return od_v3_make(0.0f, 0.0f, 1.0f);
    return od_v3_scale(value, 1.0f / length);
}

static uint32_t od_hash32(uint32_t value) {
    value ^= value >> 16u;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15u;
    value *= UINT32_C(0x846ca68b);
    value ^= value >> 16u;
    return value;
}

static float od_hash01(uint32_t value) {
    return (float)(od_hash32(value) & UINT32_C(0x00ffffff)) / 16777215.0f;
}

static uint32_t od_mix_color(uint32_t a, uint32_t b, float amount) {
    uint32_t ar = a & 255u;
    uint32_t ag = (a >> 8u) & 255u;
    uint32_t ab = (a >> 16u) & 255u;
    uint32_t br = b & 255u;
    uint32_t bg = (b >> 8u) & 255u;
    uint32_t bb = (b >> 16u) & 255u;
    uint32_t rr;
    uint32_t rg;
    uint32_t rb;
    amount = od_clampf(amount, 0.0f, 1.0f);
    rr = (uint32_t)((float)ar + ((float)br - (float)ar) * amount);
    rg = (uint32_t)((float)ag + ((float)bg - (float)ag) * amount);
    rb = (uint32_t)((float)ab + ((float)bb - (float)ab) * amount);
    return OD_COLOR(rr, rg, rb);
}

static uint32_t od_fog_color(uint32_t color, float camera_z) {
    float fog = od_clampf((camera_z - 620.0f) / 540.0f, 0.0f, 1.0f);
    return od_mix_color(color, COL_PAPER, fog);
}

static void od_set_quality_dimensions(uint32_t quality) {
    uint32_t width;
    uint32_t height;
    if (quality == 0u) {
        width = 480u;
        height = 270u;
    } else if (quality == 2u) {
        width = 800u;
        height = 450u;
    } else {
        width = 640u;
        height = 360u;
    }
    g_width = g_portrait ? height : width;
    g_height = g_portrait ? width : height;
}

static void od_clear_frame_color(uint32_t color) {
    uint32_t count = g_width * g_height;
    uint32_t index;
    for (index = 0u; index < count; ++index) g_framebuffer[index] = color;
    memset(g_depth, 0, (size_t)count * sizeof(g_depth[0]));
}

static void od_clear_frame(void) {
    od_clear_frame_color(COL_PAPER);
}

static int od_project(od_v3 point, od_screen_vertex *out) {
    od_v3 relative = od_v3_sub(point, g_view.position);
    float x = od_v3_dot(relative, g_view.right);
    float y = od_v3_dot(relative, g_view.up);
    float z = od_v3_dot(relative, g_view.forward);
    if (z < OD_NEAR_PLANE) return 0;
    out->x = (float)g_width * 0.5f + x * g_view.focal / z;
    out->y = (float)g_height * 0.5f - y * g_view.focal / z;
    out->inverse_z = 1.0f / z;
    out->camera_z = z;
    return 1;
}

static float od_camera_depth(od_v3 point) {
    return od_v3_dot(od_v3_sub(point, g_view.position), g_view.forward);
}

static od_v3 od_near_intersection(od_v3 a, od_v3 b,
                                  float depth_a, float depth_b) {
    float denominator = depth_b - depth_a;
    const float clipped_depth = OD_NEAR_PLANE + 1.0e-5f;
    float amount = od_absf(denominator) < 1.0e-8f ? 0.0f :
                   (clipped_depth - depth_a) / denominator;
    amount = od_clampf(amount, 0.0f, 1.0f);
    return od_v3_add(a, od_v3_scale(od_v3_sub(b, a), amount));
}

static float od_edge(float ax, float ay, float bx, float by,
                     float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void od_put_depth_pixel(int x, int y, float inverse_z,
                               uint32_t color, int radius) {
    int dx;
    int dy;
    for (dy = -radius; dy <= radius; ++dy) {
        int py = y + dy;
        if (py < 0 || py >= (int)g_height) continue;
        for (dx = -radius; dx <= radius; ++dx) {
            int px = x + dx;
            uint32_t index;
            if (px < 0 || px >= (int)g_width) continue;
            index = (uint32_t)py * g_width + (uint32_t)px;
            if (inverse_z > g_depth[index]) {
                g_depth[index] = inverse_z;
                g_framebuffer[index] = color;
            }
        }
    }
}

static void od_draw_triangle_screen(od_screen_vertex a,
                                    od_screen_vertex b,
                                    od_screen_vertex c,
                                    uint32_t color) {
    float area = od_edge(a.x, a.y, b.x, b.y, c.x, c.y);
    float min_xf;
    float max_xf;
    float min_yf;
    float max_yf;
    int min_x;
    int max_x;
    int min_y;
    int max_y;
    int x;
    int y;
    if (od_absf(area) < 0.02f) return;
    min_xf = fminf(a.x, fminf(b.x, c.x));
    max_xf = fmaxf(a.x, fmaxf(b.x, c.x));
    min_yf = fminf(a.y, fminf(b.y, c.y));
    max_yf = fmaxf(a.y, fmaxf(b.y, c.y));
    if (max_xf < 0.0f || max_yf < 0.0f ||
        min_xf >= (float)g_width || min_yf >= (float)g_height) return;
    min_x = (int)floorf(fmaxf(0.0f, min_xf));
    max_x = (int)ceilf(fminf((float)g_width - 1.0f, max_xf));
    min_y = (int)floorf(fmaxf(0.0f, min_yf));
    max_y = (int)ceilf(fminf((float)g_height - 1.0f, max_yf));
    for (y = min_y; y <= max_y; ++y) {
        for (x = min_x; x <= max_x; ++x) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float w0 = od_edge(b.x, b.y, c.x, c.y, px, py) / area;
            float w1 = od_edge(c.x, c.y, a.x, a.y, px, py) / area;
            float w2 = 1.0f - w0 - w1;
            uint32_t index;
            float inverse_z;
            if (w0 < -0.0001f || w1 < -0.0001f || w2 < -0.0001f) continue;
            inverse_z = w0 * a.inverse_z + w1 * b.inverse_z + w2 * c.inverse_z;
            index = (uint32_t)y * g_width + (uint32_t)x;
            if (inverse_z > g_depth[index]) {
                g_depth[index] = inverse_z;
                g_framebuffer[index] = od_fog_color(
                    color, inverse_z > 1.0e-9f ? 1.0f / inverse_z : 2000.0f);
            }
        }
    }
}

static void od_draw_triangle(od_v3 a, od_v3 b, od_v3 c, uint32_t color) {
    od_v3 input[4];
    od_v3 clipped[4];
    uint32_t input_count = 3u;
    uint32_t clipped_count = 0u;
    uint32_t index;
    input[0] = a;
    input[1] = b;
    input[2] = c;
    for (index = 0u; index < input_count; ++index) {
        od_v3 current = input[index];
        od_v3 previous = input[(index + input_count - 1u) % input_count];
        float current_depth = od_camera_depth(current);
        float previous_depth = od_camera_depth(previous);
        int current_inside = current_depth >= OD_NEAR_PLANE;
        int previous_inside = previous_depth >= OD_NEAR_PLANE;
        if (current_inside != previous_inside)
            clipped[clipped_count++] = od_near_intersection(
                previous, current, previous_depth, current_depth);
        if (current_inside) clipped[clipped_count++] = current;
    }
    if (clipped_count >= 3u) {
        od_screen_vertex first;
        if (!od_project(clipped[0], &first)) return;
        for (index = 1u; index + 1u < clipped_count; ++index) {
            od_screen_vertex second;
            od_screen_vertex third;
            if (od_project(clipped[index], &second) &&
                od_project(clipped[index + 1u], &third))
                od_draw_triangle_screen(first, second, third, color);
        }
    }
}

static void od_draw_quad(od_v3 a, od_v3 b, od_v3 c, od_v3 d,
                         uint32_t color) {
    od_draw_triangle(a, b, c, color);
    od_draw_triangle(a, c, d, color);
}

typedef struct od_uv {
    float u;
    float v;
} od_uv;

static uint32_t od_rgb565_color(uint16_t packed) {
    uint32_t r5 = (uint32_t)((packed >> 11u) & 31u);
    uint32_t g6 = (uint32_t)((packed >> 5u) & 63u);
    uint32_t b5 = (uint32_t)(packed & 31u);
    uint32_t r = (r5 << 3u) | (r5 >> 2u);
    uint32_t g = (g6 << 2u) | (g6 >> 4u);
    uint32_t b = (b5 << 3u) | (b5 >> 2u);
    return OD_COLOR(r, g, b);
}

static void od_draw_textured_triangle_screen(od_screen_vertex a,
                                             od_screen_vertex b,
                                             od_screen_vertex c,
                                             od_uv ta, od_uv tb, od_uv tc,
                                             const uint16_t *texture) {
    float area = od_edge(a.x, a.y, b.x, b.y, c.x, c.y);
    float min_xf;
    float max_xf;
    float min_yf;
    float max_yf;
    int min_x;
    int max_x;
    int min_y;
    int max_y;
    int x;
    int y;
    if (!texture || od_absf(area) < 0.02f) return;
    min_xf = fminf(a.x, fminf(b.x, c.x));
    max_xf = fmaxf(a.x, fmaxf(b.x, c.x));
    min_yf = fminf(a.y, fminf(b.y, c.y));
    max_yf = fmaxf(a.y, fmaxf(b.y, c.y));
    if (max_xf < 0.0f || max_yf < 0.0f ||
        min_xf >= (float)g_width || min_yf >= (float)g_height) return;
    min_x = (int)floorf(fmaxf(0.0f, min_xf));
    max_x = (int)ceilf(fminf((float)g_width - 1.0f, max_xf));
    min_y = (int)floorf(fmaxf(0.0f, min_yf));
    max_y = (int)ceilf(fminf((float)g_height - 1.0f, max_yf));
    for (y = min_y; y <= max_y; ++y) {
        for (x = min_x; x <= max_x; ++x) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float w0 = od_edge(b.x, b.y, c.x, c.y, px, py) / area;
            float w1 = od_edge(c.x, c.y, a.x, a.y, px, py) / area;
            float w2 = 1.0f - w0 - w1;
            float inverse_z;
            float u_over_z;
            float v_over_z;
            float u;
            float v;
            uint32_t tx;
            uint32_t ty;
            uint32_t index;
            uint32_t color;
            if (w0 < -0.0001f || w1 < -0.0001f || w2 < -0.0001f) continue;
            inverse_z = w0 * a.inverse_z + w1 * b.inverse_z + w2 * c.inverse_z;
            index = (uint32_t)y * g_width + (uint32_t)x;
            if (inverse_z <= g_depth[index] || inverse_z <= 1.0e-9f) continue;
            u_over_z = w0 * ta.u * a.inverse_z +
                       w1 * tb.u * b.inverse_z +
                       w2 * tc.u * c.inverse_z;
            v_over_z = w0 * ta.v * a.inverse_z +
                       w1 * tb.v * b.inverse_z +
                       w2 * tc.v * c.inverse_z;
            u = od_clampf(u_over_z / inverse_z, 0.0f, 1.0f);
            v = od_clampf(v_over_z / inverse_z, 0.0f, 1.0f);
            tx = (uint32_t)(u * (float)(ODWD_BILLBOARD_TEX_W - 1u) + 0.5f);
            ty = (uint32_t)(v * (float)(ODWD_BILLBOARD_TEX_H - 1u) + 0.5f);
            color = od_rgb565_color(texture[ty * ODWD_BILLBOARD_TEX_W + tx]);
            g_depth[index] = inverse_z;
            g_framebuffer[index] = od_fog_color(color, 1.0f / inverse_z);
        }
    }
}

/* Billboards are deliberately rendered only when all four corners are in
 * front of the near plane. They live beside roads/perimeters, so letting an
 * extremely close sign disappear is preferable to a malformed clipped image. */
static void od_draw_textured_quad(od_v3 a, od_v3 b, od_v3 c, od_v3 d,
                                  const uint16_t *texture) {
    od_screen_vertex sa;
    od_screen_vertex sb;
    od_screen_vertex sc;
    od_screen_vertex sd;
    od_uv uv00 = {0.0f, 1.0f};
    od_uv uv10 = {1.0f, 1.0f};
    od_uv uv11 = {1.0f, 0.0f};
    od_uv uv01 = {0.0f, 0.0f};
    if (od_camera_depth(a) < OD_NEAR_PLANE ||
        od_camera_depth(b) < OD_NEAR_PLANE ||
        od_camera_depth(c) < OD_NEAR_PLANE ||
        od_camera_depth(d) < OD_NEAR_PLANE) return;
    if (!od_project(a, &sa) || !od_project(b, &sb) ||
        !od_project(c, &sc) || !od_project(d, &sd)) return;
    od_draw_textured_triangle_screen(sa, sb, sc, uv00, uv10, uv11, texture);
    od_draw_textured_triangle_screen(sa, sc, sd, uv00, uv11, uv01, texture);
}

static void od_draw_line(od_v3 a, od_v3 b, uint32_t color, int radius) {
    od_screen_vertex sa;
    od_screen_vertex sb;
    float dx;
    float dy;
    int steps;
    int step;
    float depth_a = od_camera_depth(a);
    float depth_b = od_camera_depth(b);
    if (depth_a < OD_NEAR_PLANE && depth_b < OD_NEAR_PLANE) return;
    if (depth_a < OD_NEAR_PLANE) {
        a = od_near_intersection(a, b, depth_a, depth_b);
    } else if (depth_b < OD_NEAR_PLANE) {
        b = od_near_intersection(a, b, depth_a, depth_b);
    }
    if (!od_project(a, &sa) || !od_project(b, &sb)) return;
    dx = sb.x - sa.x;
    dy = sb.y - sa.y;
    steps = (int)ceilf(fmaxf(od_absf(dx), od_absf(dy)));
    if (steps < 1) steps = 1;
    if (steps > 1800) steps = 1800;
    color = od_fog_color(color, (sa.camera_z + sb.camera_z) * 0.5f);
    for (step = 0; step <= steps; ++step) {
        float t = (float)step / (float)steps;
        int x = (int)(sa.x + dx * t);
        int y = (int)(sa.y + dy * t);
        /* Lines already sit a few centimetres above their host surfaces.
         * Keep only a sub-pixel depth nudge: a relative 0.3% bias pulled
         * distant markings metres through hills and road decks. */
        float inverse_z = (sa.inverse_z + (sb.inverse_z - sa.inverse_z) * t) * 1.00008f;
        od_put_depth_pixel(x, y, inverse_z, color, radius);
    }
}

static void od_render_billboard_panel(od_v3 base,
                                      od_v3 right_axis,
                                      od_v3 normal_axis,
                                      float width,
                                      float height,
                                      uint32_t texture_index) {
    od_v3 up = od_v3_make(0.0f, 1.0f, 0.0f);
    od_v3 right = od_v3_normalize(od_v3_make(right_axis.x, 0.0f,
                                              right_axis.z));
    od_v3 normal = od_v3_normalize(od_v3_make(normal_axis.x, 0.0f,
                                               normal_axis.z));
    od_v3 center;
    od_v3 frame_center;
    od_v3 front_center;
    od_v3 back_center;
    od_v3 half_right;
    od_v3 half_up;
    od_v3 frame_right;
    od_v3 frame_up;
    od_v3 a;
    od_v3 b;
    od_v3 c;
    od_v3 d;
    od_v3 post_left;
    od_v3 post_right;
    od_v3 post_left_top;
    od_v3 post_right_top;
    const uint16_t *texture;
    if (texture_index >= ODWD_BILLBOARD_TEX_COUNT) return;
    texture = odwd_billboard_textures[texture_index];
    center = base;
    center.y += height * 0.5f + 2.15f;
    half_right = od_v3_scale(right, width * 0.5f);
    half_up = od_v3_scale(up, height * 0.5f);
    frame_right = od_v3_scale(right, width * 0.5f + 0.24f);
    frame_up = od_v3_scale(up, height * 0.5f + 0.24f);
    frame_center = center;

    /* A thin ink frame belongs to WhiteLine's existing visual language. The
     * supplied artwork itself is untouched; only its display surface is
     * resampled into the software renderer's embedded RGB565 texture. */
    a = od_v3_sub(od_v3_sub(frame_center, frame_right), frame_up);
    b = od_v3_add(od_v3_sub(frame_center, frame_up), frame_right);
    c = od_v3_add(od_v3_add(frame_center, frame_right), frame_up);
    d = od_v3_add(od_v3_sub(frame_center, frame_right), frame_up);
    od_draw_quad(a, b, c, d, COL_INK);

    front_center = od_v3_add(center, od_v3_scale(normal, 0.055f));
    a = od_v3_sub(od_v3_sub(front_center, half_right), half_up);
    b = od_v3_add(od_v3_sub(front_center, half_up), half_right);
    c = od_v3_add(od_v3_add(front_center, half_right), half_up);
    d = od_v3_add(od_v3_sub(front_center, half_right), half_up);
    od_draw_textured_quad(a, b, c, d, texture);

    /* Render the same supplied poster on the reverse side as well. This
     * avoids a billboard turning into a blank black card when approached
     * from an unexpected direction in the open world. */
    back_center = od_v3_sub(center, od_v3_scale(normal, 0.055f));
    a = od_v3_sub(od_v3_add(back_center, half_right), half_up);
    b = od_v3_sub(od_v3_sub(back_center, half_right), half_up);
    c = od_v3_add(od_v3_sub(back_center, half_right), half_up);
    d = od_v3_add(od_v3_add(back_center, half_right), half_up);
    od_draw_textured_quad(a, b, c, d, texture);

    post_left = od_v3_add(base, od_v3_scale(right, -width * 0.31f));
    post_right = od_v3_add(base, od_v3_scale(right, width * 0.31f));
    post_left_top = post_left;
    post_right_top = post_right;
    post_left_top.y = center.y - height * 0.36f;
    post_right_top.y = center.y - height * 0.36f;
    od_draw_line(post_left, post_left_top, COL_INK, 2);
    od_draw_line(post_right, post_right_top, COL_INK, 2);
}

static void od_build_view(const odwd_camera_snapshot *camera) {
    od_v3 world_up = od_v3_make(0.0f, 1.0f, 0.0f);
    float cosine;
    float sine;
    od_v3 base_right;
    od_v3 base_up;
    g_view.position = od_world_v3(camera->position_x,
                                  camera->position_y,
                                  camera->position_z);
    g_view.forward = od_v3_normalize(od_v3_sub(
        od_world_v3(camera->target_x,
                    camera->target_y,
                    camera->target_z),
        g_view.position));
    base_right = od_v3_normalize(od_v3_cross(world_up, g_view.forward));
    base_up = od_v3_normalize(od_v3_cross(g_view.forward, base_right));
    g_view.roll = (float)camera->roll_rad;
    cosine = cosf(g_view.roll);
    sine = sinf(g_view.roll);
    g_view.right = od_v3_add(od_v3_scale(base_right, cosine),
                             od_v3_scale(base_up, sine));
    g_view.up = od_v3_sub(od_v3_scale(base_up, cosine),
                          od_v3_scale(base_right, sine));
    /* A literal vertical-FOV projection becomes a telephoto crop on a tall
     * phone. Use the narrow canvas axis as portrait framing authority so the
     * road, rivals and mountain edges remain readable. */
    g_view.focal = (g_portrait ? (float)g_width * 0.55f
                               : (float)g_height * 0.5f) /
                   tanf((float)camera->vertical_fov_rad * 0.5f);
}

static uint32_t od_load_road(const void *storage) {
    uint32_t count = odwd_engine_road_node_count(storage);
    uint32_t index;
    if (count > OD_RENDER_MAX_ROAD) count = OD_RENDER_MAX_ROAD;
    for (index = 0u; index < count; ++index) {
        odwd_road_node node;
        if (odwd_engine_read_road_node(storage, index, &node) != ODWD_OK) break;
        g_road[index].center = od_world_v3(node.center_x,
                                           node.center_y,
                                           node.center_z);
        g_road[index].alternate = od_world_v3(node.alternate_x,
                                              node.alternate_y,
                                              node.alternate_z);
        g_road[index].tangent = od_v3_normalize(od_v3_make(
            (float)node.tangent_x, (float)node.tangent_y,
            (float)node.tangent_z));
        g_road[index].alternate_tangent = od_v3_normalize(od_v3_make(
            (float)node.alternate_tangent_x,
            (float)node.alternate_tangent_y,
            (float)node.alternate_tangent_z));
        g_road[index].half_width = (float)node.half_width_m;
        g_road[index].alternate_half_width = (float)node.alternate_half_width_m;
        g_road[index].progress = (float)node.progress_m;
        g_road[index].curvature = (float)node.curvature_per_m;
        g_road[index].global_index = node.global_node_index;
        g_road[index].flags = node.flags;
    }
    g_road_count = index;
    return index;
}

static od_v3 od_road_side(const od_render_road *node, float side,
                          float half_width, int alternate, float y_offset) {
    od_v3 center = alternate ? node->alternate : node->center;
    od_v3 tangent = alternate ? node->alternate_tangent : node->tangent;
    od_v3 right = od_v3_normalize(od_v3_make(tangent.z, 0.0f,
                                              -tangent.x));
    center = od_v3_add(center, od_v3_scale(right, side * half_width));
    center.y += y_offset;
    return center;
}

static void od_render_peak(const od_render_road *node, float side,
                           float distance, float width, float height,
                           uint32_t seed, int far_layer) {
    od_v3 right = od_v3_normalize(od_v3_make(node->tangent.z, 0.0f,
                                              -node->tangent.x));
    od_v3 tangent = od_v3_normalize(od_v3_make(node->tangent.x, 0.0f,
                                                node->tangent.z));
    od_v3 center = od_v3_add(node->center, od_v3_scale(right, side * distance));
    od_v3 b0;
    od_v3 b1;
    od_v3 b2;
    od_v3 b3;
    od_v3 peak;
    uint32_t face_a = far_layer ? od_mix_color(COL_SHADOW, COL_PAPER, 0.50f)
                                : COL_SHADOW;
    uint32_t face_b = far_layer ? od_mix_color(COL_MID, COL_PAPER, 0.48f)
                                : COL_MID;
    center.y -= far_layer ? 18.0f : 5.0f;
    b0 = od_v3_add(od_v3_add(center, od_v3_scale(right, -width * 0.62f)),
                   od_v3_scale(tangent, -width * 0.42f));
    b1 = od_v3_add(od_v3_add(center, od_v3_scale(right, width * 0.58f)),
                   od_v3_scale(tangent, -width * 0.34f));
    b2 = od_v3_add(od_v3_add(center, od_v3_scale(right, width * 0.52f)),
                   od_v3_scale(tangent, width * 0.46f));
    b3 = od_v3_add(od_v3_add(center, od_v3_scale(right, -width * 0.54f)),
                   od_v3_scale(tangent, width * 0.50f));
    peak = od_v3_add(center, od_v3_add(
        od_v3_scale(right, (od_hash01(seed) - 0.5f) * width * 0.22f),
        od_v3_scale(tangent, (od_hash01(seed + 9u) - 0.5f) * width * 0.20f)));
    peak.y += height;
    od_draw_triangle(b0, b1, peak, face_a);
    od_draw_triangle(b1, b2, peak, face_b);
    od_draw_triangle(b2, b3, peak, COL_BRIGHT);
    od_draw_triangle(b3, b0, peak, face_b);
    od_draw_line(b0, peak, far_layer ? COL_FOG_INK : COL_INK, 0);
    od_draw_line(peak, b2, far_layer ? COL_FOG_INK : COL_INK, 0);
    if (!far_layer) {
        od_v3 crease = od_v3_add(od_v3_scale(b3, 0.44f),
                                 od_v3_scale(peak, 0.56f));
        od_draw_line(b1, crease, COL_FOG_INK, 0);
    }
}

static void od_render_mountains(uint32_t begin, uint32_t end) {
    uint32_t index;
    for (index = begin; index + 1u < end; ++index) {
        const od_render_road *node = &g_road[index];
        uint32_t seed = (uint32_t)node->global_index;
        if ((uint64_t)node->global_index % UINT64_C(11) != UINT64_C(5))
            continue;
        float compression = (node->flags & (ODWD_ROAD_GORGE |
                                             ODWD_ROAD_RIDGE |
                                             ODWD_ROAD_HAIRPIN)) ? 0.78f : 1.0f;
        od_render_peak(node, -1.0f, 72.0f * compression,
                       42.0f, 31.0f + od_hash01(seed + 2u) * 32.0f,
                       seed ^ UINT32_C(0x2c1b3c6d), 0);
        od_render_peak(node, 1.0f, 82.0f * compression,
                       48.0f, 38.0f + od_hash01(seed + 7u) * 36.0f,
                       seed ^ UINT32_C(0x91e10da5), 0);
        if (((uint64_t)node->global_index / UINT64_C(11)) % UINT64_C(2) ==
            UINT64_C(0)) {
            od_render_peak(node, (seed & 1u) ? -1.0f : 1.0f, 205.0f,
                           125.0f, 92.0f + od_hash01(seed + 13u) * 80.0f,
                           seed ^ UINT32_C(0x7f4a7c15), 1);
        }
    }
}

static void od_draw_ribbon_segment(const od_render_road *a,
                                   const od_render_road *b,
                                   float extra_width_a,
                                   float extra_width_b, int alternate,
                                   float y_offset, uint32_t color) {
    float width_a = (alternate ? a->alternate_half_width : a->half_width) +
                    extra_width_a;
    float width_b = (alternate ? b->alternate_half_width : b->half_width) +
                    extra_width_b;
    od_v3 left_a = od_road_side(a, -1.0f, width_a, alternate, y_offset);
    od_v3 right_a = od_road_side(a, 1.0f, width_a, alternate, y_offset);
    od_v3 left_b = od_road_side(b, -1.0f, width_b, alternate, y_offset);
    od_v3 right_b = od_road_side(b, 1.0f, width_b, alternate, y_offset);
    od_draw_quad(left_a, left_b, right_b, right_a, color);
}

static float od_main_shoulder_extra(const od_render_road *node) {
    float blend;
    if (!(node->flags & ODWD_ROAD_ALT_ROUTE)) return 2.4f;
    blend = od_clampf((node->alternate_half_width - 0.18f) / 1.27f,
                      0.0f, 1.0f);
    blend = blend * blend * (3.0f - 2.0f * blend);
    return 2.4f + (0.14f - 2.4f) * blend;
}

static float od_alt_shoulder_extra(const od_render_road *node) {
    float blend = od_clampf(node->alternate_half_width /
                            OD_BRANCH_MIN_HALF_WIDTH, 0.0f, 1.0f);
    return 0.14f * blend * blend * (3.0f - 2.0f * blend);
}

static void od_render_road_foundation(const od_render_road *a,
                                      const od_render_road *b,
                                      int alternate) {
    float width_a = alternate ? a->alternate_half_width : a->half_width;
    float width_b = alternate ? b->alternate_half_width : b->half_width;
    od_v3 left_a = od_road_side(a, -1.0f, width_a + 0.12f,
                                alternate, -0.02f);
    od_v3 left_b = od_road_side(b, -1.0f, width_b + 0.12f,
                                alternate, -0.02f);
    od_v3 right_a = od_road_side(a, 1.0f, width_a + 0.12f,
                                 alternate, -0.02f);
    od_v3 right_b = od_road_side(b, 1.0f, width_b + 0.12f,
                                 alternate, -0.02f);
    od_v3 left_floor_a = left_a;
    od_v3 left_floor_b = left_b;
    od_v3 right_floor_a = right_a;
    od_v3 right_floor_b = right_b;
    float depth = ((a->flags | b->flags) & ODWD_ROAD_VIADUCT) ? 1.25f : 0.55f;
    left_floor_a.y -= depth;
    left_floor_b.y -= depth;
    right_floor_a.y -= depth;
    right_floor_b.y -= depth;
    od_draw_quad(left_a, left_floor_a, left_floor_b, left_b, COL_SHADOW);
    od_draw_quad(right_a, right_b, right_floor_b, right_floor_a, COL_MID);
    if (!alternate && !((a->flags | b->flags) & ODWD_ROAD_VIADUCT)) {
        od_v3 outer_left_a = od_road_side(a, -1.0f, width_a + 13.0f,
                                          0, -2.8f);
        od_v3 outer_left_b = od_road_side(b, -1.0f, width_b + 13.0f,
                                          0, -2.8f);
        od_v3 outer_right_a = od_road_side(a, 1.0f, width_a + 13.0f,
                                           0, -2.8f);
        od_v3 outer_right_b = od_road_side(b, 1.0f, width_b + 13.0f,
                                           0, -2.8f);
        od_draw_quad(left_floor_a, outer_left_a, outer_left_b,
                     left_floor_b, COL_MID);
        od_draw_quad(right_floor_a, right_floor_b, outer_right_b,
                     outer_right_a, COL_SHOULDER);
    }
}

static void od_render_gate(const od_render_road *node, int alternate) {
    float half_width = (alternate ? node->alternate_half_width :
                        node->half_width) + 1.1f;
    od_v3 left_base = od_road_side(node, -1.0f, half_width, alternate, 0.10f);
    od_v3 right_base = od_road_side(node, 1.0f, half_width, alternate, 0.10f);
    od_v3 left_top = left_base;
    od_v3 right_top = right_base;
    left_top.y += 4.0f;
    right_top.y += 4.0f;
    od_draw_line(left_base, left_top, COL_INK, 1);
    od_draw_line(right_base, right_top, COL_INK, 1);
    od_draw_line(left_top, right_top, COL_INK, 0);
    {
        od_v3 notch_a = od_v3_add(left_top, od_v3_scale(
            od_v3_sub(right_top, left_top), 0.45f));
        od_v3 notch_b = od_v3_add(left_top, od_v3_scale(
            od_v3_sub(right_top, left_top), 0.55f));
        notch_a.y -= 0.45f;
        notch_b.y -= 0.45f;
        od_draw_line(notch_a, notch_b, COL_INK, 0);
    }
}

static void od_render_guardrail(const od_render_road *a,
                                const od_render_road *b,
                                float side) {
    od_v3 base_a = od_road_side(a, side, a->half_width + 1.9f, 0, 0.1f);
    od_v3 base_b = od_road_side(b, side, b->half_width + 1.9f, 0, 0.1f);
    od_v3 top_a = base_a;
    od_v3 top_b = base_b;
    top_a.y += 0.82f;
    top_b.y += 0.82f;
    od_draw_line(base_a, top_a, COL_INK, 0);
    od_draw_line(top_a, top_b, COL_INK, 0);
}

static void od_render_track(uint32_t begin, uint32_t end) {
    uint32_t index;
    for (index = begin; index + 1u < end; ++index) {
        const od_render_road *a = &g_road[index];
        const od_render_road *b = &g_road[index + 1u];
        int branch = (a->flags & ODWD_ROAD_ALT_ROUTE) &&
                     (b->flags & ODWD_ROAD_ALT_ROUTE) &&
                     fmaxf(a->alternate_half_width,
                           b->alternate_half_width) >
                           OD_BRANCH_DRAW_EPSILON;
        float shoulder_a = od_main_shoulder_extra(a);
        float shoulder_b = od_main_shoulder_extra(b);
        if ((a->flags | b->flags) & ODWD_ROAD_GAP) continue;
        od_render_road_foundation(a, b, 0);
        /* Each endpoint owns its shoulder width. Adjacent segments therefore
         * share exactly the same split/merge edge even when authored widths
         * jump between 12 m nodes. */
        od_draw_ribbon_segment(a, b, shoulder_a, shoulder_b,
                               0, 0.01f, COL_SHOULDER);
        od_draw_ribbon_segment(a, b, 0.0f, 0.0f,
                               0, 0.055f, COL_ROAD);
        if (branch) {
            float alt_shoulder_a = od_alt_shoulder_extra(a);
            float alt_shoulder_b = od_alt_shoulder_extra(b);
            /* The tapered connector is a thin road union. Once both ends
             * are fully separated, restore side walls even for a level
             * branch so it never reads as a floating sheet. */
            if (fminf(a->alternate_half_width,
                      b->alternate_half_width) > 2.40f)
                od_render_road_foundation(a, b, 1);
            od_draw_ribbon_segment(a, b, alt_shoulder_a, alt_shoulder_b,
                                   1, 0.02f, COL_SHOULDER);
            od_draw_ribbon_segment(a, b, 0.0f, 0.0f,
                                   1, 0.065f, COL_ROAD);
        }
    }
    for (index = begin; index + 1u < end; ++index) {
        const od_render_road *a = &g_road[index];
        const od_render_road *b = &g_road[index + 1u];
        od_v3 left_a = od_road_side(a, -1.0f, a->half_width, 0, 0.085f);
        od_v3 right_a = od_road_side(a, 1.0f, a->half_width, 0, 0.085f);
        od_v3 left_b = od_road_side(b, -1.0f, b->half_width, 0, 0.085f);
        od_v3 right_b = od_road_side(b, 1.0f, b->half_width, 0, 0.085f);
        int line_radius = 0;
        if ((a->flags | b->flags) & ODWD_ROAD_GAP) continue;
        od_draw_line(left_a, left_b, COL_INK, line_radius);
        od_draw_line(right_a, right_b, COL_INK, line_radius);
        if (((uint64_t)a->global_index & UINT64_C(3)) < UINT64_C(2)) {
            od_v3 center_a = a->center;
            od_v3 center_b = b->center;
            center_a.y += 0.095f;
            center_b.y += 0.095f;
            od_draw_line(center_a, center_b, COL_FOG_INK, 0);
        }
        if ((a->flags & ODWD_ROAD_ALT_ROUTE) &&
            (b->flags & ODWD_ROAD_ALT_ROUTE) &&
            fmaxf(a->alternate_half_width,
                  b->alternate_half_width) > OD_BRANCH_DRAW_EPSILON) {
            od_v3 alt_left_a = od_road_side(a, -1.0f,
                a->alternate_half_width, 1, 0.095f);
            od_v3 alt_right_a = od_road_side(a, 1.0f,
                a->alternate_half_width, 1, 0.095f);
            od_v3 alt_left_b = od_road_side(b, -1.0f,
                b->alternate_half_width, 1, 0.095f);
            od_v3 alt_right_b = od_road_side(b, 1.0f,
                b->alternate_half_width, 1, 0.095f);
            od_draw_line(alt_left_a, alt_left_b, COL_INK, line_radius);
            od_draw_line(alt_right_a, alt_right_b, COL_INK, line_radius);
        }
        if ((a->flags & (ODWD_ROAD_GORGE | ODWD_ROAD_RIDGE |
                         ODWD_ROAD_HAIRPIN)) && (index & 1u) == 0u) {
            od_render_guardrail(a, b, -1.0f);
            od_render_guardrail(a, b, 1.0f);
        }
        if ((a->flags & ODWD_ROAD_TUNNEL) &&
            ((uint64_t)a->global_index % UINT64_C(4)) == 0u)
            od_render_gate(a, 0);
        if ((a->flags & ODWD_ROAD_VIADUCT) &&
            ((uint64_t)a->global_index % UINT64_C(3)) == 0u) {
            od_v3 left = od_road_side(a, -1.0f, a->half_width + 1.4f,
                                      0, -0.05f);
            od_v3 right = od_road_side(a, 1.0f, a->half_width + 1.4f,
                                       0, -0.05f);
            od_v3 left_floor = left;
            od_v3 right_floor = right;
            left_floor.y -= 12.0f;
            right_floor.y -= 12.0f;
            od_draw_line(left, left_floor, COL_INK, 1);
            od_draw_line(right, right_floor, COL_INK, 1);
            od_draw_line(left_floor, right_floor, COL_FOG_INK, 0);
        }
        if ((a->flags & ODWD_ROAD_RAMP) &&
            ((uint64_t)a->global_index % UINT64_C(3)) == 0u)
            od_render_gate(a, 0);
        if (a->global_index > 0 &&
            (a->global_index % INT64_C(40)) == INT64_C(0))
            od_render_gate(a, 0);
    }
}

static void od_render_track_billboards(uint32_t begin, uint32_t end) {
    uint32_t index;
    for (index = begin; index < end; ++index) {
        const od_render_road *node = &g_road[index];
        uint32_t seed;
        od_v3 tangent;
        od_v3 road_right;
        uint32_t side_index;
        if ((uint64_t)node->global_index % UINT64_C(8) != UINT64_C(4))
            continue;
        if (node->flags & (ODWD_ROAD_GAP | ODWD_ROAD_TUNNEL)) continue;
        seed = od_hash32((uint32_t)node->global_index ^ UINT32_C(0x574c4436));
        tangent = od_v3_normalize(od_v3_make(node->tangent.x, 0.0f,
                                              node->tangent.z));
        road_right = od_v3_normalize(od_v3_make(node->tangent.z, 0.0f,
                                                 -node->tangent.x));
        for (side_index = 0u; side_index < 2u; ++side_index) {
            float side = side_index == 0u ? -1.0f : 1.0f;
            od_v3 normal = od_v3_scale(road_right, -side);
            od_v3 base = od_road_side(node, side,
                                      node->half_width + 8.6f,
                                      0, 0.05f);
            od_render_billboard_panel(base, tangent, normal,
                                      8.4f, 8.4f,
                                      (seed + side_index) %
                                          ODWD_BILLBOARD_TEX_COUNT);
        }
    }
}

static void od_render_open_ground(const void *storage,
                                  const odwd_vehicle_snapshot *player) {
    const double cell_size = ODWD_OPEN_GROUND_CELL_M;
    int64_t center_x = od_floor_i64_signed(player->position_x / cell_size);
    int64_t center_z = od_floor_i64_signed(player->position_z / cell_size);
    int dz;
    for (dz = -17; dz <= 17; ++dz) {
        int dx;
        for (dx = -17; dx <= 17; ++dx) {
            double x0 = (double)(center_x + dx) * cell_size;
            double z0 = (double)(center_z + dz) * cell_size;
            double x1 = x0 + cell_size;
            double z1 = z0 + cell_size;
            od_v3 a = od_world_v3(x0,
                odwd_engine_base_ground_height(storage, x0, z0) - 0.04, z0);
            od_v3 b = od_world_v3(x0,
                odwd_engine_base_ground_height(storage, x0, z1) - 0.04, z1);
            od_v3 c = od_world_v3(x1,
                odwd_engine_base_ground_height(storage, x1, z1) - 0.04, z1);
            od_v3 d = od_world_v3(x1,
                odwd_engine_base_ground_height(storage, x1, z0) - 0.04, z0);
            float average_y = (a.y + b.y + c.y + d.y) * 0.25f;
            uint32_t cell_hash = od_hash32((uint32_t)(center_x + dx) * 977u ^
                                           (uint32_t)(center_z + dz) * 6151u);
            float relief = od_clampf((average_y + 4.0f) / 72.0f, 0.0f, 1.0f);
            float variation = ((float)(cell_hash & 255u) / 255.0f - 0.5f) * 0.08f;
            uint32_t color = od_mix_color(COL_SHOULDER, COL_BRIGHT,
                                          od_clampf(0.24f + relief * 0.28f +
                                                    variation, 0.08f, 0.62f));
            od_draw_quad(a, b, c, d, color);
        }
    }
}

static void od_render_open_billboards(const void *storage,
                                      const odwd_vehicle_snapshot *player) {
    const double cell = 118.0;
    int64_t center_x = od_floor_i64_signed(player->position_x / cell);
    int64_t center_z = od_floor_i64_signed(player->position_z / cell);
    int dz;
    for (dz = -3; dz <= 3; ++dz) {
        int dx;
        for (dx = -3; dx <= 3; ++dx) {
            int64_t gx = center_x + dx;
            int64_t gz = center_z + dz;
            uint32_t seed = od_hash32((uint32_t)gx * UINT32_C(0x9e3779b9) ^
                                      (uint32_t)gz * UINT32_C(0x85ebca6b));
            double x;
            double z;
            double y;
            od_v3 right;
            od_v3 normal;
            od_v3 base;
            if ((seed % 7u) > 2u) continue;
            x = ((double)gx + 0.5) * cell +
                ((double)((seed >> 8u) & 255u) / 255.0 - 0.5) * 46.0;
            z = ((double)gz + 0.5) * cell +
                ((double)((seed >> 16u) & 255u) / 255.0 - 0.5) * 46.0;
            y = odwd_engine_ground_height(storage, x, z) + 0.06;
            /* Open-world posters form a loose branded ring and face broadly
             * toward the central play region. Random yaw made many perfectly
             * valid signs present edge-on to the driver and look absent. */
            normal = od_v3_normalize(od_v3_make((float)-x, 0.0f,
                                                 (float)-z));
            right = od_v3_make(normal.z, 0.0f, -normal.x);
            base = od_world_v3(x, y, z);
            od_render_billboard_panel(base, right, normal,
                                      8.2f, 8.2f,
                                      (seed >> 3u) % ODWD_BILLBOARD_TEX_COUNT);
        }
    }
}

static void od_render_music_boundary_billboards(const void *storage) {
    uint32_t count = odwd_engine_world_prop_count(storage);
    uint32_t index;
    for (index = 0u; index < count; ++index) {
        odwd_world_prop_snapshot prop;
        uint32_t boundary_index;
        float yaw;
        od_v3 right;
        od_v3 normal;
        od_v3 base;
        if (odwd_engine_read_world_prop(storage, index, &prop) != ODWD_OK ||
            prop.type != ODWD_PROP_MUSIC_BOUNDARY)
            continue;
        boundary_index = prop.prop_id - UINT32_C(0x90500000);
        if ((boundary_index % 7u) != 2u) continue;
        yaw = (float)prop.rotation_rad;
        right = od_v3_make(cosf(yaw), 0.0f, -sinf(yaw));
        normal = od_v3_make(sinf(yaw), 0.0f, cosf(yaw));
        base = od_world_v3(prop.position_x,
                           prop.position_y + prop.extent_y_m + 0.10,
                           prop.position_z);
        od_render_billboard_panel(base, right, normal,
                                  5.8f, 5.8f,
                                  (boundary_index / 7u) %
                                      ODWD_BILLBOARD_TEX_COUNT);
    }
}

static void od_render_survival_billboards(const void *storage) {
    uint32_t count = odwd_engine_world_prop_count(storage);
    uint32_t index;
    int found = 0;
    double min_x = 1.0e30;
    double max_x = -1.0e30;
    double min_z = 1.0e30;
    double max_z = -1.0e30;
    double top_y = -1.0e30;
    od_v3 base;
    for (index = 0u; index < count; ++index) {
        odwd_world_prop_snapshot prop;
        double hx;
        double hz;
        if (odwd_engine_read_world_prop(storage, index, &prop) != ODWD_OK ||
            (prop.type != ODWD_PROP_SURVIVAL_PLATFORM &&
             prop.type != ODWD_PROP_SURVIVAL_SAFE_TILE))
            continue;
        hx = prop.extent_x_m * 0.5;
        hz = prop.extent_z_m * 0.5;
        if (prop.position_x - hx < min_x) min_x = prop.position_x - hx;
        if (prop.position_x + hx > max_x) max_x = prop.position_x + hx;
        if (prop.position_z - hz < min_z) min_z = prop.position_z - hz;
        if (prop.position_z + hz > max_z) max_z = prop.position_z + hz;
        if (prop.position_y + prop.extent_y_m > top_y)
            top_y = prop.position_y + prop.extent_y_m;
        found = 1;
    }
    if (!found) return;

    /* Stadium-style boards sit just outside the computed playable footprint:
     * they improve contrast/identity in BlockDash without becoming collision
     * geometry or hiding a survival obstacle. */
    base = od_world_v3(min_x - 3.0, top_y + 0.12, (min_z + max_z) * 0.5);
    od_render_billboard_panel(base, od_v3_make(0.0f, 0.0f, 1.0f),
                              od_v3_make(1.0f, 0.0f, 0.0f),
                              5.8f, 5.8f, 0u);
    base = od_world_v3(max_x + 3.0, top_y + 0.12, (min_z + max_z) * 0.5);
    od_render_billboard_panel(base, od_v3_make(0.0f, 0.0f, -1.0f),
                              od_v3_make(-1.0f, 0.0f, 0.0f),
                              5.8f, 5.8f, 1u);
    base = od_world_v3((min_x + max_x) * 0.5, top_y + 0.12, min_z - 3.0);
    od_render_billboard_panel(base, od_v3_make(1.0f, 0.0f, 0.0f),
                              od_v3_make(0.0f, 0.0f, 1.0f),
                              5.8f, 5.8f, 2u);
    base = od_world_v3((min_x + max_x) * 0.5, top_y + 0.12, max_z + 3.0);
    od_render_billboard_panel(base, od_v3_make(-1.0f, 0.0f, 0.0f),
                              od_v3_make(0.0f, 0.0f, -1.0f),
                              5.8f, 5.8f, 3u);
}

static void od_render_tree(const odwd_world_prop_snapshot *prop) {
    float scale = (float)prop->scale;
    float yaw = (float)prop->rotation_rad;
    float cosine = cosf(yaw);
    float sine = sinf(yaw);
    uint32_t variant = prop->variant % 5u;
    float trunk_radius = (0.24f + (float)(variant & 1u) * 0.08f) * scale;
    float trunk_height = (3.0f + (float)variant * 0.34f) * scale;
    od_v3 base = od_world_v3(prop->position_x, prop->position_y,
                             prop->position_z);
    od_v3 top = base;
    od_v3 corners[4];
    od_v3 top_corners[4];
    uint32_t index;
    top.y += trunk_height;
    for (index = 0u; index < 4u; ++index) {
        float sx = (index == 0u || index == 3u) ? -1.0f : 1.0f;
        float sz = index < 2u ? -1.0f : 1.0f;
        float lx = sx * trunk_radius;
        float lz = sz * trunk_radius;
        corners[index] = od_v3_make(base.x + lx * cosine + lz * sine,
                                    base.y,
                                    base.z - lx * sine + lz * cosine);
        top_corners[index] = corners[index];
        top_corners[index].y += trunk_height;
    }
    od_draw_quad(corners[0], corners[1], top_corners[1], top_corners[0],
                 COL_SHADOW);
    od_draw_quad(corners[1], corners[2], top_corners[2], top_corners[1],
                 COL_MID);
    od_draw_quad(corners[2], corners[3], top_corners[3], top_corners[2],
                 COL_SHADOW);
    for (index = 0u; index < 2u + (variant % 3u); ++index) {
        float layer_y = trunk_height * (0.54f + (float)index *
                         (variant == 2u ? 0.16f : 0.21f));
        float radius = (2.15f + (float)(variant % 2u) * 0.55f -
                        (float)index * (variant == 2u ? 0.28f : 0.47f)) * scale;
        od_v3 center = base;
        od_v3 apex = base;
        uint32_t side;
        center.y += layer_y;
        apex.y += layer_y + (variant == 1u ? 1.85f :
                              variant == 3u ? 3.25f : 2.55f) * scale;
        for (side = 0u; side < 8u; ++side) {
            float a0 = (float)side / 8.0f * 2.0f * OD_PI + yaw;
            float a1 = (float)(side + 1u) / 8.0f * 2.0f * OD_PI + yaw;
            od_v3 p0 = od_v3_make(center.x + cosf(a0) * radius,
                                   center.y,
                                   center.z + sinf(a0) * radius);
            od_v3 p1 = od_v3_make(center.x + cosf(a1) * radius,
                                   center.y,
                                   center.z + sinf(a1) * radius);
            od_draw_triangle(p0, p1, apex,
                             (side & 1u) ? COL_BRIGHT : COL_MID);
        }
    }
    od_draw_line(base, top, COL_INK, 0);
}

static void od_render_low_prop(const odwd_world_prop_snapshot *prop) {
    float scale = (float)prop->scale;
    od_v3 base = od_world_v3(prop->position_x, prop->position_y,
                             prop->position_z);
    uint32_t variant = prop->variant % 5u;
    uint32_t sides = prop->type == ODWD_PROP_SHRUB ? 6u + variant : 5u + (variant % 4u);
    float radius = (prop->type == ODWD_PROP_SHRUB ?
                    1.18f + (float)variant * 0.16f :
                    1.02f + (float)variant * 0.13f) * scale;
    float height = (prop->type == ODWD_PROP_SHRUB ?
                    0.92f + (float)((variant * 3u) % 5u) * 0.22f :
                    1.20f + (float)((variant * 2u) % 5u) * 0.23f) * scale;
    od_v3 apex = base;
    uint32_t index;
    apex.y += height;
    for (index = 0u; index < sides; ++index) {
        float a0 = (float)index / (float)sides * 2.0f * OD_PI +
                   (float)prop->rotation_rad;
        float a1 = (float)(index + 1u) / (float)sides * 2.0f * OD_PI +
                   (float)prop->rotation_rad;
        od_v3 p0 = od_v3_make(base.x + cosf(a0) * radius,
                               base.y, base.z + sinf(a0) * radius);
        od_v3 p1 = od_v3_make(base.x + cosf(a1) * radius,
                               base.y, base.z + sinf(a1) * radius);
        od_draw_triangle(p0, p1, apex,
                         (index & 1u) ? COL_SHADOW : COL_MID);
        od_draw_line(p0, apex, COL_INK, 0);
    }
}

static void od_render_sculpture(const odwd_world_prop_snapshot *prop) {
    float scale = (float)prop->scale;
    od_v3 base = od_world_v3(prop->position_x, prop->position_y,
                             prop->position_z);
    od_v3 left = base;
    od_v3 right = base;
    od_v3 crown = base;
    left.x -= 1.15f * scale;
    right.x += 1.15f * scale;
    left.y += 0.2f;
    right.y += 0.2f;
    crown.y += (4.6f + (float)(prop->variant % 5u) * 0.48f) * scale;
    crown.x += sinf((float)prop->rotation_rad) *
               (0.75f + (float)(prop->variant % 3u) * 0.38f) * scale;
    crown.z += cosf((float)prop->rotation_rad) *
               ((prop->variant & 1u) ? 0.62f : -0.32f) * scale;
    od_draw_triangle(left, right, crown, COL_MID);
    od_draw_line(left, crown, COL_INK, 1);
    od_draw_line(crown, right, COL_INK, 1);
    od_draw_line(left, right, COL_INK, 0);
}

static od_v3 od_prop_point(const odwd_world_prop_snapshot *prop,
                           float local_x, float local_y, float local_z) {
    float cosine = cosf((float)prop->rotation_rad);
    float sine = sinf((float)prop->rotation_rad);
    return od_world_v3(prop->position_x + local_x * cosine + local_z * sine,
                       prop->position_y + local_y,
                       prop->position_z - local_x * sine + local_z * cosine);
}

static void od_render_prop_box(const odwd_world_prop_snapshot *prop,
                               float cx, float cy, float cz,
                               float hx, float hy, float hz,
                               uint32_t color) {
    od_v3 v[8];
    uint32_t index;
    for (index = 0u; index < 8u; ++index) {
        float x = cx + ((index & 1u) ? hx : -hx);
        float y = cy + ((index & 2u) ? hy : -hy);
        float z = cz + ((index & 4u) ? hz : -hz);
        v[index] = od_prop_point(prop, x, y, z);
    }
    od_draw_quad(v[0], v[1], v[3], v[2], color);
    od_draw_quad(v[4], v[6], v[7], v[5], color);
    od_draw_quad(v[0], v[4], v[5], v[1], COL_SHADOW);
    od_draw_quad(v[2], v[3], v[7], v[6], COL_BRIGHT);
    od_draw_quad(v[0], v[2], v[6], v[4], COL_MID);
    od_draw_quad(v[1], v[5], v[7], v[3], color);
}

static void od_render_house(const odwd_world_prop_snapshot *prop) {
    float scale = (float)prop->scale;
    float width = 4.5f * scale;
    float depth = 6.2f * scale;
    float wall = 3.45f * scale;
    float roof = 1.75f * scale;
    od_v3 front_left;
    od_v3 front_right;
    od_v3 back_left;
    od_v3 back_right;
    od_v3 front_ridge;
    od_v3 back_ridge;
    od_render_prop_box(prop, 0.0f, wall * 0.5f, 0.0f,
                       width * 0.5f, wall * 0.5f, depth * 0.5f,
                       COL_SHELL);
    front_left = od_prop_point(prop, -width * 0.58f, wall, depth * 0.56f);
    front_right = od_prop_point(prop, width * 0.58f, wall, depth * 0.56f);
    back_left = od_prop_point(prop, -width * 0.58f, wall, -depth * 0.56f);
    back_right = od_prop_point(prop, width * 0.58f, wall, -depth * 0.56f);
    front_ridge = od_prop_point(prop, 0.0f, wall + roof, depth * 0.56f);
    back_ridge = od_prop_point(prop, 0.0f, wall + roof, -depth * 0.56f);
    od_draw_triangle(front_left, front_right, front_ridge, COL_MID);
    od_draw_triangle(back_right, back_left, back_ridge, COL_SHADOW);
    od_draw_quad(front_left, front_ridge, back_ridge, back_left, COL_BRIGHT);
    od_draw_quad(front_ridge, front_right, back_right, back_ridge, COL_MID);
    od_draw_line(front_left, front_ridge, COL_INK, 1);
    od_draw_line(front_ridge, front_right, COL_INK, 1);
    od_draw_line(front_ridge, back_ridge, COL_INK, 1);
    od_render_prop_box(prop, 0.0f, 1.12f * scale, depth * 0.515f,
                       0.66f * scale, 1.12f * scale, 0.055f,
                       COL_INK);
    od_render_prop_box(prop, -1.45f * scale, 2.18f * scale,
                       depth * 0.518f, 0.48f * scale, 0.58f * scale,
                       0.06f, COL_SHADOW);
    od_render_prop_box(prop, 1.45f * scale, 2.18f * scale,
                       depth * 0.518f, 0.48f * scale, 0.58f * scale,
                       0.06f, COL_SHADOW);
}

static void od_render_flower(const odwd_world_prop_snapshot *prop) {
    float scale = (float)prop->scale;
    od_v3 base = od_world_v3(prop->position_x, prop->position_y,
                             prop->position_z);
    od_v3 center = base;
    uint32_t petal;
    uint32_t petals = 4u + prop->variant % 4u;
    float petal_radius = (0.32f + (float)(prop->variant % 3u) * 0.07f) * scale;
    center.y += (0.58f + (float)(prop->variant % 5u) * 0.10f) * scale;
    od_draw_line(base, center, COL_FOG_INK, 0);
    for (petal = 0u; petal < petals; ++petal) {
        float angle = (float)petal / (float)petals * 2.0f * OD_PI +
                      (float)prop->rotation_rad;
        od_v3 left = center;
        od_v3 tip = center;
        od_v3 right = center;
        left.x += cosf(angle - 0.42f) * 0.13f * scale;
        left.z += sinf(angle - 0.42f) * 0.13f * scale;
        right.x += cosf(angle + 0.42f) * 0.13f * scale;
        right.z += sinf(angle + 0.42f) * 0.13f * scale;
        tip.x += cosf(angle) * petal_radius;
        tip.z += sinf(angle) * petal_radius;
        tip.y += 0.08f * scale;
        od_draw_triangle(left, tip, right,
                         (petal & 1u) ? COL_BRIGHT : COL_MID);
    }
}

static void od_render_bird(const odwd_world_prop_snapshot *prop,
                           const odwd_frame *frame) {
    float scale = (float)prop->scale;
    float flap = sinf((float)frame->tick * 0.19f +
                      (float)(prop->prop_id & 255u)) * 0.38f;
    od_v3 center = od_world_v3(prop->position_x, prop->position_y +
                               (prop->type == ODWD_PROP_BIRD_GROUND ?
                                0.32 : 0.0), prop->position_z);
    od_v3 nose = center;
    od_v3 tail = center;
    od_v3 left = center;
    od_v3 right = center;
    float yaw = (float)prop->rotation_rad;
    nose.x += sinf(yaw) * 0.48f * scale;
    nose.z += cosf(yaw) * 0.48f * scale;
    tail.x -= sinf(yaw) * 0.34f * scale;
    tail.z -= cosf(yaw) * 0.34f * scale;
    left.x -= cosf(yaw) * 0.84f * scale;
    left.z += sinf(yaw) * 0.84f * scale;
    right.x += cosf(yaw) * 0.84f * scale;
    right.z -= sinf(yaw) * 0.84f * scale;
    left.y += flap * scale;
    right.y -= flap * scale;
    od_draw_triangle(tail, left, center, COL_INK);
    od_draw_triangle(tail, center, right, COL_FOG_INK);
    od_draw_line(tail, nose, COL_INK, 0);
    od_draw_line(left, center, COL_INK, 0);
    od_draw_line(center, right, COL_INK, 0);
}

static void od_render_ramp(const odwd_world_prop_snapshot *prop) {
    float length = (float)prop->scale;
    float width = (float)prop->extent_x_m;
    float height = (float)prop->extent_y_m;
    od_v3 l0 = od_prop_point(prop, -width * 0.5f, 0.0f, -length * 0.5f);
    od_v3 r0 = od_prop_point(prop, width * 0.5f, 0.0f, -length * 0.5f);
    od_v3 l1 = od_prop_point(prop, -width * 0.5f, height, length * 0.5f);
    od_v3 r1 = od_prop_point(prop, width * 0.5f, height, length * 0.5f);
    od_v3 l1_floor = od_prop_point(prop, -width * 0.5f, 0.0f,
                                   length * 0.5f);
    od_v3 r1_floor = od_prop_point(prop, width * 0.5f, 0.0f,
                                   length * 0.5f);
    od_draw_quad(l0, r0, r1, l1, COL_ROAD);
    od_draw_triangle(l0, l1, l1_floor, COL_SHADOW);
    od_draw_triangle(r0, r1_floor, r1, COL_MID);
    od_draw_quad(l1_floor, l1, r1, r1_floor, COL_SHADOW);
    od_draw_line(l0, l1, COL_INK, 1);
    od_draw_line(r0, r1, COL_INK, 1);
    od_draw_line(l1, r1, COL_INK, 1);
    {
        uint32_t stripe;
        for (stripe = 1u; stripe < 5u; ++stripe) {
            float t = (float)stripe / 5.0f;
            od_v3 a = od_v3_add(od_v3_scale(l0, 1.0f - t),
                                 od_v3_scale(l1, t));
            od_v3 b = od_v3_add(od_v3_scale(r0, 1.0f - t),
                                 od_v3_scale(r1, t));
            od_draw_line(a, b, COL_FOG_INK, 0);
        }
    }
}

static void od_render_trampoline(const odwd_world_prop_snapshot *prop) {
    od_v3 center = od_world_v3(prop->position_x, prop->position_y + 0.22,
                               prop->position_z);
    od_v3 previous = center;
    uint32_t index;
    previous.x += 5.7f;
    for (index = 1u; index <= 16u; ++index) {
        float angle = (float)index / 16.0f * 2.0f * OD_PI;
        od_v3 point = center;
        point.x += cosf(angle) * 5.7f;
        point.z += sinf(angle) * 5.7f;
        od_draw_triangle(center, previous, point,
                         (index & 1u) ? COL_MID : COL_BRIGHT);
        od_draw_line(previous, point, COL_INK, 1);
        previous = point;
    }
    for (index = 0u; index < 8u; ++index) {
        float angle = (float)index / 8.0f * 2.0f * OD_PI;
        od_v3 rim = center;
        rim.x += cosf(angle) * 4.3f;
        rim.z += sinf(angle) * 4.3f;
        od_draw_line(center, rim, COL_FOG_INK, 0);
    }
}

static void od_render_goal(const odwd_world_prop_snapshot *prop) {
    od_v3 left = od_prop_point(prop, -5.8f, 0.0f, 0.0f);
    od_v3 right = od_prop_point(prop, 5.8f, 0.0f, 0.0f);
    od_v3 left_top = left;
    od_v3 right_top = right;
    od_v3 left_back;
    od_v3 right_back;
    left_top.y += 4.6f;
    right_top.y += 4.6f;
    left_back = od_prop_point(prop, -5.8f, 0.0f, 3.2f);
    right_back = od_prop_point(prop, 5.8f, 0.0f, 3.2f);
    od_draw_line(left, left_top, COL_INK, 1);
    od_draw_line(right, right_top, COL_INK, 1);
    od_draw_line(left_top, right_top, COL_INK, 1);
    od_draw_line(left, left_back, COL_FOG_INK, 0);
    od_draw_line(right, right_back, COL_FOG_INK, 0);
    od_draw_line(left_back, right_back, COL_FOG_INK, 0);
}

static od_v3 od_ball_rotated_point(od_v3 center, float x, float y, float z,
                                   float axis_x, float axis_z, float angle) {
    float cosine = cosf(angle);
    float sine = sinf(angle);
    float dot = axis_x * x + axis_z * z;
    float cross_x = -axis_z * y;
    float cross_y = axis_z * x - axis_x * z;
    float cross_z = axis_x * y;
    float rx = x * cosine + cross_x * sine + axis_x * dot * (1.0f - cosine);
    float ry = y * cosine + cross_y * sine;
    float rz = z * cosine + cross_z * sine + axis_z * dot * (1.0f - cosine);
    return od_v3_make(center.x + rx, center.y + ry, center.z + rz);
}

static void od_render_ball(const odwd_world_prop_snapshot *prop) {
    enum { BALL_LATS = 6, BALL_SIDES = 16 };
    od_v3 rings[BALL_LATS][BALL_SIDES];
    od_v3 center = od_world_v3(prop->position_x, prop->position_y,
                               prop->position_z);
    float radius = prop->radius_m > 0.1 ? (float)prop->radius_m : 0.92f;
    float axis_x = (float)prop->extent_x_m;
    float axis_z = (float)prop->extent_z_m;
    float axis_len = sqrtf(axis_x * axis_x + axis_z * axis_z);
    float roll = (float)prop->rotation_rad;
    uint32_t lat, side;
    if (axis_len < 0.001f) { axis_x = 1.0f; axis_z = 0.0f; }
    else { axis_x /= axis_len; axis_z /= axis_len; }
    for (lat = 0u; lat < BALL_LATS; ++lat) {
        float phi = -OD_PI * 0.5f +
                    (float)(lat + 1u) / (float)(BALL_LATS + 1u) * OD_PI;
        float ring_radius = cosf(phi) * radius;
        float y = sinf(phi) * radius;
        for (side = 0u; side < BALL_SIDES; ++side) {
            float angle = (float)side / (float)BALL_SIDES * 2.0f * OD_PI;
            rings[lat][side] = od_ball_rotated_point(
                center, cosf(angle) * ring_radius, y,
                sinf(angle) * ring_radius, axis_x, axis_z, roll);
        }
    }
    {
        od_v3 bottom = od_ball_rotated_point(center, 0.0f, -radius, 0.0f,
                                             axis_x, axis_z, roll);
        od_v3 top = od_ball_rotated_point(center, 0.0f, radius, 0.0f,
                                          axis_x, axis_z, roll);
        for (side = 0u; side < BALL_SIDES; ++side) {
            uint32_t next = (side + 1u) % BALL_SIDES;
            od_draw_triangle(bottom, rings[0][next], rings[0][side],
                             (side & 2u) ? COL_SHADOW : COL_SHELL);
            for (lat = 0u; lat + 1u < BALL_LATS; ++lat) {
                uint32_t color = ((side + lat * 3u) % 7u) < 2u ?
                                 COL_INK : (lat & 1u ? COL_BRIGHT : COL_SHELL);
                od_draw_quad(rings[lat][side], rings[lat][next],
                             rings[lat + 1u][next], rings[lat + 1u][side],
                             color);
            }
            od_draw_triangle(top, rings[BALL_LATS - 1u][side],
                             rings[BALL_LATS - 1u][next],
                             (side & 3u) == 0u ? COL_INK : COL_BRIGHT);
        }
    }
}
static void od_render_survival_prop(const odwd_world_prop_snapshot *prop) {
    float hx = fmaxf((float)prop->extent_x_m * 0.5f, 0.10f);
    float hy = fmaxf((float)prop->extent_y_m * 0.5f, 0.10f);
    float hz = fmaxf((float)prop->extent_z_m * 0.5f, 0.10f);
    if (prop->type == ODWD_PROP_SURVIVAL_RAMP) {
        od_v3 a = od_prop_point(prop, -hx, 0.0f, -hz);
        od_v3 b = od_prop_point(prop, hx, 0.0f, -hz);
        od_v3 c = od_prop_point(prop, hx, hy * 2.0f, hz);
        od_v3 d = od_prop_point(prop, -hx, hy * 2.0f, hz);
        od_draw_quad(a, b, c, d, COL_BRIGHT);
        od_draw_triangle(a, d, c, COL_MID);
        od_draw_triangle(a, c, b, COL_SHADOW);
        od_draw_line(a, d, COL_INK, 1);
        od_draw_line(b, c, COL_INK, 1);
        return;
    }
    od_render_prop_box(prop, 0.0f, hy, 0.0f, hx, hy, hz,
                       prop->type == ODWD_PROP_SURVIVAL_PLATFORM ?
                           ((prop->variant & 1u) ? COL_ARENA_ALT : COL_ARENA) :
                       prop->type == ODWD_PROP_SURVIVAL_SWEEPER ? COL_INK :
                       (prop->variant & 1u) ? COL_OBSTACLE_ALT : COL_OBSTACLE);
}

static void od_render_music_disc(const odwd_world_prop_snapshot *prop,
                                 uint32_t fill, uint32_t edge,
                                 float height_offset) {
    enum { SIDES = 24 };
    od_v3 center = od_world_v3(prop->position_x,
                               prop->position_y + height_offset,
                               prop->position_z);
    float radius = fmaxf((float)prop->radius_m, 0.8f);
    uint32_t i;
    for (i = 0u; i < SIDES; ++i) {
        float a0 = (float)i / (float)SIDES * 2.0f * OD_PI;
        float a1 = (float)(i + 1u) / (float)SIDES * 2.0f * OD_PI;
        od_v3 p0 = od_v3_make(center.x + cosf(a0) * radius, center.y,
                               center.z + sinf(a0) * radius);
        od_v3 p1 = od_v3_make(center.x + cosf(a1) * radius, center.y,
                               center.z + sinf(a1) * radius);
        if (fill != 0u) od_draw_triangle(center, p1, p0, fill);
        od_draw_line(p0, p1, edge, 1);
    }
}

static void od_render_music_prop(const odwd_world_prop_snapshot *prop,
                                 const odwd_frame *frame) {
    float hx = fmaxf((float)prop->extent_x_m * 0.5f, 0.10f);
    float hy = fmaxf((float)prop->extent_y_m * 0.5f, 0.08f);
    float hz = fmaxf((float)prop->extent_z_m * 0.5f, 0.10f);
    float pulse = (float)frame->music_pulse_01;
    if (prop->type == ODWD_PROP_MUSIC_PLATFORM) {
        uint32_t base = (prop->variant & 1u) ? COL_ARENA_ALT : COL_ARENA;
        uint32_t color = od_mix_color(base, COL_OBSTACLE_ALT,
            od_clampf((float)frame->music_mid_01 * 0.16f, 0.0f, 0.18f));
        od_render_prop_box(prop, 0.0f, hy, 0.0f, hx, hy, hz, color);
        return;
    }
    if (prop->type == ODWD_PROP_MUSIC_BOUNDARY) {
        uint32_t color = (prop->variant & 1u) ? COL_OBSTACLE : COL_INK;
        od_render_prop_box(prop, 0.0f, hy, 0.0f, hx, hy, hz, color);
        return;
    }
    if (prop->type == ODWD_PROP_MUSIC_SHELTER) {
        float post = fmaxf(hx * 0.08f, 0.20f);
        float roof_y = hy * 1.70f;
        od_render_prop_box(prop, -hx * 0.78f, hy * 0.82f, -hz * 0.78f,
                           post, hy * 0.82f, post, COL_INK);
        od_render_prop_box(prop, hx * 0.78f, hy * 0.82f, -hz * 0.78f,
                           post, hy * 0.82f, post, COL_INK);
        od_render_prop_box(prop, -hx * 0.78f, hy * 0.82f, hz * 0.78f,
                           post, hy * 0.82f, post, COL_INK);
        od_render_prop_box(prop, hx * 0.78f, hy * 0.82f, hz * 0.78f,
                           post, hy * 0.82f, post, COL_INK);
        od_render_prop_box(prop, 0.0f, roof_y, 0.0f,
                           hx, fmaxf(hy * 0.11f, 0.18f), hz, COL_BRIGHT);
        return;
    }
    if (prop->type == ODWD_PROP_MUSIC_SAFE_PAD) {
        uint32_t layer;
        /* Flood refuges are physical terrain clues, not a UI answer key. The
         * old bright/mid tint revealed the winning mountain immediately. Draw
         * every refuge with the same material and let its authoritative height
         * communicate the risk. A tapered five-tier silhouette also reads as
         * a climbable hill instead of an anonymous rectangular block. */
        for (layer = 0u; layer < 5u; ++layer) {
            float layer_h = hy * 0.20f;
            float y = hy * (0.20f + 0.40f * (float)layer);
            float taper = 1.0f - (float)layer * 0.13f;
            uint32_t color = (layer & 1u) ? COL_SAFE : od_mix_color(COL_SAFE, COL_ARENA, 0.28f);
            od_render_prop_box(prop, 0.0f, y, 0.0f,
                               hx * 0.56f * taper, layer_h,
                               hz * 0.56f * taper, color);
        }
        od_render_music_disc(prop, 0u, COL_INK,
                             0.08f + (float)frame->music_mid_01 * 0.04f);
        return;
    }
    if (prop->type == ODWD_PROP_MUSIC_WARNING) {
        uint32_t hazard = (prop->variant >> 8u) & 255u;
        float expand = 1.0f + 0.10f * sinf((float)frame->tick * 0.11f +
                                           (float)(prop->variant & 7u));
        odwd_world_prop_snapshot ring = *prop;
        if (hazard == 3u) {
            /* Quake is arena-wide, so telegraph it with concentric ground
             * waves rather than a meaningless single target circle. */
            uint32_t wave;
            for (wave = 0u; wave < 4u; ++wave) {
                float phase = ((float)(frame->tick % 120u) / 120.0f +
                               (float)wave * 0.23f);
                phase -= floorf(phase);
                ring.radius_m = fmaxf(1.0f, (float)prop->radius_m *
                                      (0.28f + phase * 0.72f));
                od_render_music_disc(&ring, 0u,
                    (wave & 1u) ? COL_WARNING : COL_INK,
                    0.13f + pulse * 0.07f);
            }
        } else if (hazard == 6u) {
            /* Wind direction is encoded in prop rotation by the C authority.
             * Parallel world-space streaks show what will push the car without
             * returning to the old screen-space speed scratches. */
            uint32_t line;
            float radius = fmaxf((float)prop->radius_m, 5.0f);
            od_render_music_disc(&ring, 0u, COL_WARNING, 0.12f);
            for (line = 0u; line < 9u; ++line) {
                float lane = ((float)line - 4.0f) * radius * 0.18f;
                float flow_period = radius * 0.75f;
                float flow = (float)frame->tick * 0.085f +
                             (float)line * 0.77f;
                flow -= floorf(flow / flow_period) * flow_period;
                od_v3 a = od_prop_point(prop, -radius * 0.72f + flow,
                                        0.20f + pulse * 0.05f, lane);
                od_v3 b = od_prop_point(prop, -radius * 0.18f + flow,
                                        0.20f + pulse * 0.05f, lane);
                od_draw_line(a, b, (line & 1u) ? COL_OBSTACLE : COL_WARNING, 1);
            }
        } else if (hazard == 5u) {
            /* Flood warning: a broad lava-colored perimeter pulse appears
             * before the actual flood plane, giving time to read the hills. */
            ring.radius_m = (float)prop->radius_m *
                            (0.90f + 0.05f * sinf((float)frame->tick * 0.08f));
            od_render_music_disc(&ring, 0u, COL_LAVA_HOT, 0.13f + pulse * 0.05f);
            ring.radius_m *= 0.82f;
            od_render_music_disc(&ring, 0u, COL_WARNING, 0.14f);
        } else if (hazard == 7u && frame->music_hazard_phase == 2u) {
            /* Expanding physical shockwave: the visual ring is the same
             * authoritative radius used by the event timeline, not a random
             * screen-space flash. */
            float max_radius = fmaxf((float)prop->extent_x_m * 0.5f, 4.0f);
            ring.radius_m = max_radius *
                            od_clampf((float)frame->music_hazard_time_01,
                                      0.0f, 1.0f);
            od_render_music_disc(&ring, 0u, COL_SHOCK,
                                 0.16f + pulse * 0.16f);
            ring.radius_m = fmaxf(0.8, ring.radius_m - 1.7);
            od_render_music_disc(&ring, 0u, COL_INK, 0.15f);
        } else {
            ring.radius_m *= expand;
            od_render_music_disc(&ring, 0u,
                                 ((frame->tick / 8u) & 1u) ? COL_WARNING : COL_INK,
                                 0.12f + pulse * 0.08f);
            if (hazard == 8u && frame->music_hazard_phase == 2u &&
                ((frame->tick + (prop->variant & 7u) * 5u) % 18u) < 4u) {
                od_v3 ground = od_world_v3(prop->position_x,
                                           prop->position_y + 0.15,
                                           prop->position_z);
                od_v3 sky = ground;
                sky.y += 18.0f + (float)(prop->variant & 7u);
                od_draw_line(sky, ground, COL_ELECTRIC, 2);
            }
        }
        return;
    }
    if (prop->type == ODWD_PROP_MUSIC_HOLE) {
        od_render_music_disc(prop, COL_INK, COL_WARNING, -0.18f);
        return;
    }
    if (prop->type == ODWD_PROP_MUSIC_LAVA) {
        uint32_t i;
        uint32_t fill = od_mix_color(COL_LAVA, COL_LAVA_HOT,
                                     0.20f + pulse * 0.28f);
        if ((prop->variant & UINT32_C(0x10000)) != 0u) {
            od_render_prop_box(prop, 0.0f, hy, 0.0f, hx, hy, hz, fill);
            /* Deterministic hot seams give the flood a flowing lava material
             * instead of a flat monochrome rectangle. */
            for (i = 0u; i < 9u; ++i) {
                float t = ((float)i + 0.5f) / 9.0f;
                float wobble = sinf((float)frame->tick * 0.035f +
                                    (float)i * 1.7f) * hz * 0.10f;
                od_v3 a = od_prop_point(prop, -hx, hy * 2.02f,
                                        -hz + t * hz * 2.0f + wobble);
                od_v3 b = od_prop_point(prop, hx, hy * 2.02f,
                                        -hz + t * hz * 2.0f - wobble);
                od_draw_line(a, b, (i & 1u) ? COL_LAVA_HOT : COL_INK, 1);
            }
        } else {
            od_render_music_disc(prop, fill, COL_LAVA_HOT, 0.10f);
            for (i = 0u; i < 6u; ++i) {
                float angle = ((float)i / 6.0f) * 2.0f * OD_PI +
                              (float)frame->tick * 0.008f;
                od_v3 c = od_world_v3(prop->position_x,
                                      prop->position_y + 0.13, prop->position_z);
                od_v3 q = c;
                q.x += cosf(angle) * (float)prop->radius_m * 0.78f;
                q.z += sinf(angle) * (float)prop->radius_m * 0.78f;
                od_draw_line(c, q, (i & 1u) ? COL_LAVA_HOT : COL_INK, 1);
            }
        }
        return;
    }
    if (prop->type == ODWD_PROP_MUSIC_METEOR) {
        float stagger = (float)(prop->variant & 7u) * 0.117f;
        float raw_phase = (float)frame->music_hazard_time_01 * 1.55f + stagger;
        float phase = raw_phase - (float)(uint32_t)raw_phase;
        od_v3 impact = od_world_v3(prop->position_x,
                                   prop->position_y + 0.12,
                                   prop->position_z);
        od_v3 meteor = impact;
        od_v3 left, right, front, back, top;
        meteor.y += 2.0f + (1.0f - phase) * 21.0f;
        left = meteor; left.x -= 1.0f;
        right = meteor; right.x += 1.0f;
        front = meteor; front.z += 1.0f;
        back = meteor; back.z -= 1.0f;
        top = meteor; top.y += 1.7f;
        od_draw_triangle(left, front, top, COL_METEOR);
        od_draw_triangle(front, right, top, COL_BRIGHT);
        od_draw_triangle(right, back, top, COL_MID);
        od_draw_triangle(back, left, top, COL_BRIGHT);
        od_draw_line(meteor, impact, COL_FOG_INK, 1);
        od_render_music_disc(prop, 0u, COL_INK, 0.08f);
    }
}

static void od_render_world_props(const void *storage,
                                  const odwd_frame *frame,
                                  const odwd_vehicle_snapshot *player) {
    uint32_t count = odwd_engine_world_prop_count(storage);
    uint32_t index;
    for (index = 0u; index < count; ++index) {
        odwd_world_prop_snapshot prop;
        double dx;
        double dz;
        if (odwd_engine_read_world_prop(storage, index, &prop) != ODWD_OK)
            continue;
        dx = prop.position_x - player->position_x;
        dz = prop.position_z - player->position_z;
        if (dx * dx + dz * dz > 145000.0) continue;
        if (prop.type == ODWD_PROP_TREE) od_render_tree(&prop);
        else if (prop.type == ODWD_PROP_SCULPTURE)
            od_render_sculpture(&prop);
        else if (prop.type == ODWD_PROP_HOUSE) od_render_house(&prop);
        else if (prop.type == ODWD_PROP_FLOWER) od_render_flower(&prop);
        else if (prop.type == ODWD_PROP_BIRD_GROUND ||
                 prop.type == ODWD_PROP_BIRD_FLYING)
            od_render_bird(&prop, frame);
        else if (prop.type == ODWD_PROP_RAMP ||
                 prop.type == ODWD_PROP_RAMP_LARGE)
            od_render_ramp(&prop);
        else if (prop.type == ODWD_PROP_TRAMPOLINE)
            od_render_trampoline(&prop);
        else if (prop.type == ODWD_PROP_GOAL) od_render_goal(&prop);
        else if (prop.type == ODWD_PROP_BARRIER)
            od_render_prop_box(&prop, 0.0f, 0.78f, 0.0f,
                               6.2f, 0.78f, 0.82f, COL_MID);
        else if (prop.type == ODWD_PROP_BUMPER_FLOOR) {
            odwd_world_prop_snapshot ring = prop;
            od_render_music_disc(&ring, COL_SHADOW, COL_BRIGHT, 0.015f);
            ring.radius_m *= 0.72;
            od_render_music_disc(&ring, 0u, COL_MID, 0.025f);
            ring.radius_m *= 0.56;
            od_render_music_disc(&ring, 0u, COL_INK, 0.035f);
        }
        else if (prop.type == ODWD_PROP_BALL) od_render_ball(&prop);
        else if (prop.type >= ODWD_PROP_SURVIVAL_PLATFORM &&
                 prop.type <= ODWD_PROP_SURVIVAL_GATE)
            od_render_survival_prop(&prop);
        else if (prop.type >= ODWD_PROP_MUSIC_PLATFORM &&
                 prop.type <= ODWD_PROP_MUSIC_HOLE)
            od_render_music_prop(&prop, frame);
        else od_render_low_prop(&prop);
    }
}

static void od_render_pickups(const void *storage, const odwd_frame *frame,
                              const odwd_vehicle_snapshot *player) {
    uint32_t count = odwd_engine_pickup_count(storage);
    uint32_t index;
    for (index = 0u; index < count; ++index) {
        odwd_pickup_snapshot pickup;
        double dx;
        double dz;
        od_v3 center;
        od_v3 top;
        od_v3 bottom;
        od_v3 left;
        od_v3 right;
        float pulse;
        if (odwd_engine_read_pickup(storage, index, &pickup) != ODWD_OK ||
            !pickup.active) continue;
        dx = pickup.position_x - player->position_x;
        dz = pickup.position_z - player->position_z;
        if (dx * dx + dz * dz > 110000.0) continue;
        pulse = 1.0f + 0.12f * sinf((float)frame->tick * 0.075f +
                                    (float)index);
        center = od_world_v3(pickup.position_x, pickup.position_y,
                             pickup.position_z);
        top = center;
        bottom = center;
        left = center;
        right = center;
        top.y += 1.05f * pulse;
        bottom.y -= 1.05f * pulse;
        left.x -= 0.72f * pulse;
        right.x += 0.72f * pulse;
        od_draw_triangle(top, left, bottom, COL_SHELL);
        od_draw_triangle(top, bottom, right, COL_MID);
        od_draw_line(top, left, COL_INK, 1);
        od_draw_line(left, bottom, COL_INK, 1);
        od_draw_line(bottom, right, COL_INK, 1);
        od_draw_line(right, top, COL_INK, 1);
    }
}

static od_v3 od_car_point(const odwd_vehicle_snapshot *car,
                          float local_x, float local_y, float local_z,
                          float roll, float pitch) {
    float cosine = cosf((float)car->heading_rad);
    float sine = sinf((float)car->heading_rad);
    float cosine_pitch = cosf(pitch);
    float sine_pitch = sinf(pitch);
    float cosine_roll = cosf(roll);
    float sine_roll = sinf(roll);
    float pitched_y = local_y * cosine_pitch - local_z * sine_pitch;
    float adjusted_z = local_y * sine_pitch + local_z * cosine_pitch;
    float adjusted_x = local_x * cosine_roll - pitched_y * sine_roll;
    float adjusted_y = local_x * sine_roll + pitched_y * cosine_roll;
    /* Pitch, roll and yaw are composed as rigid rotations. The former
     * small-angle shortcut collapsed most of the body volume in large jumps. */
    return od_v3_make((float)(car->position_x - g_origin_x) +
                      adjusted_x * cosine + adjusted_z * sine,
                      (float)(car->position_y - g_origin_y) + adjusted_y,
                      (float)(car->position_z - g_origin_z) -
                      adjusted_x * sine + adjusted_z * cosine);
}

static void od_render_car_box(const odwd_vehicle_snapshot *car,
                              float cx, float cy, float cz,
                              float hx, float hy, float hz,
                              float roll, float pitch, uint32_t color) {
    od_v3 v[8];
    uint32_t index;
    for (index = 0u; index < 8u; ++index) {
        float x = cx + ((index & 1u) ? hx : -hx);
        float y = cy + ((index & 2u) ? hy : -hy);
        float z = cz + ((index & 4u) ? hz : -hz);
        v[index] = od_car_point(car, x, y, z, roll, pitch);
    }
    od_draw_quad(v[0], v[1], v[3], v[2], color);
    od_draw_quad(v[4], v[6], v[7], v[5], color);
    od_draw_quad(v[0], v[4], v[5], v[1], color);
    od_draw_quad(v[2], v[3], v[7], v[6], color);
    od_draw_quad(v[0], v[2], v[6], v[4], color);
    od_draw_quad(v[1], v[5], v[7], v[3], color);
}

static double od_shadow_ground_height(const void *storage,
                                      const odwd_frame *frame,
                                      const odwd_vehicle_snapshot *car,
                                      double world_x, double world_z) {
    uint32_t index;
    double local_x;
    double local_z;
    double best_d2 = 1.0e30;
    double best_y = car->position_y - 0.500;
    uint32_t best_flags = 0u;
    if (frame->world_mode == ODWD_MODE_OPEN_FIELD ||
        frame->world_mode == ODWD_MODE_SURVIVAL)
        return odwd_engine_ground_height(storage, world_x, world_z) + 0.025;
    local_x = world_x - g_origin_x;
    local_z = world_z - g_origin_z;
    for (index = 0u; index + 1u < g_road_count; ++index) {
        const od_render_road *a = &g_road[index];
        const od_render_road *b = &g_road[index + 1u];
        uint32_t route;
        for (route = 0u; route < 2u; ++route) {
            od_v3 pa;
            od_v3 pb;
            double dx;
            double dz;
            double length2;
            double t;
            double px;
            double pz;
            double d2;
            if (route != 0u &&
                fmaxf(a->alternate_half_width,
                      b->alternate_half_width) <=
                      OD_BRANCH_MIN_HALF_WIDTH)
                continue;
            pa = route != 0u ? a->alternate : a->center;
            pb = route != 0u ? b->alternate : b->center;
            dx = (double)pb.x - (double)pa.x;
            dz = (double)pb.z - (double)pa.z;
            length2 = dx * dx + dz * dz;
            if (length2 < 1.0e-8) continue;
            t = ((local_x - (double)pa.x) * dx +
                 (local_z - (double)pa.z) * dz) / length2;
            if (t < 0.0) t = 0.0;
            else if (t > 1.0) t = 1.0;
            px = (double)pa.x + dx * t;
            pz = (double)pa.z + dz * t;
            d2 = (local_x - px) * (local_x - px) +
                 (local_z - pz) * (local_z - pz);
            if (d2 < best_d2) {
                best_d2 = d2;
                best_y = g_origin_y + (double)pa.y +
                         ((double)pb.y - (double)pa.y) * t + 0.080;
                best_flags = a->flags | b->flags;
            }
        }
    }
    if (best_flags & ODWD_ROAD_GAP)
        return car->position_y - 0.500 -
               (car->airborne ? fmin(car->air_time_s * 2.2, 8.0) : 0.0);
    return best_y;
}

static void od_render_shadow(const void *storage, const odwd_frame *frame,
                             const odwd_vehicle_snapshot *car) {
    double ground_y = od_shadow_ground_height(storage, frame, car,
                                               car->position_x,
                                               car->position_z);
    od_v3 center = od_world_v3(car->position_x,
                               ground_y,
                               car->position_z);
    od_v3 previous;
    uint32_t index;
    float yaw = (float)car->heading_rad;
    previous = od_v3_make(center.x + cosf(yaw) * 1.18f,
                          center.y, center.z - sinf(yaw) * 1.18f);
    previous.y = (float)(od_shadow_ground_height(storage, frame, car,
        (double)previous.x + g_origin_x,
        (double)previous.z + g_origin_z) - g_origin_y);
    for (index = 1u; index <= 14u; ++index) {
        float angle = (float)index / 14.0f * 2.0f * OD_PI;
        float lx = cosf(angle) * 1.22f;
        float lz = sinf(angle) * 2.18f;
        od_v3 point = od_v3_make(center.x + lx * cosf(yaw) + lz * sinf(yaw),
                                  center.y,
                                  center.z - lx * sinf(yaw) + lz * cosf(yaw));
        point.y = (float)(od_shadow_ground_height(storage, frame, car,
            (double)point.x + g_origin_x,
            (double)point.z + g_origin_z) - g_origin_y);
        od_draw_triangle(center, previous, point,
                         od_mix_color(COL_SHADOW, COL_PAPER, 0.46f));
        previous = point;
    }
}

static void od_render_car_wheel(const odwd_vehicle_snapshot *car,
                                float base_x, float base_z, float steering,
                                float roll, float pitch) {
    enum { WHEEL_SIDES = 12 };
    od_v3 outer[WHEEL_SIDES];
    od_v3 inner[WHEEL_SIDES];
    od_v3 hub[WHEEL_SIDES];
    od_v3 outer_center;
    od_v3 inner_center;
    od_v3 hub_center;
    float side = base_x < 0.0f ? -1.0f : 1.0f;
    float axis_x = cosf(steering);
    float axis_z = -sinf(steering);
    float forward_x = sinf(steering);
    float forward_z = cosf(steering);
    float spin = (float)car->traveled_distance_m / 0.43f;
    uint32_t wheel_index;
    outer_center = od_car_point(car,
        base_x + axis_x * 0.19f * side, -0.12f,
        base_z + axis_z * 0.19f * side, roll, pitch);
    inner_center = od_car_point(car,
        base_x - axis_x * 0.16f * side, -0.12f,
        base_z - axis_z * 0.16f * side, roll, pitch);
    hub_center = od_car_point(car,
        base_x + axis_x * 0.205f * side, -0.12f,
        base_z + axis_z * 0.205f * side, roll, pitch);
    for (wheel_index = 0u; wheel_index < WHEEL_SIDES; ++wheel_index) {
        float angle = spin + (float)wheel_index /
                      (float)WHEEL_SIDES * 2.0f * OD_PI;
        float vertical = sinf(angle);
        float rolling = cosf(angle);
        float outer_x = base_x + axis_x * 0.19f * side;
        float outer_z = base_z + axis_z * 0.19f * side;
        float inner_x = base_x - axis_x * 0.16f * side;
        float inner_z = base_z - axis_z * 0.16f * side;
        outer[wheel_index] = od_car_point(car,
            outer_x + forward_x * rolling * 0.43f,
            -0.12f + vertical * 0.43f,
            outer_z + forward_z * rolling * 0.43f, roll, pitch);
        inner[wheel_index] = od_car_point(car,
            inner_x + forward_x * rolling * 0.43f,
            -0.12f + vertical * 0.43f,
            inner_z + forward_z * rolling * 0.43f, roll, pitch);
        hub[wheel_index] = od_car_point(car,
            base_x + axis_x * 0.207f * side +
                forward_x * rolling * 0.215f,
            -0.12f + vertical * 0.215f,
            base_z + axis_z * 0.207f * side +
                forward_z * rolling * 0.215f, roll, pitch);
    }
    for (wheel_index = 0u; wheel_index < WHEEL_SIDES; ++wheel_index) {
        uint32_t next = (wheel_index + 1u) % WHEEL_SIDES;
        od_draw_quad(outer[wheel_index], outer[next], inner[next],
                     inner[wheel_index], COL_TIRE);
        od_draw_triangle(outer_center, outer[next], outer[wheel_index],
                         COL_TIRE);
        od_draw_triangle(inner_center, inner[wheel_index], inner[next],
                         COL_TIRE);
        od_draw_triangle(hub_center, hub[wheel_index], hub[next],
                         COL_SHADOW);
        od_draw_line(outer[wheel_index], outer[next], COL_INK, 0);
    }
}

static void od_render_car(const void *storage, const odwd_frame *frame,
                          const odwd_vehicle_snapshot *car, uint32_t index) {
    static const float section_z[7] = {
        2.34f, 1.86f, 1.10f, 0.20f, -0.82f, -1.72f, -2.22f
    };
    static const float section_width[7] = {
        0.74f, 0.91f, 0.98f, 1.00f, 0.98f, 0.91f, 0.78f
    };
    static const float section_bottom[7] = {
        -0.21f, -0.31f, -0.36f, -0.37f, -0.36f, -0.31f, -0.20f
    };
    static const float section_top[7] = {
        0.08f, 0.23f, 0.32f, 0.35f, 0.31f, 0.19f, 0.07f
    };
    od_v3 lower_left[7];
    od_v3 lower_right[7];
    od_v3 upper_left[7];
    od_v3 upper_right[7];
    float roll = od_clampf((float)car->body_roll_rad -
                           (float)car->lateral_speed_mps * 0.004f,
                           -1.12f, 1.12f);
    float pitch = od_clampf((float)car->body_pitch_rad -
                            (float)car->velocity_y * 0.006f,
                            -1.16f, 1.16f);
    uint32_t shell_side = index == 0u ? COL_MID :
                          od_mix_color(COL_MID, COL_SHADOW,
                                       (float)(index % 3u) * 0.13f);
    uint32_t section;
    od_render_shadow(storage, frame, car);
    od_render_car_wheel(car, -0.93f, 1.36f,
                        (float)car->steering_rad, roll, pitch);
    od_render_car_wheel(car, 0.93f, 1.36f,
                        (float)car->steering_rad, roll, pitch);
    od_render_car_wheel(car, -0.93f, -1.38f, 0.0f, roll, pitch);
    od_render_car_wheel(car, 0.93f, -1.38f, 0.0f, roll, pitch);
    for (section = 0u; section < 7u; ++section) {
        lower_left[section] = od_car_point(car, -section_width[section],
            section_bottom[section], section_z[section], roll, pitch);
        lower_right[section] = od_car_point(car, section_width[section],
            section_bottom[section], section_z[section], roll, pitch);
        upper_left[section] = od_car_point(car, -section_width[section],
            section_top[section], section_z[section], roll, pitch);
        upper_right[section] = od_car_point(car, section_width[section],
            section_top[section], section_z[section], roll, pitch);
    }
    for (section = 0u; section + 1u < 7u; ++section) {
        od_draw_quad(lower_left[section], lower_left[section + 1u],
                     upper_left[section + 1u], upper_left[section],
                     shell_side);
        od_draw_quad(lower_right[section], upper_right[section],
                     upper_right[section + 1u], lower_right[section + 1u],
                     shell_side);
        od_draw_quad(upper_left[section], upper_left[section + 1u],
                     upper_right[section + 1u], upper_right[section],
                     section < 3u ? COL_SHELL : COL_MID);
        od_draw_quad(lower_left[section], lower_right[section],
                     lower_right[section + 1u], lower_left[section + 1u],
                     COL_SHADOW);
    }
    od_draw_quad(lower_left[0], upper_left[0], upper_right[0],
                 lower_right[0], COL_SHELL);
    od_draw_quad(lower_left[6], lower_right[6], upper_right[6],
                 upper_left[6], COL_SHADOW);
    {
        od_v3 canopy_fl = od_car_point(car, -0.65f, 0.27f, 0.74f, roll, pitch);
        od_v3 canopy_fr = od_car_point(car, 0.65f, 0.27f, 0.74f, roll, pitch);
        od_v3 canopy_rl = od_car_point(car, -0.61f, 0.25f, -0.91f, roll, pitch);
        od_v3 canopy_rr = od_car_point(car, 0.61f, 0.25f, -0.91f, roll, pitch);
        od_v3 roof_fl = od_car_point(car, -0.46f, 1.02f, 0.34f, roll, pitch);
        od_v3 roof_fr = od_car_point(car, 0.46f, 1.02f, 0.34f, roll, pitch);
        od_v3 roof_rl = od_car_point(car, -0.45f, 0.97f, -0.62f, roll, pitch);
        od_v3 roof_rr = od_car_point(car, 0.45f, 0.97f, -0.62f, roll, pitch);
        uint32_t mullion;
        od_draw_quad(canopy_fl, roof_fl, roof_fr, canopy_fr, COL_INK);
        od_draw_quad(canopy_fl, canopy_rl, roof_rl, roof_fl, COL_TIRE);
        od_draw_quad(canopy_fr, roof_fr, roof_rr, canopy_rr, COL_TIRE);
        od_draw_quad(canopy_rl, canopy_rr, roof_rr, roof_rl, COL_TIRE);
        od_draw_quad(roof_fl, roof_rl, roof_rr, roof_fr, COL_SHELL);
        od_draw_line(roof_fl, roof_fr, COL_INK, 0);
        od_draw_line(roof_rl, roof_rr, COL_INK, 0);
        for (mullion = 1u; mullion <= 4u; ++mullion) {
            float t = (float)mullion / 5.0f;
            od_v3 left = od_v3_add(od_v3_scale(roof_fl, 1.0f - t),
                                    od_v3_scale(roof_rl, t));
            od_v3 right = od_v3_add(od_v3_scale(roof_fr, 1.0f - t),
                                     od_v3_scale(roof_rr, t));
            od_draw_line(left, right, COL_MID, 0);
        }
        od_render_car_box(car, -0.78f, 0.40f, 0.39f,
                          0.13f, 0.08f, 0.12f,
                          roll, pitch, COL_INK);
        od_render_car_box(car, 0.78f, 0.40f, 0.39f,
                          0.13f, 0.08f, 0.12f,
                          roll, pitch, COL_INK);
    }
    for (section = 0u; section + 1u < 7u; ++section) {
        od_draw_line(upper_left[section], upper_left[section + 1u],
                     COL_INK, index == 0u ? 1 : 0);
        od_draw_line(upper_right[section], upper_right[section + 1u],
                     COL_INK, index == 0u ? 1 : 0);
    }
    od_draw_line(lower_left[0], lower_left[6], COL_INK, 0);
    od_draw_line(lower_right[0], lower_right[6], COL_INK, 0);
    od_draw_line(upper_left[0], upper_right[0], COL_INK, 0);
    od_draw_line(upper_left[6], upper_right[6], COL_INK, 0);
    {
        od_v3 crease_a = od_car_point(car, 0.78f, 0.09f, 1.12f, roll, pitch);
        od_v3 crease_b = od_car_point(car, 0.68f, 0.09f, -1.55f, roll, pitch);
        od_draw_line(crease_a, crease_b, COL_INK, 0);
    }
    {
        od_v3 grille_l = od_car_point(car, -0.48f, -0.02f, 2.36f,
                                      roll, pitch);
        od_v3 grille_r = od_car_point(car, 0.48f, -0.02f, 2.36f,
                                      roll, pitch);
        od_v3 lamp_l0 = od_car_point(car, -0.72f, 0.12f, 2.25f,
                                     roll, pitch);
        od_v3 lamp_l1 = od_car_point(car, -0.30f, 0.14f, 2.31f,
                                     roll, pitch);
        od_v3 lamp_r0 = od_car_point(car, 0.30f, 0.14f, 2.31f,
                                     roll, pitch);
        od_v3 lamp_r1 = od_car_point(car, 0.72f, 0.12f, 2.25f,
                                     roll, pitch);
        od_v3 spoiler_l = od_car_point(car, -0.72f, 0.48f, -1.96f,
                                       roll, pitch);
        od_v3 spoiler_r = od_car_point(car, 0.72f, 0.48f, -1.96f,
                                       roll, pitch);
        od_v3 spoiler_lb = od_car_point(car, -0.61f, 0.19f, -1.82f,
                                        roll, pitch);
        od_v3 spoiler_rb = od_car_point(car, 0.61f, 0.19f, -1.82f,
                                        roll, pitch);
        od_draw_line(grille_l, grille_r, COL_INK, 1);
        od_draw_line(lamp_l0, lamp_l1, COL_INK, 1);
        od_draw_line(lamp_r0, lamp_r1, COL_INK, 1);
        od_draw_line(spoiler_lb, spoiler_l, COL_INK, 0);
        od_draw_line(spoiler_rb, spoiler_r, COL_INK, 0);
        od_draw_line(spoiler_l, spoiler_r, COL_INK, 1);
    }
    if (index == 0u) {
        od_v3 stripe_a1 = od_car_point(car, -0.10f, 0.23f, 1.52f, roll, pitch);
        od_v3 stripe_b1 = od_car_point(car, -0.10f, 0.22f, -1.54f, roll, pitch);
        od_v3 stripe_a2 = od_car_point(car, 0.10f, 0.23f, 1.52f, roll, pitch);
        od_v3 stripe_b2 = od_car_point(car, 0.10f, 0.22f, -1.54f, roll, pitch);
        od_draw_line(stripe_a1, stripe_b1, COL_INK, 0);
        od_draw_line(stripe_a2, stripe_b2, COL_INK, 0);
    } else {
        uint32_t pattern = index % 6u;
        if (pattern == 1u || pattern == 4u) {
            od_v3 mark_a = od_car_point(car, -0.63f, 0.12f, 0.92f, roll, pitch);
            od_v3 mark_b = od_car_point(car, 0.63f, 0.12f, -0.84f, roll, pitch);
            od_draw_line(mark_a, mark_b, COL_INK, 1);
        } else if (pattern == 2u || pattern == 5u) {
            od_v3 mark_a = od_car_point(car, 0.0f, 0.24f, 1.28f, roll, pitch);
            od_v3 mark_b = od_car_point(car, 0.0f, 0.24f, -1.48f, roll, pitch);
            od_draw_line(mark_a, mark_b, COL_INK, 1);
        }
    }
}

static void od_fx_reset(void) {
    memset(g_skids, 0, sizeof(g_skids));
    memset(g_particles, 0, sizeof(g_particles));
    g_skid_cursor = 0u;
    g_particle_cursor = 0u;
    g_last_tick = 0u;
    g_last_beat_tick = UINT64_MAX;
    g_last_rear_valid = 0;
}

static void od_fx_rebase(float dx, float dy, float dz) {
    uint32_t index;
    od_v3 delta = od_v3_make(dx, dy, dz);
    for (index = 0u; index < OD_RENDER_MAX_SKIDS; ++index) {
        if (g_skids[index].life <= 0.0f) continue;
        g_skids[index].a = od_v3_add(g_skids[index].a, delta);
        g_skids[index].b = od_v3_add(g_skids[index].b, delta);
    }
    for (index = 0u; index < OD_RENDER_MAX_PARTICLES; ++index) {
        if (g_particles[index].life <= 0.0f) continue;
        g_particles[index].position = od_v3_add(g_particles[index].position,
                                                 delta);
    }
    if (g_last_rear_valid) {
        g_last_rear_left = od_v3_add(g_last_rear_left, delta);
        g_last_rear_right = od_v3_add(g_last_rear_right, delta);
    }
}

static void od_fx_update(const odwd_frame *frame,
                         const odwd_vehicle_snapshot *player) {
    uint64_t tick_delta;
    float dt;
    uint32_t index;
    float yaw;
    float sine;
    float cosine;
    od_v3 rear_center;
    od_v3 rear_left;
    od_v3 rear_right;
    if (frame->tick < g_last_tick) od_fx_reset();
    tick_delta = frame->tick - g_last_tick;
    if (tick_delta == 0u) return;
    if (tick_delta > 60u) tick_delta = 60u;
    dt = (float)tick_delta / (float)ODWD_TICK_HZ;
    for (index = 0u; index < OD_RENDER_MAX_SKIDS; ++index)
        if (g_skids[index].life > 0.0f) g_skids[index].life -= dt;
    for (index = 0u; index < OD_RENDER_MAX_PARTICLES; ++index) {
        od_particle *particle = &g_particles[index];
        if (particle->life <= 0.0f) continue;
        particle->life -= dt;
        particle->position = od_v3_add(particle->position,
                                       od_v3_scale(particle->velocity, dt));
        particle->velocity.y += 0.45f * dt;
        particle->velocity = od_v3_scale(particle->velocity,
                                         fmaxf(0.0f, 1.0f - dt * 1.8f));
    }
    yaw = (float)player->heading_rad;
    sine = sinf(yaw);
    cosine = cosf(yaw);
    rear_center = od_world_v3(player->position_x - sine * 1.40f,
                              player->position_y - 0.545,
                              player->position_z - cosine * 1.40f);
    rear_left = od_v3_make(rear_center.x - cosine * 0.73f,
                            rear_center.y,
                            rear_center.z + sine * 0.73f);
    rear_right = od_v3_make(rear_center.x + cosine * 0.73f,
                             rear_center.y,
                             rear_center.z - sine * 0.73f);
    /* One deterministic visual burst at the authoritative PCM onset tick.
     * This is presentation only: it cannot feed back into simulation. */
    if ((frame->event_flags & ODWD_EVENT_MUSIC_BEAT) != 0u &&
        g_last_beat_tick != frame->tick) {
        uint32_t particle_number;
        float strength = od_clampf((float)frame->music_beat_01, 0.0f, 1.0f);
        od_v3 center = od_world_v3(player->position_x,
                                   player->position_y + 0.18,
                                   player->position_z);
        for (particle_number = 0u; particle_number < 12u; ++particle_number) {
            od_particle *particle =
                &g_particles[g_particle_cursor++ % OD_RENDER_MAX_PARTICLES];
            uint32_t seed = od_hash32((uint32_t)frame->tick ^
                                      particle_number * UINT32_C(0x9e3779b9));
            float angle = ((float)particle_number / 12.0f) * 2.0f * OD_PI +
                          od_hash01(seed) * 0.24f;
            float speed = 2.2f + strength * 3.4f + od_hash01(seed ^ 0xa53u);
            particle->position = center;
            particle->velocity = od_v3_make(cosf(angle) * speed,
                                             1.0f + strength * 1.5f,
                                             sinf(angle) * speed);
            particle->life = 0.30f + strength * 0.32f;
            particle->total_life = particle->life;
        }
        g_last_beat_tick = frame->tick;
    }
    if (player->drift_intensity > 0.10 && player->speed_mps > 8.0 &&
        g_last_rear_valid) {
        float left_distance = od_v3_length(od_v3_sub(rear_left,
                                                     g_last_rear_left));
        if (left_distance < 4.0f) {
            od_skid *left = &g_skids[g_skid_cursor++ % OD_RENDER_MAX_SKIDS];
            od_skid *right = &g_skids[g_skid_cursor++ % OD_RENDER_MAX_SKIDS];
            left->a = g_last_rear_left;
            left->b = rear_left;
            left->life = 8.0f;
            right->a = g_last_rear_right;
            right->b = rear_right;
            right->life = 8.0f;
        }
        if ((frame->tick & UINT64_C(3)) == 0u) {
            uint32_t seed = (uint32_t)frame->tick;
            uint32_t particle_number;
            for (particle_number = 0u; particle_number < 2u; ++particle_number) {
                od_particle *particle =
                    &g_particles[g_particle_cursor++ % OD_RENDER_MAX_PARTICLES];
                float side = particle_number == 0u ? -1.0f : 1.0f;
                float random = od_hash01(seed + particle_number * 977u);
                particle->position = particle_number == 0u ?
                                     rear_left : rear_right;
                particle->position.y += 0.12f;
                particle->velocity = od_v3_make(
                    -sine * (0.8f + random * 1.5f) + cosine * side * 0.28f,
                    0.75f + random * 0.70f,
                    -cosine * (0.8f + random * 1.5f) - sine * side * 0.28f);
                particle->life = 0.38f + random * 0.42f;
                particle->total_life = particle->life;
            }
        }
    }
    g_last_rear_left = rear_left;
    g_last_rear_right = rear_right;
    g_last_rear_valid = 1;
    g_last_tick = frame->tick;
}

static void od_render_skids(void) {
    uint32_t index;
    for (index = 0u; index < OD_RENDER_MAX_SKIDS; ++index) {
        const od_skid *skid = &g_skids[index];
        float fade;
        uint32_t color;
        if (skid->life <= 0.0f) continue;
        fade = od_clampf(skid->life / 8.0f, 0.0f, 1.0f);
        color = od_mix_color(COL_INK, COL_ROAD, 1.0f - fade * 0.72f);
        od_draw_line(skid->a, skid->b, color, 0);
    }
}

static void od_render_particles(void) {
    uint32_t index;
    for (index = 0u; index < OD_RENDER_MAX_PARTICLES; ++index) {
        const od_particle *particle = &g_particles[index];
        od_screen_vertex screen;
        float life;
        uint32_t color;
        int radius;
        if (particle->life <= 0.0f || !od_project(particle->position, &screen))
            continue;
        life = od_clampf(particle->life / particle->total_life, 0.0f, 1.0f);
        color = od_mix_color(COL_FOG_INK, COL_PAPER, 1.0f - life * 0.55f);
        radius = screen.camera_z < 18.0f ? 2 : 1;
        od_put_depth_pixel((int)screen.x, (int)screen.y,
                           screen.inverse_z, color, radius);
    }
}

static void od_render_motion_trails(const void *storage,
                                    const odwd_frame *frame,
                                    const odwd_vehicle_snapshot *player) {
    /* Deliberately no screen/world “speed scratches”.  They read as graphic
     * noise, can obscure obstacles, and add line raster work exactly when the
     * scene is already moving fastest. Speed is communicated by the physical
     * camera/FOV, suspension, road flow, particles and turbo state instead. */
    (void)storage;
    (void)frame;
    (void)player;
}

static void od_render_music_aura(const odwd_frame *frame,
                                 const odwd_vehicle_snapshot *player) {
    const float bands[3] = {(float)frame->music_bass_01,
                            (float)frame->music_mid_01,
                            (float)frame->music_high_01};
    uint32_t ring;
    if (frame->music_energy_01 < 0.035 && frame->music_pulse_01 < 0.035)
        return;
    for (ring = 0u; ring < 3u; ++ring) {
        uint32_t i;
        float band = bands[ring];
        float radius = 3.8f + (float)ring * 2.1f + band * 5.6f +
                       (float)frame->music_pulse_01 * (ring == 0u ? 2.8f : 1.4f);
        float height = (float)player->position_y - 0.48f +
                       (float)ring * 0.10f + band * 0.22f;
        uint32_t color = ring == 0u ? COL_INK :
                         ring == 1u ? COL_MID : COL_BRIGHT;
        for (i = 0u; i < 28u; ++i) {
            float a0 = (float)i / 28.0f * 2.0f * OD_PI;
            float a1 = (float)(i + 1u) / 28.0f * 2.0f * OD_PI;
            float wobble0 = 1.0f + 0.06f * sinf(a0 * (3.0f + (float)ring) +
                (float)frame->tick * (0.035f + 0.012f * (float)ring));
            float wobble1 = 1.0f + 0.06f * sinf(a1 * (3.0f + (float)ring) +
                (float)frame->tick * (0.035f + 0.012f * (float)ring));
            od_v3 a = od_world_v3(player->position_x + cosf(a0) * radius * wobble0,
                                  height,
                                  player->position_z + sinf(a0) * radius * wobble0);
            od_v3 b = od_world_v3(player->position_x + cosf(a1) * radius * wobble1,
                                  height,
                                  player->position_z + sinf(a1) * radius * wobble1);
            if (((i + ring) & 1u) == 0u || band > 0.42f)
                od_draw_line(a, b, color, band > 0.65f ? 1 : 0);
        }
    }

    /* Make the music legible even when the player is concentrating on the
     * road. Bass owns the ground impact, mids own vertical body, and high/flux
     * own a crown of short world-space sparks. None of these are screen-space
     * speed scratches, so obstacles remain readable and the effect follows
     * the car in 3D. */
    {
        uint32_t i;
        float bass = (float)frame->music_bass_01;
        float mid = (float)frame->music_mid_01;
        float high = (float)frame->music_high_01;
        float flux = (float)frame->music_flux_01;
        float beat = (float)frame->music_beat_01;
        float phase = (float)frame->music_beat_phase_01 * 2.0f * OD_PI;
        float crown_radius = 5.0f + mid * 2.7f;
        float base_y = (float)player->position_y - 0.42f;

        if (bass > 0.06f || beat > 0.10f) {
            float impact_radius = 2.8f + bass * 6.8f + beat * 4.6f;
            for (i = 0u; i < 36u; ++i) {
                float a0 = (float)i / 36.0f * 2.0f * OD_PI;
                float a1 = (float)(i + 1u) / 36.0f * 2.0f * OD_PI;
                od_v3 a = od_world_v3(player->position_x + cosf(a0) * impact_radius,
                                      base_y,
                                      player->position_z + sinf(a0) * impact_radius);
                od_v3 b = od_world_v3(player->position_x + cosf(a1) * impact_radius,
                                      base_y,
                                      player->position_z + sinf(a1) * impact_radius);
                if ((i & 1u) == 0u || bass > 0.50f)
                    od_draw_line(a, b, COL_INK, beat > 0.45f ? 1 : 0);
            }
        }

        if (mid > 0.05f) {
            for (i = 0u; i < 10u; ++i) {
                float angle = (float)i / 10.0f * 2.0f * OD_PI;
                float wave = 0.70f + 0.30f * sinf(phase + angle * 2.0f);
                float height = 0.55f + mid * (2.0f + 2.3f * wave);
                od_v3 low = od_world_v3(player->position_x + cosf(angle) * crown_radius,
                                        base_y + 0.05f,
                                        player->position_z + sinf(angle) * crown_radius);
                od_v3 top = low;
                top.y += height;
                od_draw_line(low, top, (i & 1u) ? COL_MID : COL_BRIGHT,
                             mid > 0.58f ? 1 : 0);
            }
        }

        if (high > 0.07f || flux > 0.09f) {
            float spark = 0.55f + high * 1.8f + flux * 1.4f;
            float radius = 6.3f + high * 2.2f;
            for (i = 0u; i < 12u; ++i) {
                float angle = (float)i / 12.0f * 2.0f * OD_PI + phase * 0.18f;
                od_v3 inner = od_world_v3(player->position_x + cosf(angle) * radius,
                                          (float)player->position_y + 0.8f +
                                          sinf(angle * 3.0f + phase) * 0.35f,
                                          player->position_z + sinf(angle) * radius);
                od_v3 outer = inner;
                outer.x += cosf(angle) * spark;
                outer.z += sinf(angle) * spark;
                outer.y += 0.45f + flux * 1.2f;
                od_draw_line(inner, outer, COL_BRIGHT, high > 0.62f ? 1 : 0);
            }
        }
    }
}

static uint32_t od_invert_color(uint32_t color) {
    return OD_COLOR(255u - (color & 255u),
                    255u - ((color >> 8u) & 255u),
                    255u - ((color >> 16u) & 255u));
}

static void od_postprocess_night(const odwd_frame *frame,
                                 const odwd_vehicle_snapshot *player) {
    float night = (float)frame->night_amount_01;
    uint32_t count = g_width * g_height;
    uint32_t index;
    if (night < 0.001f) return;
    for (index = 0u; index < count; ++index) {
        uint32_t x = index % g_width;
        uint32_t y = index / g_width;
        uint32_t day = g_framebuffer[index];
        float local_night = night;
        if (frame->headlights_on && night > 0.001f &&
            g_depth[index] > 1.0e-8f) {
            float camera_z = 1.0f / g_depth[index];
            float camera_x = ((float)x + 0.5f - (float)g_width * 0.5f) *
                             camera_z / g_view.focal;
            float camera_y = -((float)y + 0.5f - (float)g_height * 0.5f) *
                             camera_z / g_view.focal;
            od_v3 world = od_v3_add(g_view.position,
                od_v3_add(od_v3_scale(g_view.right, camera_x),
                od_v3_add(od_v3_scale(g_view.up, camera_y),
                          od_v3_scale(g_view.forward, camera_z))));
            float yaw = (float)player->heading_rad;
            float fx = sinf(yaw);
            float fz = cosf(yaw);
            float rx = cosf(yaw);
            float rz = -sinf(yaw);
            od_v3 lamp = od_world_v3(player->position_x + fx * 2.18,
                                     player->position_y + 0.12,
                                     player->position_z + fz * 2.18);
            float dx = world.x - lamp.x;
            float dy = world.y - lamp.y;
            float dz = world.z - lamp.z;
            float forward = dx * fx + dz * fz;
            float lateral = dx * rx + dz * rz;
            if (forward > -0.8f && forward < 155.0f) {
                float beam_side = fminf(od_absf(lateral - 0.54f),
                                        od_absf(lateral + 0.54f));
                float width = 1.18f + forward * 0.185f;
                float side = od_clampf(1.0f - beam_side / width,
                                       0.0f, 1.0f);
                float vertical = od_clampf(1.0f - od_absf(dy) /
                    (3.2f + forward * 0.065f), 0.0f, 1.0f);
                float near_fade = od_clampf((forward + 0.8f) / 5.0f,
                                            0.0f, 1.0f);
                float far_fade = od_clampf((155.0f - forward) / 48.0f,
                                           0.0f, 1.0f);
                float cone = side * side * vertical * near_fade * far_fade;
                local_night *= 1.0f - cone * 0.94f;
            }
        }
        g_framebuffer[index] = od_mix_color(day, od_invert_color(day),
                                             local_night);
    }
}

uint32_t od_renderer_render_storage(const void *storage, uint32_t quality) {
    odwd_frame frame;
    odwd_camera_snapshot camera;
    odwd_vehicle_snapshot player;
    uint32_t begin = 0u;
    uint32_t end;
    uint32_t index;
    float player_progress;
    if (!storage) return 0u;
    if (odwd_engine_read_frame(storage, &frame) != ODWD_OK ||
        odwd_engine_read_camera(storage, &camera) != ODWD_OK ||
        odwd_engine_read_vehicle(storage, 0u, &player) != ODWD_OK)
        return 0u;
    g_origin_x = (double)od_floor_i64_signed(camera.position_x / 512.0) * 512.0;
    g_origin_y = (double)od_floor_i64_signed(camera.position_y / 128.0) * 128.0;
    g_origin_z = (double)od_floor_i64_signed(camera.position_z / 512.0) * 512.0;
    if (g_origin_x != g_previous_origin_x ||
        g_origin_y != g_previous_origin_y ||
        g_origin_z != g_previous_origin_z) {
        od_fx_rebase((float)(g_previous_origin_x - g_origin_x),
                     (float)(g_previous_origin_y - g_origin_y),
                     (float)(g_previous_origin_z - g_origin_z));
        g_previous_origin_x = g_origin_x;
        g_previous_origin_y = g_origin_y;
        g_previous_origin_z = g_origin_z;
    }
    od_set_quality_dimensions(quality);
    if (frame.world_mode == ODWD_MODE_SURVIVAL ||
        frame.world_mode == ODWD_MODE_MUSIC_SURVIVAL)
        od_clear_frame_color(COL_CHALLENGE_SKY);
    else
        od_clear_frame();
    od_build_view(&camera);
    player_progress = (float)player.road_progress_m;
    if (frame.world_mode == ODWD_MODE_OPEN_FIELD) {
        od_render_open_ground(storage, &player);
        od_render_open_billboards(storage, &player);
    } else if (frame.world_mode == ODWD_MODE_SURVIVAL ||
               frame.world_mode == ODWD_MODE_MUSIC_SURVIVAL) {
        /* Finite challenge arenas are represented by authoritative props. A
         * cool paper-grey sky/void separates the arena from dark platforms
         * and light obstacles without abandoning WhiteLine's ink/paper style. */
    } else {
        if (od_load_road(storage) < 2u)
            return (uint32_t)(uintptr_t)g_framebuffer;
        while (begin + 1u < g_road_count &&
               g_road[begin + 1u].progress < player_progress - 960.0f) ++begin;
        end = begin;
        while (end < g_road_count &&
               g_road[end].progress < player_progress + 1840.0f) ++end;
        if (end < begin + 2u) end = begin + 2u;
        if (end > g_road_count) end = g_road_count;
        od_render_mountains(begin, end);
        od_render_track(begin, end);
        od_render_track_billboards(begin, end);
    }
    od_render_world_props(storage, &frame, &player);
    if (frame.world_mode == ODWD_MODE_MUSIC_SURVIVAL)
        od_render_music_boundary_billboards(storage);
    else if (frame.world_mode == ODWD_MODE_SURVIVAL)
        od_render_survival_billboards(storage);
    od_render_pickups(storage, &frame, &player);
    od_fx_update(&frame, &player);
    od_render_skids();
    for (index = frame.vehicle_count; index > 0u; --index) {
        odwd_vehicle_snapshot car;
        uint32_t car_index = index - 1u;
        if (odwd_engine_read_vehicle(storage, car_index, &car) == ODWD_OK)
            od_render_car(storage, &frame, &car, car_index);
    }
    od_render_particles();
    od_render_music_aura(&frame, &player);
    od_render_motion_trails(storage, &frame, &player);
    od_postprocess_night(&frame, &player);
    return (uint32_t)(uintptr_t)g_framebuffer;
}

uint32_t od_render(void) {
    od_renderer_set_context(0u);
    return od_renderer_render_storage(od_internal_storage(), od_get_quality());
}

void od_renderer_set_context(uint32_t context_id) {
    if (context_id >= 3u) context_id = 0u;
    g_fx = &g_fx_contexts[context_id];
}

uint32_t od_renderer_regression_probe(void) {
    odwd_camera_snapshot camera;
    od_screen_vertex left;
    od_screen_vertex right;
    uint32_t result = 0u;
    uint32_t index;
    uint32_t changed;
    uint32_t center;
    memset(&camera, 0, sizeof(camera));
    camera.position_x = 0.0;
    camera.position_y = 0.0;
    camera.position_z = 0.0;
    camera.target_x = 0.0;
    camera.target_y = 0.0;
    camera.target_z = 1.0;
    camera.vertical_fov_rad = 0.92;
    g_origin_x = g_origin_y = g_origin_z = 0.0;
    g_portrait = 0u;
    od_set_quality_dimensions(1u);
    od_clear_frame();
    od_build_view(&camera);

    if (od_project(od_v3_make(-1.0f, 0.0f, 5.0f), &left) &&
        od_project(od_v3_make(1.0f, 0.0f, 5.0f), &right) &&
        left.x < (float)g_width * 0.5f &&
        right.x > (float)g_width * 0.5f)
        result |= 1u;

    od_draw_triangle(od_v3_make(-0.03f, -0.03f, 0.04f),
                     od_v3_make(0.26f, -0.12f, 0.60f),
                     od_v3_make(0.0f, 0.25f, 0.60f), COL_INK);
    changed = 0u;
    for (index = 0u; index < g_width * g_height; ++index)
        if (g_framebuffer[index] != COL_PAPER) ++changed;
    if (changed > 12u) result |= 2u;

    od_clear_frame();
    od_draw_line(od_v3_make(-0.02f, 0.0f, 0.02f),
                 od_v3_make(0.22f, 0.0f, 0.75f), COL_INK, 0);
    changed = 0u;
    for (index = 0u; index < g_width * g_height; ++index)
        if (g_framebuffer[index] != COL_PAPER) ++changed;
    if (changed > 3u) result |= 4u;

    od_clear_frame();
    od_draw_triangle(od_v3_make(-0.7f, -0.6f, 2.0f),
                     od_v3_make(0.7f, -0.6f, 2.0f),
                     od_v3_make(0.0f, 0.7f, 2.0f), COL_INK);
    od_draw_triangle(od_v3_make(-1.0f, -0.9f, 3.0f),
                     od_v3_make(1.0f, -0.9f, 3.0f),
                     od_v3_make(0.0f, 1.0f, 3.0f), COL_BRIGHT);
    center = (g_height / 2u) * g_width + g_width / 2u;
    if (g_framebuffer[center] == COL_INK) result |= 8u;
    return result;
}

void od_set_view_orientation(uint32_t portrait) {
    g_portrait = portrait != 0u;
}

uint32_t od_framebuffer_ptr(void) {
    return (uint32_t)(uintptr_t)g_framebuffer;
}

uint32_t od_framebuffer_width(void) { return g_width; }
uint32_t od_framebuffer_height(void) { return g_height; }

uint64_t od_framebuffer_hash(void) {
    uint64_t hash = UINT64_C(1469598103934665603);
    uint32_t count = g_width * g_height;
    uint32_t index;
    for (index = 0u; index < count; ++index) {
        uint32_t value = g_framebuffer[index];
        unsigned byte;
        for (byte = 0u; byte < 4u; ++byte) {
            hash ^= (value >> (byte * 8u)) & UINT32_C(0xff);
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

const uint32_t *od_framebuffer_data(void) { return g_framebuffer; }
