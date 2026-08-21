#include "game_internal.h"

#include <stdint.h>

typedef struct { float x, y, z; } rv3;
typedef struct { float x, y, z; } cv3;
typedef struct { float sx, sy, z; int valid; } rpv;
typedef struct { cv3 p; float r,g,b; } ccv3;
typedef struct {
    float cam_x, cam_y, cam_z;
    float forward_x, forward_z;
    float right_x, right_z;
    float cp, sp;
    float focal;
    uint32_t w, h;
} rcam;

typedef struct {
    uint32_t sky_top,sky_horizon,fog,water,water_glint;
    uint32_t land_low,land_mid,land_high,coast,coast_edge,road;
    uint32_t building,building_alt,glass,rock,trunk,leaf_low,leaf_high;
    uint32_t neutral_turret,ammo,accent;
} visual_palette;

static uint32_t g_daylight_q8=256u;
static float g_daylight_f=1.0f;
static float g_sun_world_x=0.0f;
static float g_sun_world_z=1.0f;
static uint8_t g_depth_fog_amount[641];
static uint16_t g_depth_fog_r_term[97];
static uint16_t g_depth_fog_g_term[97];
static uint16_t g_depth_fog_b_term[97];
static uint32_t g_depth_fog_color=UINT32_MAX;
static uint32_t g_depth_fog_ready=0u;
/* Renderer-only floating origin. Normal gameplay matches the simulation center; a
 * remote artifact view rebases around that artifact without mutating gameplay state. */
static int64_t g_render_center_global_fx_x=0;
static int64_t g_render_center_global_fx_z=0;
static int64_t g_render_origin_cell_x=0;
static int64_t g_render_origin_cell_z=0;
static uint32_t g_render_remote_rebased=0u;

/* World objects remain physical 3D geometry, but short telemetry/captions are queued as
 * crisp screen-space labels after the scene has been postprocessed.  The old renderer
 * built every bitmap pixel from tiny 3D line segments; perspective collapsed those
 * segments into horizontal scratches above turrets and item cards.  This queue keeps
 * text a presentation concern while its anchor remains a real world position. */
#define ODG_RENDER_LABEL_MAX 96u
#define ODG_RENDER_LABEL_TEXT_MAX 24u
typedef struct {
    int32_t center_x,center_y;
    uint32_t fg,bg,border;
    uint8_t scale;
    char text[ODG_RENDER_LABEL_TEXT_MAX];
} odg_render_label;
static odg_render_label g_render_labels[ODG_RENDER_LABEL_MAX];
static uint32_t g_render_label_count=0u;

static float render_global_fx_local_f(int64_t value,int axis_x){
    int64_t center=axis_x?g_render_center_global_fx_x:g_render_center_global_fx_z;
    return (float)(value-center)/(float)ODG_FX_ONE;
}

static float render_global_cell_center_local_f(int64_t cell,int axis_x){
    int64_t fx=cell*(int64_t)ODG_FX_ONE+(int64_t)ODG_FX_ONE/2;
    return render_global_fx_local_f(fx,axis_x);
}

static int game_local_to_render_fx(int32_t x,int32_t z,int32_t *out_x,int32_t *out_z){
    int64_t global_x=odg_global_center_cell_x_internal()*(int64_t)ODG_FX_ONE+(int64_t)x;
    int64_t global_z=odg_global_center_cell_z_internal()*(int64_t)ODG_FX_ONE+(int64_t)z;
    int64_t rx=global_x-g_render_center_global_fx_x;
    int64_t rz=global_z-g_render_center_global_fx_z;
    if(out_x==NULL||out_z==NULL||rx<INT32_MIN||rx>INT32_MAX||rz<INT32_MIN||rz>INT32_MAX)return 0;
    *out_x=(int32_t)rx;*out_z=(int32_t)rz;return 1;
}

static visual_palette palette(void) {
    static const visual_palette p[ODG_VISUAL_THEME_COUNT] = {
        /* North Atlantic — mineral terrain, oxidised metal and cold air. */
        {0x142a3affu,0x8da4a4ffu,0x9ca9a6ffu,0x173946ffu,0x7eabb0ffu,
         0x3f604bffu,0x587258ffu,0x727d5effu,0x9d8f72ffu,0xd6c5a1ffu,0x3c484cffu,
         0x515e66ffu,0x68747affu,0x93b8bdffu,0x646967ffu,0x58483affu,0x355f46ffu,0x567958ffu,
         0xcbd4d7ffu,0xd7aa52ffu,0x72bdc8ffu},
        /* Temperate daylight — clear visibility without toy-like saturation. */
        {0x142b3dffu,0x87a4abffu,0x9dadaaffu,0x1c414bffu,0x82adb3ffu,
         0x445f4effu,0x557057ffu,0x707d60ffu,0xa39275ffu,0xd9c6a0ffu,0x414e52ffu,
         0x59656affu,0x6b777affu,0x99b9beffu,0x646964ffu,0x5d4c40ffu,0x496950ffu,0x617f61ffu,
         0xdce0dcffu,0xd8aa50ffu,0x96c4c0ffu},
        /* Copper hour — warm atmosphere over restrained olive and slate. */
        {0x1c2130ffu,0xa57a6cffu,0x9c8980ffu,0x293b49ffu,0xa27b6fffu,
         0x56604bffu,0x686f51ffu,0x827e5dffu,0xaa8460ffu,0xd7ad7cffu,0x4b474dffu,
         0x6a5e61ffu,0x7e6d70ffu,0xb99686ffu,0x6b6668ffu,0x664a3bffu,0x58674affu,0x737953ffu,
         0xe3d9d0ffu,0xd4a04fffu,0xd28b70ffu},
        /* Blackwater — near-black arena with one controlled signal colour. */
        {0x050b12ffu,0x26343effu,0x536169ffu,0x091a23ffu,0x2d5661ffu,
         0x203537ffu,0x2a4242ffu,0x3b504bffu,0x68635affu,0x9b8f7dffu,0x29343bffu,
         0x37444effu,0x49565effu,0x638b95ffu,0x4a5455ffu,0x49423bffu,0x2d5246ffu,0x3d6652ffu,
         0xc2ccd1ffu,0xcda052ffu,0x75a8baffu}
    };
    uint32_t t=g_odg.visual_theme;
    if (t>=ODG_VISUAL_THEME_COUNT) t=0u;
    return p[t];
}

static void prepare_depth_fog(void) {
    uint32_t fog=palette().fog;
    uint32_t i;
    if(g_depth_fog_ready==0u){
        /* Broad smootherstep fog: no visible distance band where haze suddenly starts.
         * 1/z is already the renderer's depth currency, so this table is generated once
         * and the hot fragment path stays division-free. */
        for(i=0u;i<=640u;++i){
            if(i<=176u)g_depth_fog_amount[i]=88u;
            else if(i>=640u)g_depth_fog_amount[i]=0u;
            else{
                uint32_t u=((i-176u)*256u)/(640u-176u);
                uint32_t smooth=(u*u*(768u-2u*u)+32768u)>>16u;
                uint32_t fog_amt=(88u*(256u-smooth)+128u)>>8u;
                g_depth_fog_amount[i]=(uint8_t)fog_amt;
            }
        }
        g_depth_fog_ready=1u;
    }
    if(g_depth_fog_color!=fog){
        uint32_t fr=(fog>>24)&255u,fg=(fog>>16)&255u,fb=(fog>>8)&255u;
        for(i=0u;i<=96u;++i){
            g_depth_fog_r_term[i]=(uint16_t)(fr*i);
            g_depth_fog_g_term[i]=(uint16_t)(fg*i);
            g_depth_fog_b_term[i]=(uint16_t)(fb*i);
        }
        g_depth_fog_color=fog;
    }
}

static uint32_t rgba_lerp(uint32_t a,uint32_t b,uint32_t t256) {
    uint32_t ar=(a>>24)&255u,ag=(a>>16)&255u,ab=(a>>8)&255u;
    uint32_t br=(b>>24)&255u,bg=(b>>16)&255u,bb=(b>>8)&255u;
    uint32_t inv=256u-t256;
    uint32_t r=(ar*inv+br*t256)>>8,g=(ag*inv+bg*t256)>>8,bl=(ab*inv+bb*t256)>>8;
    return (r<<24)|(g<<16)|(bl<<8)|0xffu;
}

static uint32_t biome_surface_color(uint32_t base,uint32_t biome,const visual_palette *p){
    if(p==NULL)return base;
    if(biome==ODG_BIOME_FOREST)return rgba_lerp(base,p->leaf_low,24u);
    if(biome==ODG_BIOME_ROCKY)return rgba_lerp(base,p->rock,34u);
    return base;
}

static uint32_t rgba_mix(uint32_t c, float k) {
    uint32_t r = (c >> 24) & 255u;
    uint32_t g = (c >> 16) & 255u;
    uint32_t b = (c >> 8) & 255u;
    uint32_t a = c & 255u;
    uint32_t rr = (uint32_t)((float)r * k);
    uint32_t gg = (uint32_t)((float)g * k);
    uint32_t bb = (uint32_t)((float)b * k);
    if (rr > 255u) rr = 255u;
    if (gg > 255u) gg = 255u;
    if (bb > 255u) bb = 255u;
    return (rr << 24) | (gg << 16) | (bb << 8) | a;
}

static float world_face_light(float nx,float nz,float base) {
    float directional=(nx*g_sun_world_x+nz*g_sun_world_z)*0.16f;
    float daylight=(g_daylight_f-0.70f)*0.10f;
    float out=base+directional+daylight;
    if(out<0.58f)out=0.58f;
    if(out>1.18f)out=1.18f;
    return out;
}

static float music_visual_beat(void) {
    float beat=(float)odg_music_beat_q16_internal()/65535.0f;
    float reactivity=(float)g_odg.music_reactivity_q16/65535.0f;
    beat*=reactivity;
    return beat>1.5f?1.5f:beat;
}

static uint32_t actor_base_color(uint32_t id) {
    /* Identity remains readable, but lives in the same mineral colour system as the
     * world instead of looking like fluorescent toy plastic. */
    static const uint32_t colors[ODG_MAX_ACTORS] = {
        0x58adbdffu, 0xc95d70ffu, 0xc58d4dffu, 0x8e74b1ffu, 0x55a27affu,
        0xb96691ffu, 0xc4a34fffu, 0x667faeffu, 0xbd6855ffu, 0x77a45bffu
    };
    return colors[id % ODG_MAX_ACTORS];
}

static uint32_t territory_color(uint32_t id,uint32_t terrain) {
    /* Ownership is a restrained mineral stain. The contour carries exact topology, so
     * the ground tint can stay subordinate to terrain relief/material instead of turning
     * a claimed district into a flat cyan/red floor. */
    uint32_t signal=actor_base_color(id);
    return rgba_lerp(terrain,signal,id==ODG_PLAYER_ID?42u:30u);
}


static uint32_t visual_hash2(uint32_t x,uint32_t z) {
    uint32_t v=x*UINT32_C(0x9e3779b1)^z*UINT32_C(0x85ebca6b)^UINT32_C(0x51ed270b);
    v^=v>>16u;v*=UINT32_C(0x7feb352d);v^=v>>15u;v*=UINT32_C(0x846ca68b);v^=v>>16u;
    return v;
}

static uint32_t visual_hash_cell64(int64_t x,int64_t z,uint32_t salt){
    uint64_t ux=(uint64_t)x,uz=(uint64_t)z;
    uint32_t hx=(uint32_t)ux^(uint32_t)(ux>>32u)^salt;
    uint32_t hz=(uint32_t)uz^(uint32_t)(uz>>32u)^(salt*UINT32_C(0x9e3779b9));
    return visual_hash2(hx,hz);
}

static uint32_t visual_smooth_noise_scale(int64_t x,int64_t z,uint32_t scale,uint32_t salt){
    int64_t bx,bz;uint32_t tx,tz,ix,iz,n00,n10,n01,n11,a,b;
    if(scale==0u)scale=1u;
    bx=odg_floor_div_i64_internal(x,(int64_t)scale);bz=odg_floor_div_i64_internal(z,(int64_t)scale);
    tx=(uint32_t)(x-bx*(int64_t)scale);tz=(uint32_t)(z-bz*(int64_t)scale);
    ix=scale-tx;iz=scale-tz;
    n00=visual_hash_cell64(bx,bz,salt)&255u;
    n10=visual_hash_cell64(bx+1,bz,salt)&255u;
    n01=visual_hash_cell64(bx,bz+1,salt)&255u;
    n11=visual_hash_cell64(bx+1,bz+1,salt)&255u;
    a=(n00*ix+n10*tx)/scale;
    b=(n01*ix+n11*tx)/scale;
    return (a*iz+b*tz)/scale;
}

static float smoothstep01(float t){
    if(t<=0.0f)return 0.0f;
    if(t>=1.0f)return 1.0f;
    return t*t*(3.0f-2.0f*t);
}

static uint32_t biome_cache_lookup(const uint32_t *cache,uint32_t cache_w,uint32_t cache_h,
                                   int64_t min_cx,int64_t min_cz,int64_t cx,int64_t cz){
    int64_t ix=cx-min_cx,iz=cz-min_cz;
    if(ix<0||iz<0||(uint64_t)ix>=cache_w||(uint64_t)iz>=cache_h)return ODG_BIOME_PLAIN;
    return cache[(uint32_t)iz*7u+(uint32_t)ix];
}

static uint32_t biome_surface_transition(uint32_t base,const visual_palette *p,
                                         const uint32_t *cache,uint32_t cache_w,uint32_t cache_h,
                                         int64_t min_cx,int64_t min_cz,int64_t world_gx,int64_t world_gz){
    const float band=(float)ODG_CHUNK_SIZE_CELLS*0.24f;
    const float inv_band=band>0.0f?1.0f/band:0.0f;
    const int64_t chunk_size=(int64_t)ODG_CHUNK_SIZE_CELLS;
    int64_t cx=odg_floor_div_i64_internal(world_gx,chunk_size),cz=odg_floor_div_i64_internal(world_gz,chunk_size);
    int64_t lx=world_gx-cx*chunk_size,lz=world_gz-cz*chunk_size;
    uint32_t out=biome_surface_color(base,biome_cache_lookup(cache,cache_w,cache_h,min_cx,min_cz,cx,cz),p);
    float tx_left=smoothstep01((band-(float)lx)*inv_band);
    float tx_right=smoothstep01(((float)lx-((float)ODG_CHUNK_SIZE_CELLS-band))*inv_band);
    float tz_up=smoothstep01((band-(float)lz)*inv_band);
    float tz_down=smoothstep01(((float)lz-((float)ODG_CHUNK_SIZE_CELLS-band))*inv_band);
    if(tx_left>0.0f){
        uint32_t nb=biome_surface_color(base,biome_cache_lookup(cache,cache_w,cache_h,min_cx,min_cz,cx-1,cz),p);
        out=rgba_lerp(out,nb,(uint32_t)(tx_left*112.0f));
    }
    if(tx_right>0.0f){
        uint32_t nb=biome_surface_color(base,biome_cache_lookup(cache,cache_w,cache_h,min_cx,min_cz,cx+1,cz),p);
        out=rgba_lerp(out,nb,(uint32_t)(tx_right*112.0f));
    }
    if(tz_up>0.0f){
        uint32_t nb=biome_surface_color(base,biome_cache_lookup(cache,cache_w,cache_h,min_cx,min_cz,cx,cz-1),p);
        out=rgba_lerp(out,nb,(uint32_t)(tz_up*112.0f));
    }
    if(tz_down>0.0f){
        uint32_t nb=biome_surface_color(base,biome_cache_lookup(cache,cache_w,cache_h,min_cx,min_cz,cx,cz+1),p);
        out=rgba_lerp(out,nb,(uint32_t)(tz_down*112.0f));
    }
    if((tx_left>0.0f||tx_right>0.0f)&&(tz_up>0.0f||tz_down>0.0f)){
        int64_t ncx=cx+(tx_right>tx_left?1:-1);
        int64_t ncz=cz+(tz_down>tz_up?1:-1);
        float corner=(tx_right>tx_left?tx_right:tx_left)*(tz_down>tz_up?tz_down:tz_up);
        uint32_t nb=biome_surface_color(base,biome_cache_lookup(cache,cache_w,cache_h,min_cx,min_cz,ncx,ncz),p);
        out=rgba_lerp(out,nb,(uint32_t)(corner*88.0f));
    }
    return out;
}

static float terrain_yf(float x, float z) {
    int64_t global_fx_x=g_render_center_global_fx_x+(int64_t)(x*(float)ODG_FX_ONE);
    int64_t global_fx_z=g_render_center_global_fx_z+(int64_t)(z*(float)ODG_FX_ONE);
    int64_t gx=odg_floor_div_i64_internal(global_fx_x,(int64_t)ODG_FX_ONE);
    int64_t gz=odg_floor_div_i64_internal(global_fx_z,(int64_t)ODG_FX_ONE);
    int64_t ox=global_fx_x-gx*(int64_t)ODG_FX_ONE;
    int64_t oz=global_fx_z-gz*(int64_t)ODG_FX_ONE;
    int32_t h00=420,h10=420,h01=420,h11=420;
    int64_t a,b,h;
    (void)odg_world_height_milli64(gx,gz,&h00);
    (void)odg_world_height_milli64(gx+1,gz,&h10);
    (void)odg_world_height_milli64(gx,gz+1,&h01);
    (void)odg_world_height_milli64(gx+1,gz+1,&h11);
    a=(int64_t)h00+(((int64_t)h10-h00)*ox)/(int64_t)ODG_FX_ONE;
    b=(int64_t)h01+(((int64_t)h11-h01)*ox)/(int64_t)ODG_FX_ONE;
    h=a+((b-a)*oz)/(int64_t)ODG_FX_ONE;
    {
        float raw=(float)h/1000.0f;
        /* Presentation-only relief amplification.  Gameplay/worldgen keep their exact
         * millimetre heights; the renderer increases readable landform depth around the
         * neutral 0.42m datum so Open Domain no longer looks like a flat board. */
        return 0.42f+(raw-0.42f)*1.85f;
    }
}

/* Reversed inverse-depth. Rasterization already interpolates 1/z, so v12 stores that
 * quantity directly instead of dividing back to z for every covered pixel. Larger values
 * are nearer. The mapping keeps useful precision across the 0.15..45m camera range. */
static uint16_t invz16(float inv_z) {
    int v=(int)(inv_z*9000.0f);
    if(v<1)v=1;
    if(v>65535)v=65535;
    return (uint16_t)v;
}

static uint32_t depth_fog_inv(uint32_t c,uint16_t inv_depth) {
    uint32_t t,near_weight;
    uint32_t r,g,b;
    /* Haze fades in smoothly from roughly 14m and reaches its restrained cap in the
     * far field. Amount and fog-weighted terms are cached outside the hot pixel path. */
    if((uint32_t)inv_depth>=640u)return c;
    t=g_depth_fog_amount[inv_depth];near_weight=256u-t;
    r=(c>>24)&255u;g=(c>>16)&255u;b=(c>>8)&255u;
    r=(r*near_weight+g_depth_fog_r_term[t])>>8;
    g=(g*near_weight+g_depth_fog_g_term[t])>>8;
    b=(b*near_weight+g_depth_fog_b_term[t])>>8;
    return (r<<24)|(g<<16)|(b<<8)|(c&255u);
}

static void put_px_index(uint32_t idx,uint32_t c,uint16_t inv_depth) {
    uint8_t *p;
    if(inv_depth<=g_odg_depth[idx])return;
    g_odg_depth[idx]=inv_depth;
    c=depth_fog_inv(c,inv_depth);
    p=&g_odg_framebuffer[idx*4u];
    p[0]=(uint8_t)(c>>24);p[1]=(uint8_t)(c>>16);p[2]=(uint8_t)(c>>8);p[3]=(uint8_t)c;
    ++g_odg.render_pixels_touched;
}

static void put_px(int x,int y,uint32_t c,uint16_t inv_depth) {
    uint32_t idx;
    if(x<0||y<0||x>=(int)g_odg.width||y>=(int)g_odg.height)return;
    idx=(uint32_t)y*g_odg.width+(uint32_t)x;
    put_px_index(idx,c,inv_depth);
}

static cv3 world_to_camera(const rcam *c, rv3 p) {
    float dx = p.x - c->cam_x;
    float dy = p.y - c->cam_y;
    float dz = p.z - c->cam_z;
    float local_x = dx * c->right_x + dz * c->right_z;
    float local_f = dx * c->forward_x + dz * c->forward_z;
    cv3 o;
    o.x = local_x;
    o.y = c->cp * dy + c->sp * local_f;
    o.z = -c->sp * dy + c->cp * local_f;
    return o;
}

static int world_point_maybe_visible(const rcam *c,float x,float z,float radius) {
    float dx=x-c->cam_x,dz=z-c->cam_z;
    float forward=dx*c->forward_x+dz*c->forward_z;
    float side=dx*c->right_x+dz*c->right_z;
    if(side<0.0f)side=-side;
    if(forward < -(radius+0.55f)) return 0;
    /* Conservative horizontal frustum guard. Gameplay uses ~80deg FOV and showcase is
     * slightly wider; 1.22 leaves generous safety for cell corners and near clipping. */
    if(forward>0.0f && side > forward*1.22f + radius + 1.40f) return 0;
    return 1;
}

static rpv project_camera(const rcam *c, cv3 p) {
    rpv o;
    o.valid = p.z > 0.15f;
    if (!o.valid) { o.sx = 0.0f; o.sy = 0.0f; o.z = 999.0f; return o; }
    o.sx = (float)c->w * 0.5f + p.x * c->focal / p.z;
    o.sy = (float)c->h * 0.43f - p.y * c->focal / p.z;
    o.z = p.z;
    return o;
}


static float edgef(float ax, float ay, float bx, float by, float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static cv3 clip_intersection(cv3 a, cv3 b, float near_z) {
    float denom = b.z - a.z;
    float t = denom != 0.0f ? (near_z - a.z) / denom : 0.0f;
    cv3 o;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    o.x = a.x + (b.x - a.x) * t;
    o.y = a.y + (b.y - a.y) * t;
    o.z = near_z;
    return o;
}

static uint32_t clip_near_triangle(const cv3 in[3], cv3 out[4]) {
    const float near_z = 0.151f;
    uint32_t count = 0u;
    uint32_t i;
    cv3 prev = in[2];
    int prev_inside = prev.z >= near_z;
    for (i = 0u; i < 3u; ++i) {
        cv3 cur = in[i];
        int cur_inside = cur.z >= near_z;
        if (cur_inside != 0) {
            if (prev_inside == 0 && count < 4u) out[count++] = clip_intersection(prev, cur, near_z);
            if (count < 4u) out[count++] = cur;
        } else if (prev_inside != 0 && count < 4u) {
            out[count++] = clip_intersection(prev, cur, near_z);
        }
        prev = cur;
        prev_inside = cur_inside;
    }
    return count;
}

static int float_ceil_to_int(float v) {
    int i=(int)v;
    if ((float)i<v) ++i;
    return i;
}

static int float_floor_to_int(float v) {
    int i=(int)v;
    if ((float)i>v) --i;
    return i;
}

static int edge_span_clip(float b,float a,int *lo,int *hi) {
    const float eps=0.00001f;
    if (a>eps) {
        int v=float_ceil_to_int((-b/a)-eps);
        if (v>*lo) *lo=v;
    } else if (a<-eps) {
        int v=float_floor_to_int((b/(-a))+eps);
        if (v<*hi) *hi=v;
    } else if (b<0.0f) {
        return 0;
    }
    return *lo<=*hi;
}

static void raster_camera_triangle(const rcam *c, cv3 a, cv3 b, cv3 d, uint32_t color) {
    rpv p0 = project_camera(c, a);
    rpv p1 = project_camera(c, b);
    rpv p2 = project_camera(c, d);
    float area;
    float minxf, maxxf, minyf, maxyf;
    float inv_area, iz0, iz1, iz2;
    float dw0dx, dw1dx, dw2dx, dw0dy, dw1dy, dw2dy;
    float row_w0, row_w1, row_w2;
    float row_invz, invz_dx, invz_dy;
    float sign;
    int minx, maxx, miny, maxy, y;
    if (!p0.valid || !p1.valid || !p2.valid) return;
    area = edgef(p0.sx, p0.sy, p1.sx, p1.sy, p2.sx, p2.sy);
    if (area > -0.02f && area < 0.02f) return;
    minxf = p0.sx; if (p1.sx < minxf) minxf = p1.sx; if (p2.sx < minxf) minxf = p2.sx;
    maxxf = p0.sx; if (p1.sx > maxxf) maxxf = p1.sx; if (p2.sx > maxxf) maxxf = p2.sx;
    minyf = p0.sy; if (p1.sy < minyf) minyf = p1.sy; if (p2.sy < minyf) minyf = p2.sy;
    maxyf = p0.sy; if (p1.sy > maxyf) maxyf = p1.sy; if (p2.sy > maxyf) maxyf = p2.sy;
    minx = (int)minxf; maxx = (int)maxxf + 1;
    miny = (int)minyf; maxy = (int)maxyf + 1;
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= (int)c->w) maxx = (int)c->w - 1;
    if (maxy >= (int)c->h) maxy = (int)c->h - 1;
    if (maxx < minx || maxy < miny) return;

    /* v12 scanline spans. Edge functions are affine, therefore each scanline can solve
     * the three half-plane inequalities once and rasterize only covered pixels.  The hot
     * inner loop then advances inverse depth with one add instead of testing three edges
     * and rebuilding barycentrics for every pixel in a triangle's bounding box. */
    inv_area = 1.0f / area;
    iz0 = 1.0f / p0.z; iz1 = 1.0f / p1.z; iz2 = 1.0f / p2.z;
    dw0dx = p2.sy - p1.sy; dw0dy = -(p2.sx - p1.sx);
    dw1dx = p0.sy - p2.sy; dw1dy = -(p0.sx - p2.sx);
    dw2dx = p1.sy - p0.sy; dw2dy = -(p1.sx - p0.sx);
    row_w0 = edgef(p1.sx,p1.sy,p2.sx,p2.sy,(float)minx+0.5f,(float)miny+0.5f);
    row_w1 = edgef(p2.sx,p2.sy,p0.sx,p0.sy,(float)minx+0.5f,(float)miny+0.5f);
    row_w2 = edgef(p0.sx,p0.sy,p1.sx,p1.sy,(float)minx+0.5f,(float)miny+0.5f);
    invz_dx=(dw0dx*iz0+dw1dx*iz1+dw2dx*iz2)*inv_area;
    invz_dy=(dw0dy*iz0+dw1dy*iz1+dw2dy*iz2)*inv_area;
    row_invz=(row_w0*iz0+row_w1*iz1+row_w2*iz2)*inv_area;
    sign=area>0.0f?1.0f:-1.0f;

    for (y=miny;y<=maxy;++y) {
        int lo=0,hi=maxx-minx;
        float b0=sign*row_w0,b1=sign*row_w1,b2=sign*row_w2;
        float a0=sign*dw0dx,a1=sign*dw1dx,a2=sign*dw2dx;
        if (edge_span_clip(b0,a0,&lo,&hi) && edge_span_clip(b1,a1,&lo,&hi) && edge_span_clip(b2,a2,&lo,&hi)) {
            int x=minx+lo;
            int xend=minx+hi;
            float inv_z=row_invz+(float)lo*invz_dx;
            uint32_t idx=(uint32_t)y*c->w+(uint32_t)x;
            for (;x<=xend;++x,++idx) {
                if (inv_z>0.0f) put_px_index(idx,color,invz16(inv_z));
                inv_z+=invz_dx;
            }
        }
        row_w0+=dw0dy;row_w1+=dw1dy;row_w2+=dw2dy;row_invz+=invz_dy;
    }
    ++g_odg.render_triangles;
}

static ccv3 clip_intersection_color(ccv3 a,ccv3 b,float near_z){
    float denom=b.p.z-a.p.z;
    float t=denom!=0.0f?(near_z-a.p.z)/denom:0.0f;
    ccv3 o;
    if(t<0.0f)t=0.0f;
    if(t>1.0f)t=1.0f;
    o.p.x=a.p.x+(b.p.x-a.p.x)*t;
    o.p.y=a.p.y+(b.p.y-a.p.y)*t;
    o.p.z=near_z;
    o.r=a.r+(b.r-a.r)*t;o.g=a.g+(b.g-a.g)*t;o.b=a.b+(b.b-a.b)*t;
    return o;
}

static uint32_t clip_near_triangle_color(const ccv3 in[3],ccv3 out[4]){
    const float near_z=0.151f;
    uint32_t count=0u,i;
    ccv3 prev=in[2];int prev_inside=prev.p.z>=near_z;
    for(i=0u;i<3u;++i){
        ccv3 cur=in[i];int cur_inside=cur.p.z>=near_z;
        if(cur_inside!=0){
            if(prev_inside==0&&count<4u)out[count++]=clip_intersection_color(prev,cur,near_z);
            if(count<4u)out[count++]=cur;
        }else if(prev_inside!=0&&count<4u)out[count++]=clip_intersection_color(prev,cur,near_z);
        prev=cur;prev_inside=cur_inside;
    }
    return count;
}

static int clamp_color_channel(int v){return v<0?0:(v>255?255:v);}

/* Terrain is the largest continuous surface in the frame. Flat colour per 1m quad made
 * the authoritative heightfield read like giant polygon wedges near the chase camera.
 * This scanline variant keeps the same geometry/depth path, but interpolates only RGB.
 * RGB derivatives are converted to 16.16 once per row/triangle, so the hot pixel loop
 * pays three integer additions rather than perspective-correct texture arithmetic. */
static void raster_camera_triangle_color(const rcam *c,ccv3 a,ccv3 b,ccv3 d){
    rpv p0=project_camera(c,a.p),p1=project_camera(c,b.p),p2=project_camera(c,d.p);
    float area,minxf,maxxf,minyf,maxyf,inv_area,iz0,iz1,iz2;
    float dw0dx,dw1dx,dw2dx,dw0dy,dw1dy,dw2dy;
    float row_w0,row_w1,row_w2,row_invz,invz_dx,invz_dy,sign;
    float row_r,row_g,row_b,drdx,dgdx,dbdx,drdy,dgdy,dbdy;
    int minx,maxx,miny,maxy,y;
    if(!p0.valid||!p1.valid||!p2.valid)return;
    area=edgef(p0.sx,p0.sy,p1.sx,p1.sy,p2.sx,p2.sy);
    if(area>-0.02f&&area<0.02f)return;
    minxf=p0.sx;if(p1.sx<minxf)minxf=p1.sx;if(p2.sx<minxf)minxf=p2.sx;
    maxxf=p0.sx;if(p1.sx>maxxf)maxxf=p1.sx;if(p2.sx>maxxf)maxxf=p2.sx;
    minyf=p0.sy;if(p1.sy<minyf)minyf=p1.sy;if(p2.sy<minyf)minyf=p2.sy;
    maxyf=p0.sy;if(p1.sy>maxyf)maxyf=p1.sy;if(p2.sy>maxyf)maxyf=p2.sy;
    minx=(int)minxf;maxx=(int)maxxf+1;miny=(int)minyf;maxy=(int)maxyf+1;
    if(minx<0)minx=0;
    if(miny<0)miny=0;
    if(maxx>=(int)c->w)maxx=(int)c->w-1;
    if(maxy>=(int)c->h)maxy=(int)c->h-1;
    if(maxx<minx||maxy<miny)return;
    inv_area=1.0f/area;iz0=1.0f/p0.z;iz1=1.0f/p1.z;iz2=1.0f/p2.z;
    dw0dx=p2.sy-p1.sy;dw0dy=-(p2.sx-p1.sx);
    dw1dx=p0.sy-p2.sy;dw1dy=-(p0.sx-p2.sx);
    dw2dx=p1.sy-p0.sy;dw2dy=-(p1.sx-p0.sx);
    row_w0=edgef(p1.sx,p1.sy,p2.sx,p2.sy,(float)minx+0.5f,(float)miny+0.5f);
    row_w1=edgef(p2.sx,p2.sy,p0.sx,p0.sy,(float)minx+0.5f,(float)miny+0.5f);
    row_w2=edgef(p0.sx,p0.sy,p1.sx,p1.sy,(float)minx+0.5f,(float)miny+0.5f);
    invz_dx=(dw0dx*iz0+dw1dx*iz1+dw2dx*iz2)*inv_area;
    invz_dy=(dw0dy*iz0+dw1dy*iz1+dw2dy*iz2)*inv_area;
    row_invz=(row_w0*iz0+row_w1*iz1+row_w2*iz2)*inv_area;
    drdx=(dw0dx*a.r+dw1dx*b.r+dw2dx*d.r)*inv_area;
    dgdx=(dw0dx*a.g+dw1dx*b.g+dw2dx*d.g)*inv_area;
    dbdx=(dw0dx*a.b+dw1dx*b.b+dw2dx*d.b)*inv_area;
    drdy=(dw0dy*a.r+dw1dy*b.r+dw2dy*d.r)*inv_area;
    dgdy=(dw0dy*a.g+dw1dy*b.g+dw2dy*d.g)*inv_area;
    dbdy=(dw0dy*a.b+dw1dy*b.b+dw2dy*d.b)*inv_area;
    row_r=(row_w0*a.r+row_w1*b.r+row_w2*d.r)*inv_area;
    row_g=(row_w0*a.g+row_w1*b.g+row_w2*d.g)*inv_area;
    row_b=(row_w0*a.b+row_w1*b.b+row_w2*d.b)*inv_area;
    sign=area>0.0f?1.0f:-1.0f;
    for(y=miny;y<=maxy;++y){
        int lo=0,hi=maxx-minx;
        float b0=sign*row_w0,b1=sign*row_w1,b2=sign*row_w2;
        float a0=sign*dw0dx,a1=sign*dw1dx,a2=sign*dw2dx;
        if(edge_span_clip(b0,a0,&lo,&hi)&&edge_span_clip(b1,a1,&lo,&hi)&&edge_span_clip(b2,a2,&lo,&hi)){
            int x=minx+lo,xend=minx+hi;
            float inv_z=row_invz+(float)lo*invz_dx;
            int32_t rf=(int32_t)((row_r+(float)lo*drdx)*65536.0f);
            int32_t gf=(int32_t)((row_g+(float)lo*dgdx)*65536.0f);
            int32_t bf=(int32_t)((row_b+(float)lo*dbdx)*65536.0f);
            int32_t rdf=(int32_t)(drdx*65536.0f),gdf=(int32_t)(dgdx*65536.0f),bdf=(int32_t)(dbdx*65536.0f);
            uint32_t idx=(uint32_t)y*c->w+(uint32_t)x;
            for(;x<=xend;++x,++idx){
                if(inv_z>0.0f){
                    int rr=clamp_color_channel(rf>>16),gg=clamp_color_channel(gf>>16),bb=clamp_color_channel(bf>>16);
                    put_px_index(idx,((uint32_t)rr<<24)|((uint32_t)gg<<16)|((uint32_t)bb<<8)|255u,invz16(inv_z));
                }
                inv_z+=invz_dx;rf+=rdf;gf+=gdf;bf+=bdf;
            }
        }
        row_w0+=dw0dy;row_w1+=dw1dy;row_w2+=dw2dy;row_invz+=invz_dy;
        row_r+=drdy;row_g+=dgdy;row_b+=dbdy;
    }
    ++g_odg.render_triangles;
}

static ccv3 colored_camera_vertex(const rcam *c,rv3 p,uint32_t color){
    ccv3 o;o.p=world_to_camera(c,p);o.r=(float)((color>>24)&255u);o.g=(float)((color>>16)&255u);o.b=(float)((color>>8)&255u);return o;
}

static void tri_color(const rcam *c,rv3 a,uint32_t ca,rv3 b,uint32_t cb,rv3 d,uint32_t cd){
    ccv3 input[3]={colored_camera_vertex(c,a,ca),colored_camera_vertex(c,b,cb),colored_camera_vertex(c,d,cd)};
    ccv3 clipped[4];uint32_t n=clip_near_triangle_color(input,clipped);
    if(n<3u)return;
    raster_camera_triangle_color(c,clipped[0],clipped[1],clipped[2]);
    if(n==4u)raster_camera_triangle_color(c,clipped[0],clipped[2],clipped[3]);
}

static void quad_color(const rcam *c,rv3 a,uint32_t ca,rv3 b,uint32_t cb,rv3 d,uint32_t cd,rv3 e,uint32_t ce){
    tri_color(c,a,ca,b,cb,d,cd);tri_color(c,a,ca,d,cd,e,ce);
}

static void tri(const rcam *c, rv3 a, rv3 b, rv3 d, uint32_t color) {
    cv3 input[3] = { world_to_camera(c, a), world_to_camera(c, b), world_to_camera(c, d) };
    cv3 clipped[4];
    uint32_t n = clip_near_triangle(input, clipped);
    if (n < 3u) return;
    raster_camera_triangle(c, clipped[0], clipped[1], clipped[2], color);
    if (n == 4u) raster_camera_triangle(c, clipped[0], clipped[2], clipped[3], color);
}

static void line_overlay(const rcam *c, rv3 a, rv3 b, uint32_t color) {
    const float near_z=0.151f;
    cv3 ca=world_to_camera(c,a),cb=world_to_camera(c,b);
    rpv p0,p1;
    float invz,invz_step;
    int x0,y0,x1,y1,dx,dy,sx,sy,err,e2,steps;
    if (ca.z<near_z && cb.z<near_z) return;
    if (ca.z<near_z) ca=clip_intersection(ca,cb,near_z);
    if (cb.z<near_z) cb=clip_intersection(cb,ca,near_z);
    p0=project_camera(c,ca);p1=project_camera(c,cb);
    if(!p0.valid||!p1.valid) return;
    x0=(int)p0.sx;y0=(int)p0.sy;x1=(int)p1.sx;y1=(int)p1.sy;
    dx=odg_abs_i32(x1-x0);dy=-odg_abs_i32(y1-y0);
    sx=x0<x1?1:-1;sy=y0<y1?1:-1;err=dx+dy;
    steps=odg_abs_i32(x1-x0);
    if(odg_abs_i32(y1-y0)>steps) steps=odg_abs_i32(y1-y0);
    if(steps<1) steps=1;
    invz=1.0f/p0.z;
    invz_step=((1.0f/p1.z)-invz)/(float)steps;
    for(;;){
        if(invz>0.0f) put_px(x0,y0,color,invz16(invz));
        if(x0==x1&&y0==y1)break;
        e2=2*err;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}
        invz+=invz_step;
    }
}

static void quad(const rcam *c, rv3 a, rv3 b, rv3 d, rv3 e, uint32_t color) {
    tri(c, a, b, d, color);
    tri(c, a, d, e, color);
}


static rv3 oriented_point(float x,float z,float fx,float fz,float local_x,float local_z,float y);

static void put_px_textured_index(uint32_t idx,uint32_t color,uint16_t inv_depth) {
    uint32_t alpha=color&255u;
    uint8_t *dst;
    if(alpha==0u || inv_depth<=g_odg_depth[idx]) return;
    color=depth_fog_inv(color,inv_depth);
    dst=&g_odg_framebuffer[idx*4u];
    if(alpha<255u){
        uint32_t inv=255u-alpha;
        uint32_t r=(((color>>24u)&255u)*alpha+(uint32_t)dst[0]*inv)/255u;
        uint32_t g=(((color>>16u)&255u)*alpha+(uint32_t)dst[1]*inv)/255u;
        uint32_t b=(((color>>8u)&255u)*alpha+(uint32_t)dst[2]*inv)/255u;
        dst[0]=(uint8_t)r;dst[1]=(uint8_t)g;dst[2]=(uint8_t)b;dst[3]=255u;
        if(alpha>=128u) g_odg_depth[idx]=inv_depth;
    } else {
        g_odg_depth[idx]=inv_depth;
        dst[0]=(uint8_t)(color>>24u);dst[1]=(uint8_t)(color>>16u);dst[2]=(uint8_t)(color>>8u);dst[3]=255u;
    }
    ++g_odg.render_pixels_touched;
}

/* Small translucent geometry path used for terrain-following contact shadows. It shares
 * the normal near clip/depth test, but alpha below 128 intentionally does not claim the
 * z-buffer in put_px_textured_index(). Later physical geometry therefore remains fully
 * authoritative while the shadow naturally inherits whatever terrain/territory colour is
 * already in the framebuffer. */
static void raster_camera_triangle_alpha(const rcam *c,cv3 a,cv3 b,cv3 d,uint32_t color){
    rpv p0=project_camera(c,a),p1=project_camera(c,b),p2=project_camera(c,d);
    float area,minxf,maxxf,minyf,maxyf,inv_area,iz0,iz1,iz2;
    float dw0dx,dw1dx,dw2dx,dw0dy,dw1dy,dw2dy;
    float row_w0,row_w1,row_w2,row_invz,invz_dx,invz_dy,sign;
    int minx,maxx,miny,maxy,y;
    if(!p0.valid||!p1.valid||!p2.valid)return;
    area=edgef(p0.sx,p0.sy,p1.sx,p1.sy,p2.sx,p2.sy);
    if(area>-0.02f&&area<0.02f)return;
    minxf=p0.sx;if(p1.sx<minxf)minxf=p1.sx;if(p2.sx<minxf)minxf=p2.sx;
    maxxf=p0.sx;if(p1.sx>maxxf)maxxf=p1.sx;if(p2.sx>maxxf)maxxf=p2.sx;
    minyf=p0.sy;if(p1.sy<minyf)minyf=p1.sy;if(p2.sy<minyf)minyf=p2.sy;
    maxyf=p0.sy;if(p1.sy>maxyf)maxyf=p1.sy;if(p2.sy>maxyf)maxyf=p2.sy;
    minx=(int)minxf;maxx=(int)maxxf+1;miny=(int)minyf;maxy=(int)maxyf+1;
    if(minx<0)minx=0;
    if(miny<0)miny=0;
    if(maxx>=(int)c->w)maxx=(int)c->w-1;
    if(maxy>=(int)c->h)maxy=(int)c->h-1;
    if(maxx<minx||maxy<miny)return;
    inv_area=1.0f/area;iz0=1.0f/p0.z;iz1=1.0f/p1.z;iz2=1.0f/p2.z;
    dw0dx=p2.sy-p1.sy;dw0dy=-(p2.sx-p1.sx);
    dw1dx=p0.sy-p2.sy;dw1dy=-(p0.sx-p2.sx);
    dw2dx=p1.sy-p0.sy;dw2dy=-(p1.sx-p0.sx);
    row_w0=edgef(p1.sx,p1.sy,p2.sx,p2.sy,(float)minx+0.5f,(float)miny+0.5f);
    row_w1=edgef(p2.sx,p2.sy,p0.sx,p0.sy,(float)minx+0.5f,(float)miny+0.5f);
    row_w2=edgef(p0.sx,p0.sy,p1.sx,p1.sy,(float)minx+0.5f,(float)miny+0.5f);
    invz_dx=(dw0dx*iz0+dw1dx*iz1+dw2dx*iz2)*inv_area;
    invz_dy=(dw0dy*iz0+dw1dy*iz1+dw2dy*iz2)*inv_area;
    row_invz=(row_w0*iz0+row_w1*iz1+row_w2*iz2)*inv_area;
    sign=area>0.0f?1.0f:-1.0f;
    for(y=miny;y<=maxy;++y){
        int lo=0,hi=maxx-minx;
        float b0=sign*row_w0,b1=sign*row_w1,b2=sign*row_w2;
        float a0=sign*dw0dx,a1=sign*dw1dx,a2=sign*dw2dx;
        if(edge_span_clip(b0,a0,&lo,&hi)&&edge_span_clip(b1,a1,&lo,&hi)&&edge_span_clip(b2,a2,&lo,&hi)){
            int x=minx+lo,xend=minx+hi;float inv_z=row_invz+(float)lo*invz_dx;
            uint32_t idx=(uint32_t)y*c->w+(uint32_t)x;
            for(;x<=xend;++x,++idx){if(inv_z>0.0f)put_px_textured_index(idx,color,invz16(inv_z));inv_z+=invz_dx;}
        }
        row_w0+=dw0dy;row_w1+=dw1dy;row_w2+=dw2dy;row_invz+=invz_dy;
    }
    ++g_odg.render_triangles;
}

static void tri_alpha(const rcam *c,rv3 a,rv3 b,rv3 d,uint32_t color){
    cv3 input[3]={world_to_camera(c,a),world_to_camera(c,b),world_to_camera(c,d)};
    cv3 clipped[4];uint32_t n=clip_near_triangle(input,clipped);
    if(n<3u)return;
    raster_camera_triangle_alpha(c,clipped[0],clipped[1],clipped[2],color);
    if(n==4u)raster_camera_triangle_alpha(c,clipped[0],clipped[2],clipped[3],color);
}

static void raster_avatar_triangle(const rcam *c,uint32_t actor_id,uint32_t face,
                                   rv3 wa,float ua,float va,rv3 wb,float ub,float vb,
                                   rv3 wd,float ud,float vd,float light,uint32_t fallback) {
    cv3 a=world_to_camera(c,wa),b=world_to_camera(c,wb),d=world_to_camera(c,wd);
    rpv p0,p1,p2;float area,inv_area,iz0,iz1,iz2,uiz0,uiz1,uiz2,viz0,viz1,viz2;
    int minx,maxx,miny,maxy,x,y;
    if(a.z<=0.151f||b.z<=0.151f||d.z<=0.151f){tri(c,wa,wb,wd,fallback);return;}
    p0=project_camera(c,a);p1=project_camera(c,b);p2=project_camera(c,d);
    area=edgef(p0.sx,p0.sy,p1.sx,p1.sy,p2.sx,p2.sy);
    if(area>-0.00001f&&area<0.00001f)return;
    minx=float_floor_to_int(p0.sx);maxx=float_ceil_to_int(p0.sx);miny=float_floor_to_int(p0.sy);maxy=float_ceil_to_int(p0.sy);
    if(p1.sx<(float)minx) minx=float_floor_to_int(p1.sx);
    if(p1.sx>(float)maxx) maxx=float_ceil_to_int(p1.sx);
    if(p2.sx<(float)minx) minx=float_floor_to_int(p2.sx);
    if(p2.sx>(float)maxx) maxx=float_ceil_to_int(p2.sx);
    if(p1.sy<(float)miny) miny=float_floor_to_int(p1.sy);
    if(p1.sy>(float)maxy) maxy=float_ceil_to_int(p1.sy);
    if(p2.sy<(float)miny) miny=float_floor_to_int(p2.sy);
    if(p2.sy>(float)maxy) maxy=float_ceil_to_int(p2.sy);
    if(minx<0) minx=0;
    if(miny<0) miny=0;
    if(maxx>=(int)c->w) maxx=(int)c->w-1;
    if(maxy>=(int)c->h) maxy=(int)c->h-1;
    if(minx>maxx||miny>maxy)return;
    {
        uint32_t span=(uint32_t)(((maxx-minx)>(maxy-miny)?(maxx-minx):(maxy-miny))+1),lod=0u;
        while(lod<8u && (ODG_AVATAR_TEXTURE_SIZE>>lod)>span*2u)++lod;
        inv_area=1.0f/area;iz0=1.0f/a.z;iz1=1.0f/b.z;iz2=1.0f/d.z;
        uiz0=ua*iz0;uiz1=ub*iz1;uiz2=ud*iz2;viz0=va*iz0;viz1=vb*iz1;viz2=vd*iz2;
        for(y=miny;y<=maxy;++y)for(x=minx;x<=maxx;++x){
        float px=(float)x+0.5f,py=(float)y+0.5f;
        float w0=edgef(p1.sx,p1.sy,p2.sx,p2.sy,px,py)*inv_area;
        float w1=edgef(p2.sx,p2.sy,p0.sx,p0.sy,px,py)*inv_area;
        float w2=1.0f-w0-w1;
        float invz,u,v;uint32_t uq,vq,sample,idx;
        if(w0<-0.0001f||w1<-0.0001f||w2<-0.0001f)continue;
        invz=w0*iz0+w1*iz1+w2*iz2;if(invz<=0.0f)continue;
        u=(w0*uiz0+w1*uiz1+w2*uiz2)/invz;v=(w0*viz0+w1*viz1+w2*viz2)/invz;
        if(u<0.0f) u=0.0f;
        if(u>1.0f) u=1.0f;
        if(v<0.0f) v=0.0f;
        if(v>1.0f) v=1.0f;
        uq=(uint32_t)(u*65535.0f);vq=(uint32_t)(v*65535.0f);
        sample=odg_avatar_texture_sample_lod_internal(actor_id,face,uq,vq,lod);
        if(sample==0u) sample=fallback;
        sample=rgba_mix(sample,light);
        idx=(uint32_t)y*c->w+(uint32_t)x;put_px_textured_index(idx,sample,invz16(invz));
        }
    }
    ++g_odg.render_triangles;
}

static void avatar_face(const rcam *c,uint32_t actor_id,uint32_t face,
                        rv3 a,rv3 b,rv3 d,rv3 e,float light,uint32_t fallback) {
    int textured=actor_id!=ODG_PLAYER_ID || odg_avatar_texture_present(face)!=0u;
    if(!textured){
        uint32_t lit=rgba_mix(fallback,light);
        if(face==ODG_AVATAR_FACE_TOP){quad(c,a,b,d,e,lit);}
        else{
            uint32_t lower=rgba_mix(lit,0.92f),upper=rgba_mix(lit,1.055f);
            quad_color(c,a,lower,b,lower,d,upper,e,upper);
        }
        return;
    }
    raster_avatar_triangle(c,actor_id,face,a,0.0f,1.0f,b,1.0f,1.0f,d,1.0f,0.0f,light,fallback);
    raster_avatar_triangle(c,actor_id,face,a,0.0f,1.0f,d,1.0f,0.0f,e,0.0f,0.0f,light,fallback);
}

typedef struct { int32_t rx,ry,rz,ux,uy,uz,fx,fy,fz; } render_surface_basis;

static render_surface_basis render_basis_at(int32_t x_fx,int32_t z_fx,int32_t face_x,int32_t face_z){
    render_surface_basis b;int32_t dot,fx,fy,fz;uint64_t m2;uint32_t mag;
    if(!odg_environment_normal_local_q15(x_fx,z_fx,&b.ux,&b.uy,&b.uz)){b.ux=0;b.uy=ODG_Q15_ONE;b.uz=0;}
    if(face_x==0&&face_z==0){face_x=0;face_z=ODG_Q15_ONE;}
    dot=(int32_t)(((int64_t)face_x*b.ux+(int64_t)face_z*b.uz)/ODG_Q15_ONE);
    fx=face_x-(int32_t)(((int64_t)b.ux*dot)/ODG_Q15_ONE);
    fy=-(int32_t)(((int64_t)b.uy*dot)/ODG_Q15_ONE);
    fz=face_z-(int32_t)(((int64_t)b.uz*dot)/ODG_Q15_ONE);
    m2=(uint64_t)((int64_t)fx*fx+(int64_t)fy*fy+(int64_t)fz*fz);mag=odg_isqrt_u64(m2);if(mag==0u)mag=1u;
    b.fx=(int32_t)(((int64_t)fx*ODG_Q15_ONE)/(int64_t)mag);b.fy=(int32_t)(((int64_t)fy*ODG_Q15_ONE)/(int64_t)mag);b.fz=(int32_t)(((int64_t)fz*ODG_Q15_ONE)/(int64_t)mag);
    b.rx=(int32_t)(((int64_t)b.uy*b.fz-(int64_t)b.uz*b.fy)/ODG_Q15_ONE);
    b.ry=(int32_t)(((int64_t)b.uz*b.fx-(int64_t)b.ux*b.fz)/ODG_Q15_ONE);
    b.rz=(int32_t)(((int64_t)b.ux*b.fy-(int64_t)b.uy*b.fx)/ODG_Q15_ONE);
    return b;
}

static rv3 basis_point(rv3 center,const render_surface_basis *b,float r,float u,float f){
    rv3 v=center;
    v.x+=((float)b->rx*r+(float)b->ux*u+(float)b->fx*f)/(float)ODG_Q15_ONE;
    v.y+=((float)b->ry*r+(float)b->uy*u+(float)b->fy*f)/(float)ODG_Q15_ONE;
    v.z+=((float)b->rz*r+(float)b->uz*u+(float)b->fz*f)/(float)ODG_Q15_ONE;
    return v;
}

static void avatar_cube(const rcam *c,const odg_actor *a,float x,float z,float y0,float h,uint32_t base) {
    render_surface_basis basis=render_basis_at(a->x,a->z,a->face_x_q15,a->face_z_q15);
    rv3 v[8];float half=h*0.5f;
    rv3 center={x,y0,z};
    /* y0 is the terrain contact point plus jump offset. Move the cube half a body along
     * the terrain normal so its bottom face rests on the same physical tangent plane. */
    center=basis_point(center,&basis,0.0f,half,0.0f);
    v[0]=basis_point(center,&basis,-half,-half,-half);v[1]=basis_point(center,&basis, half,-half,-half);
    v[2]=basis_point(center,&basis, half,-half, half);v[3]=basis_point(center,&basis,-half,-half, half);
    v[4]=basis_point(center,&basis,-half, half,-half);v[5]=basis_point(center,&basis, half, half,-half);
    v[6]=basis_point(center,&basis, half, half, half);v[7]=basis_point(center,&basis,-half, half, half);
    avatar_face(c,a->id,ODG_AVATAR_FACE_TOP,v[4],v[5],v[6],v[7],1.04f+0.14f*g_daylight_f,base);
    avatar_face(c,a->id,ODG_AVATAR_FACE_BACK,v[0],v[1],v[5],v[4],world_face_light(-(float)basis.fx/(float)ODG_Q15_ONE,-(float)basis.fz/(float)ODG_Q15_ONE,0.78f),base);
    avatar_face(c,a->id,ODG_AVATAR_FACE_RIGHT,v[1],v[2],v[6],v[5],world_face_light((float)basis.rx/(float)ODG_Q15_ONE,(float)basis.rz/(float)ODG_Q15_ONE,0.82f),base);
    avatar_face(c,a->id,ODG_AVATAR_FACE_FRONT,v[2],v[3],v[7],v[6],world_face_light((float)basis.fx/(float)ODG_Q15_ONE,(float)basis.fz/(float)ODG_Q15_ONE,0.90f),base);
    avatar_face(c,a->id,ODG_AVATAR_FACE_LEFT,v[3],v[0],v[4],v[7],world_face_light(-(float)basis.rx/(float)ODG_Q15_ONE,-(float)basis.rz/(float)ODG_Q15_ONE,0.82f),base);
}

static void surface_box(const rcam *c,int32_t x_fx,int32_t z_fx,int32_t face_x,int32_t face_z,
                        float local_r,float local_u,float local_f,float hx,float hz,float h,
                        float y_offset,uint32_t color){
    render_surface_basis basis=render_basis_at(x_fx,z_fx,face_x,face_z);
    float x=odg_fx_to_float(x_fx),z=odg_fx_to_float(z_fx),gy=terrain_yf(x,z);
    rv3 center={x,gy+y_offset,z};rv3 v[8];
    center=basis_point(center,&basis,local_r,local_u+h*0.5f,local_f);
    v[0]=basis_point(center,&basis,-hx,-h*0.5f,-hz);v[1]=basis_point(center,&basis,hx,-h*0.5f,-hz);
    v[2]=basis_point(center,&basis,hx,-h*0.5f,hz);v[3]=basis_point(center,&basis,-hx,-h*0.5f,hz);
    v[4]=basis_point(center,&basis,-hx,h*0.5f,-hz);v[5]=basis_point(center,&basis,hx,h*0.5f,-hz);
    v[6]=basis_point(center,&basis,hx,h*0.5f,hz);v[7]=basis_point(center,&basis,-hx,h*0.5f,hz);
    quad(c,v[4],v[5],v[6],v[7],rgba_mix(color,1.12f));
    quad(c,v[0],v[1],v[5],v[4],rgba_mix(color,0.77f));
    quad(c,v[1],v[2],v[6],v[5],rgba_mix(color,0.90f));
    quad(c,v[2],v[3],v[7],v[6],color);
    quad(c,v[3],v[0],v[4],v[7],rgba_mix(color,0.83f));
}

static void box_y(const rcam *c, float x, float z, float hx, float hz, float y0, float h, uint32_t base) {
    rv3 v[8] = {
        {x-hx,y0,z-hz},{x+hx,y0,z-hz},{x+hx,y0,z+hz},{x-hx,y0,z+hz},
        {x-hx,y0+h,z-hz},{x+hx,y0+h,z-hz},{x+hx,y0+h,z+hz},{x-hx,y0+h,z+hz}
    };
    uint32_t top = rgba_mix(base,1.04f+0.14f*g_daylight_f);
    uint32_t north = rgba_mix(base,world_face_light(0.0f,-1.0f,0.78f));
    uint32_t east = rgba_mix(base,world_face_light(1.0f,0.0f,0.82f));
    uint32_t south = rgba_mix(base,world_face_light(0.0f,1.0f,0.82f));
    uint32_t west = rgba_mix(base,world_face_light(-1.0f,0.0f,0.78f));
    tri(c,v[4],v[5],v[6],top); tri(c,v[4],v[6],v[7],top);
    {
        uint32_t nb=rgba_mix(north,0.91f),nt=rgba_mix(north,1.055f);
        uint32_t eb=rgba_mix(east,0.91f),et=rgba_mix(east,1.055f);
        uint32_t sb=rgba_mix(south,0.91f),st=rgba_mix(south,1.055f);
        uint32_t wb=rgba_mix(west,0.91f),wt=rgba_mix(west,1.055f);
        quad_color(c,v[0],nb,v[1],nb,v[5],nt,v[4],nt);
        quad_color(c,v[1],eb,v[2],eb,v[6],et,v[5],et);
        quad_color(c,v[2],sb,v[3],sb,v[7],st,v[6],st);
        quad_color(c,v[3],wb,v[0],wb,v[4],wt,v[7],wt);
    }
}

static void tapered_box_y(const rcam *c,float x,float z,float hx,float hz,float tx,float tz,float y0,float h,uint32_t base){
    rv3 b[4]={{x-hx,y0,z-hz},{x+hx,y0,z-hz},{x+hx,y0,z+hz},{x-hx,y0,z+hz}};
    rv3 t[4]={{x-tx,y0+h,z-tz},{x+tx,y0+h,z-tz},{x+tx,y0+h,z+tz},{x-tx,y0+h,z+tz}};
    uint32_t top=rgba_mix(base,1.03f+0.13f*g_daylight_f);
    uint32_t north=rgba_mix(base,world_face_light(0.0f,-1.0f,0.78f));
    uint32_t east =rgba_mix(base,world_face_light(1.0f, 0.0f,0.82f));
    uint32_t south=rgba_mix(base,world_face_light(0.0f, 1.0f,0.84f));
    uint32_t west =rgba_mix(base,world_face_light(-1.0f,0.0f,0.78f));
    tri(c,t[0],t[1],t[2],top);tri(c,t[0],t[2],t[3],top);
    quad_color(c,b[0],rgba_mix(north,0.90f),b[1],rgba_mix(north,0.90f),t[1],rgba_mix(north,1.06f),t[0],rgba_mix(north,1.06f));
    quad_color(c,b[1],rgba_mix(east,0.90f), b[2],rgba_mix(east,0.90f), t[2],rgba_mix(east,1.06f), t[1],rgba_mix(east,1.06f));
    quad_color(c,b[2],rgba_mix(south,0.90f),b[3],rgba_mix(south,0.90f),t[3],rgba_mix(south,1.06f),t[2],rgba_mix(south,1.06f));
    quad_color(c,b[3],rgba_mix(west,0.90f), b[0],rgba_mix(west,0.90f), t[0],rgba_mix(west,1.06f), t[3],rgba_mix(west,1.06f));
}

static void octa(const rcam *c, float x, float z, float r, float y0, float h, uint32_t base) {
    rv3 top = {x,y0+h,z}, bot = {x,y0+0.02f,z};
    rv3 e = {x+r,y0+h*0.45f,z}, w = {x-r,y0+h*0.45f,z};
    rv3 n = {x,y0+h*0.45f,z+r}, s = {x,y0+h*0.45f,z-r};
    uint32_t a = rgba_mix(base,1.18f), b = rgba_mix(base,0.92f), d = rgba_mix(base,0.70f);
    tri(c,top,e,n,a); tri(c,top,n,w,a); tri(c,top,w,s,b); tri(c,top,s,e,b);
    tri(c,bot,n,e,d); tri(c,bot,w,n,d); tri(c,bot,s,w,d); tri(c,bot,e,s,d);
}

/* Eight-sided vertical prism for low-poly machinery.  It gives bearings, housings and
 * drums a deliberate manufactured silhouette without requiring a high-poly mesh. */
static void prism8_y(const rcam *c,float x,float z,float r,float y0,float h,uint32_t base){
    static const float dx[8]={1.0f,0.7071f,0.0f,-0.7071f,-1.0f,-0.7071f,0.0f,0.7071f};
    static const float dz[8]={0.0f,0.7071f,1.0f,0.7071f,0.0f,-0.7071f,-1.0f,-0.7071f};
    rv3 lo[8],hi[8],topc={x,y0+h,z};uint32_t i;
    for(i=0u;i<8u;++i){lo[i]=(rv3){x+dx[i]*r,y0,z+dz[i]*r};hi[i]=lo[i];hi[i].y=y0+h;}
    for(i=0u;i<8u;++i){
        uint32_t j=(i+1u)&7u;
        float nx=(dx[i]+dx[j])*0.5412f,nz=(dz[i]+dz[j])*0.5412f;
        float tone=world_face_light(nx,nz,0.82f);
        uint32_t side=rgba_mix(base,tone);
        uint32_t low=rgba_mix(side,0.91f),high=rgba_mix(side,1.055f);
        quad_color(c,lo[i],low,lo[j],low,hi[j],high,hi[i],high);
        tri(c,topc,hi[i],hi[j],rgba_mix(base,1.01f+0.11f*g_daylight_f+(float)(i&1u)*0.012f));
    }
}

/* Rectangular mass with clipped corners.  Large deployed artifacts use this instead of
 * perfect cuboids so their silhouette carries scale and manufactured intent. */
static void beveled_box_y(const rcam *c,float x,float z,float hx,float hz,float bevel,float y0,float h,uint32_t base){
    rv3 lo[8],hi[8],topc={x,y0+h,z};uint32_t i;
    float b=bevel;if(b>hx*0.45f)b=hx*0.45f;if(b>hz*0.45f)b=hz*0.45f;if(b<0.0f)b=0.0f;
    lo[0]=(rv3){x+hx-b,y0,z-hz};lo[1]=(rv3){x+hx,y0,z-hz+b};
    lo[2]=(rv3){x+hx,y0,z+hz-b};lo[3]=(rv3){x+hx-b,y0,z+hz};
    lo[4]=(rv3){x-hx+b,y0,z+hz};lo[5]=(rv3){x-hx,y0,z+hz-b};
    lo[6]=(rv3){x-hx,y0,z-hz+b};lo[7]=(rv3){x-hx+b,y0,z-hz};
    for(i=0u;i<8u;++i){
        uint32_t j=(i+1u)&7u;float ex,ez,len,nx,nz,tone;uint32_t side,low,high;
        hi[i]=lo[i];hi[i].y=y0+h;
        ex=lo[j].x-lo[i].x;ez=lo[j].z-lo[i].z;len=(ex<0.0f?-ex:ex)+(ez<0.0f?-ez:ez);
        if(len<0.0001f)len=1.0f;
        nx=ez/len;nz=-ex/len;tone=world_face_light(nx,nz,0.82f);
        side=rgba_mix(base,tone);low=rgba_mix(side,0.90f);high=rgba_mix(side,1.06f);
        quad_color(c,lo[i],low,lo[j],low,(rv3){lo[j].x,y0+h,lo[j].z},high,hi[i],high);
    }
    for(i=0u;i<8u;++i){uint32_t j=(i+1u)&7u;tri(c,topc,hi[i],hi[j],rgba_mix(base,1.03f+0.12f*g_daylight_f));}
}

/* Broad, irregular low-poly rock.  Unlike octa(), this keeps a wide shoulder and a
 * subdued crown so ore deposits read as geological masses rather than pyramids. */
static void faceted_rock(const rcam *c,float x,float z,float rx,float rz,float y0,float h,uint32_t base,uint32_t seed){
    static const float dx[8]={1.0f,0.7071f,0.0f,-0.7071f,-1.0f,-0.7071f,0.0f,0.7071f};
    static const float dz[8]={0.0f,0.7071f,1.0f,0.7071f,0.0f,-0.7071f,-1.0f,-0.7071f};
    rv3 lo[8],shoulder[8],top[4];uint32_t i;
    float top_dx=((float)((seed>>4u)&15u)-7.5f)*rx*0.012f;
    float top_dz=((float)((seed>>8u)&15u)-7.5f)*rz*0.012f;
    for(i=0u;i<8u;++i){
        uint32_t bits=(seed>>(i*3u%24u))&7u;float jitter=0.86f+(float)bits*0.035f;
        float lx=x+dx[i]*rx*jitter,lz=z+dz[i]*rz*jitter;
        float upper=0.47f+(float)((seed>>(i+11u))&3u)*0.035f;
        lo[i]=(rv3){lx,terrain_yf(lx,lz)+0.018f,lz};
        shoulder[i]=(rv3){x+dx[i]*rx*0.64f*jitter,y0+h*upper,z+dz[i]*rz*0.64f*jitter};
    }
    /* Four uneven top vertices form a broad crown instead of a central apex.  This
     * makes ore read as fractured geology even with very few triangles. */
    top[0]=(rv3){x+rx*0.27f+top_dx,y0+h*(0.70f+(float)((seed>>2u)&3u)*0.025f),z-rz*0.18f+top_dz};
    top[1]=(rv3){x+rx*0.12f+top_dx,y0+h*(0.75f+(float)((seed>>6u)&3u)*0.020f),z+rz*0.27f+top_dz};
    top[2]=(rv3){x-rx*0.28f+top_dx,y0+h*(0.69f+(float)((seed>>10u)&3u)*0.026f),z+rz*0.14f+top_dz};
    top[3]=(rv3){x-rx*0.14f+top_dx,y0+h*(0.73f+(float)((seed>>14u)&3u)*0.022f),z-rz*0.26f+top_dz};
    for(i=0u;i<8u;++i){
        uint32_t j=(i+1u)&7u;float tone=0.73f+(float)((seed>>(i+2u))&7u)*0.038f;
        uint32_t side=rgba_mix(base,world_face_light(dx[i],dz[i],tone));
        uint32_t k=i>>1u,kn=(j>>1u)&3u;
        quad(c,lo[i],lo[j],shoulder[j],shoulder[i],side);
        if(k==kn)tri(c,shoulder[i],shoulder[j],top[k],rgba_mix(base,0.88f+(float)(i&3u)*0.045f));
        else quad(c,shoulder[i],shoulder[j],top[kn],top[k],rgba_mix(base,0.92f+(float)(i&3u)*0.040f));
    }
    tri(c,top[0],top[1],top[2],rgba_mix(base,1.00f+0.10f*g_daylight_f));
    tri(c,top[0],top[2],top[3],rgba_mix(base,0.94f+0.09f*g_daylight_f));
}


/* Foliage is deliberately NOT terrain-following. Geological faceted_rock() anchors its
 * lower ring to the ground, which is correct for boulders but made tree crowns stretch
 * down into cactus-like columns. These suspended clusters keep a full 3D crown at y0. */
static void faceted_canopy(const rcam *c,float x,float z,float rx,float rz,float y0,float h,uint32_t base,uint32_t seed){
    static const float dx[8]={1.0f,0.7071f,0.0f,-0.7071f,-1.0f,-0.7071f,0.0f,0.7071f};
    static const float dz[8]={0.0f,0.7071f,1.0f,0.7071f,0.0f,-0.7071f,-1.0f,-0.7071f};
    rv3 lo[8],mid[8],top[4];uint32_t i;
    float top_dx=((float)((seed>>5u)&15u)-7.5f)*rx*0.010f;
    float top_dz=((float)((seed>>9u)&15u)-7.5f)*rz*0.010f;
    for(i=0u;i<8u;++i){
        uint32_t bits=(seed>>((i*3u)%24u))&7u;float jitter=0.90f+(float)bits*0.026f;
        lo[i]=(rv3){x+dx[i]*rx*0.68f*jitter,y0,z+dz[i]*rz*0.68f*jitter};
        mid[i]=(rv3){x+dx[i]*rx*jitter,y0+h*(0.39f+(float)((seed>>(i+12u))&3u)*0.025f),z+dz[i]*rz*jitter};
    }
    top[0]=(rv3){x+rx*0.27f+top_dx,y0+h*0.88f,z-rz*0.18f+top_dz};
    top[1]=(rv3){x+rx*0.14f+top_dx,y0+h*0.96f,z+rz*0.27f+top_dz};
    top[2]=(rv3){x-rx*0.26f+top_dx,y0+h*0.90f,z+rz*0.16f+top_dz};
    top[3]=(rv3){x-rx*0.16f+top_dx,y0+h*0.94f,z-rz*0.25f+top_dz};
    for(i=0u;i<8u;++i){
        uint32_t j=(i+1u)&7u,k=i>>1u,kn=(j>>1u)&3u;
        uint32_t lower=rgba_mix(base,world_face_light(dx[i],dz[i],0.72f+(float)(i&3u)*0.035f));
        quad(c,lo[i],lo[j],mid[j],mid[i],lower);
        if(k==kn)tri(c,mid[i],mid[j],top[k],rgba_mix(base,0.90f+(float)(i&3u)*0.035f));
        else quad(c,mid[i],mid[j],top[kn],top[k],rgba_mix(base,0.93f+(float)(i&3u)*0.030f));
    }
    tri(c,top[0],top[1],top[2],rgba_mix(base,1.03f+0.08f*g_daylight_f));
    tri(c,top[0],top[2],top[3],rgba_mix(base,0.98f+0.07f*g_daylight_f));
}

static void faceted_cone_y(const rcam *c,float x,float z,float rx,float rz,float y0,float h,uint32_t base,uint32_t seed){
    static const float dx[8]={1.0f,0.7071f,0.0f,-0.7071f,-1.0f,-0.7071f,0.0f,0.7071f};
    static const float dz[8]={0.0f,0.7071f,1.0f,0.7071f,0.0f,-0.7071f,-1.0f,-0.7071f};
    rv3 ring[8];uint32_t i;
    float ax=x+((float)((seed>>6u)&15u)-7.5f)*rx*0.010f;
    float az=z+((float)((seed>>10u)&15u)-7.5f)*rz*0.010f;
    rv3 apex={ax,y0+h,az};
    for(i=0u;i<8u;++i){
        float jitter=0.91f+(float)((seed>>((i*3u)%24u))&7u)*0.024f;
        ring[i]=(rv3){x+dx[i]*rx*jitter,y0,z+dz[i]*rz*jitter};
    }
    for(i=0u;i<8u;++i){
        uint32_t j=(i+1u)&7u;
        float tone=world_face_light(dx[i],dz[i],0.76f+(float)(i&3u)*0.035f);
        tri(c,ring[i],ring[j],apex,rgba_mix(base,tone));
    }
}

static rv3 oriented_point(float x, float z, float fx, float fz, float local_x, float local_z, float y) {
    float rx = fz;
    float rz = -fx;
    rv3 p;
    p.x = x + rx * local_x + fx * local_z;
    p.y = y;
    p.z = z + rz * local_x + fz * local_z;
    return p;
}

static void oriented_box_y(const rcam *c,float x,float z,float fx,float fz,
                           float local_x,float local_z,float hx,float hz,
                           float y0,float h,uint32_t base) {
    rv3 v[8];
    float center_ground=terrain_yf(x,z);
    float base_offset=y0-center_ground;
    uint32_t j;
    float rx=fz,rz=-fx;
    uint32_t top=rgba_mix(base,1.03f+0.13f*g_daylight_f);
    uint32_t back=rgba_mix(base,world_face_light(-fx,-fz,0.79f));
    uint32_t right=rgba_mix(base,world_face_light(rx,rz,0.82f));
    uint32_t front=rgba_mix(base,world_face_light(fx,fz,0.86f));
    uint32_t left=rgba_mix(base,world_face_light(-rx,-rz,0.79f));
    v[0]=oriented_point(x,z,fx,fz,local_x-hx,local_z-hz,0.0f);
    v[1]=oriented_point(x,z,fx,fz,local_x+hx,local_z-hz,0.0f);
    v[2]=oriented_point(x,z,fx,fz,local_x+hx,local_z+hz,0.0f);
    v[3]=oriented_point(x,z,fx,fz,local_x-hx,local_z+hz,0.0f);
    for (j=0u;j<4u;++j) v[j].y=terrain_yf(v[j].x,v[j].z)+base_offset;
    for (j=0u;j<4u;++j) { v[j+4u]=v[j]; v[j+4u].y+=h; }
    tri(c,v[4],v[5],v[6],top);tri(c,v[4],v[6],v[7],top);
    {
        uint32_t bb=rgba_mix(back,0.91f),bt=rgba_mix(back,1.055f);
        uint32_t rb=rgba_mix(right,0.91f),rt=rgba_mix(right,1.055f);
        uint32_t fb=rgba_mix(front,0.91f),ft=rgba_mix(front,1.055f);
        uint32_t lb=rgba_mix(left,0.91f),lt=rgba_mix(left,1.055f);
        quad_color(c,v[0],bb,v[1],bb,v[5],bt,v[4],bt);
        quad_color(c,v[1],rb,v[2],rb,v[6],rt,v[5],rt);
        quad_color(c,v[2],fb,v[3],fb,v[7],ft,v[6],ft);
        quad_color(c,v[3],lb,v[0],lb,v[4],lt,v[7],lt);
    }
}


static void ground_shadow_layer(const rcam *c,float x,float z,float sx,float sz,float ox,float oz,float yoff,uint32_t col) {
    static const float dx[12]={1.0000f,0.8660f,0.5000f,0.0000f,-0.5000f,-0.8660f,-1.0000f,-0.8660f,-0.5000f,0.0000f,0.5000f,0.8660f};
    static const float dz[12]={0.0000f,0.5000f,0.8660f,1.0000f,0.8660f,0.5000f,0.0000f,-0.5000f,-0.8660f,-1.0000f,-0.8660f,-0.5000f};
    float along_x=-g_sun_world_x,along_z=-g_sun_world_z;
    float side_x=-along_z,side_z=along_x;
    rv3 center={x+ox,terrain_yf(x+ox,z+oz)+yoff,z+oz};uint32_t i;
    for(i=0u;i<12u;++i){
        uint32_t j=(i+1u)%12u;
        float a_side=dx[i]*sx,a_along=dz[i]*sz,b_side=dx[j]*sx,b_along=dz[j]*sz;
        float ax=x+ox+side_x*a_side+along_x*a_along,az=z+oz+side_z*a_side+along_z*a_along;
        float bx=x+ox+side_x*b_side+along_x*b_along,bz=z+oz+side_z*b_side+along_z*b_along;
        tri_alpha(c,center,(rv3){ax,terrain_yf(ax,az)+yoff,az},(rv3){bx,terrain_yf(bx,bz)+yoff,bz},col);
    }
}

static void ground_shadow(const rcam *c,float x,float z,float sx,float sz,float strength) {
    visual_palette p=palette();
    uint32_t rgb=rgba_mix(p.sky_top,0.62f)&UINT32_C(0xffffff00);
    uint32_t outer_a=9u+(uint32_t)(strength*5.0f),middle_a=15u+(uint32_t)(strength*7.0f),inner_a=23u+(uint32_t)(strength*10.0f);
    uint32_t outer,middle,inner;
    float ox=-g_sun_world_x*0.23f*strength,oz=-g_sun_world_z*0.23f*strength;
    if(outer_a>24u)outer_a=24u;
    if(middle_a>34u)middle_a=34u;
    if(inner_a>48u)inner_a=48u;
    outer=rgb|outer_a;middle=rgb|middle_a;inner=rgb|inner_a;
    /* True translucent layers preserve the terrain/territory colour underneath and do not
     * write depth. The result behaves like contact occlusion instead of three opaque decals. */
    ground_shadow_layer(c,x,z,sx*1.20f,sz*1.30f,ox,oz,0.128f,outer);
    ground_shadow_layer(c,x,z,sx*0.95f,sz*1.05f,ox*0.58f,oz*0.58f,0.130f,middle);
    ground_shadow_layer(c,x,z,sx*0.56f,sz*0.64f,0.0f,0.0f,0.132f,inner);
}

static void actor_shadow(const rcam *c, float x, float z, float r) {
    ground_shadow(c,x,z,r*0.84f,r*0.61f,0.72f);
}

static void ground_disc16(const rcam *c,float x,float z,float r,float yoff,uint32_t col){
    static const float dx[16]={1.0f,0.9239f,0.7071f,0.3827f,0.0f,-0.3827f,-0.7071f,-0.9239f,-1.0f,-0.9239f,-0.7071f,-0.3827f,0.0f,0.3827f,0.7071f,0.9239f};
    static const float dz[16]={0.0f,0.3827f,0.7071f,0.9239f,1.0f,0.9239f,0.7071f,0.3827f,0.0f,-0.3827f,-0.7071f,-0.9239f,-1.0f,-0.9239f,-0.7071f,-0.3827f};
    rv3 center={x,terrain_yf(x,z)+yoff,z};uint32_t i;
    for(i=0u;i<16u;++i){
        uint32_t j=(i+1u)&15u;
        rv3 a={x+dx[i]*r,terrain_yf(x+dx[i]*r,z+dz[i]*r)+yoff,z+dz[i]*r};
        rv3 b={x+dx[j]*r,terrain_yf(x+dx[j]*r,z+dz[j]*r)+yoff,z+dz[j]*r};
        tri(c,center,a,b,col);
    }
}

static void ground_patch8(const rcam *c,float x,float z,float r,uint32_t seed,uint32_t col){
    static const float dx[8]={1.0f,0.7071f,0.0f,-0.7071f,-1.0f,-0.7071f,0.0f,0.7071f};
    static const float dz[8]={0.0f,0.7071f,1.0f,0.7071f,0.0f,-0.7071f,-1.0f,-0.7071f};
    rv3 center={x,terrain_yf(x,z)+0.020f,z};uint32_t i;
    for(i=0u;i<8u;++i){
        uint32_t j=(i+1u)&7u;
        float ra=r*(0.72f+(float)((seed>>(i*3u))&7u)*0.045f);
        float rb=r*(0.72f+(float)((seed>>(j*3u))&7u)*0.045f);
        float ax=x+dx[i]*ra,az=z+dz[i]*ra,bx=x+dx[j]*rb,bz=z+dz[j]*rb;
        tri(c,center,(rv3){ax,terrain_yf(ax,az)+0.021f,az},(rv3){bx,terrain_yf(bx,bz)+0.021f,bz},col);
    }
}

static void grass_tuft(const rcam *c,float x,float z,float y,float s,uint32_t col,uint32_t seed){
    static const float dx[6]={1.0f,0.50f,-0.50f,-1.0f,-0.50f,0.50f};
    static const float dz[6]={0.0f,0.866f,0.866f,0.0f,-0.866f,-0.866f};
    uint32_t i;
    for(i=0u;i<6u;++i){
        float r=0.025f*s+(float)((seed>>(i*3u))&3u)*0.004f*s;
        float h=(0.12f+(float)((seed>>(i+7u))&7u)*0.012f)*s;
        float bx=x+dx[i]*0.050f*s,bz=z+dz[i]*0.050f*s;
        float px=-dz[i]*r,pz=dx[i]*r;
        uint32_t blade=rgba_mix(col,0.88f+(float)(i&3u)*0.055f);
        tri(c,(rv3){bx-px,terrain_yf(bx-px,bz-pz)+0.024f,bz-pz},
              (rv3){bx+px,terrain_yf(bx+px,bz+pz)+0.024f,bz+pz},
              (rv3){bx+dx[i]*0.018f*s,y+h,bz+dz[i]*0.018f*s},blade);
    }
}

static void grass_sprig(const rcam *c,float x,float z,float s,uint32_t col,uint32_t seed){
    static const float dx[3]={0.93f,-0.22f,-0.74f};
    static const float dz[3]={0.36f,0.98f,-0.67f};
    uint32_t i;
    for(i=0u;i<3u;++i){
        float h=(0.075f+(float)((seed>>(i*5u))&7u)*0.0065f)*s;
        float half=(0.010f+(float)((seed>>(i*4u+11u))&3u)*0.0018f)*s;
        float bx=x+dx[i]*0.025f*s,bz=z+dz[i]*0.025f*s;
        float px=-dz[i]*half,pz=dx[i]*half;
        uint32_t blade=rgba_mix(col,0.84f+(float)i*0.075f);
        tri(c,(rv3){bx-px,terrain_yf(bx-px,bz-pz)+0.020f,bz-pz},
              (rv3){bx+px,terrain_yf(bx+px,bz+pz)+0.020f,bz+pz},
              (rv3){bx+dx[i]*0.014f*s,terrain_yf(bx,bz)+0.020f+h,bz+dz[i]*0.014f*s},blade);
    }
}

static void low_shrub(const rcam *c,float x,float z,float y,float s,uint32_t low,uint32_t high,uint32_t seed){
    visual_palette p=palette();
    /* A real low shrub needs negative space.  A short woody fork plus three separate
     * leaf masses reads as vegetation instead of a single faceted green boulder. */
    prism8_y(c,x,z,0.020f*s,y+0.018f,0.115f*s,rgba_mix(p.trunk,0.76f));
    oriented_box_y(c,x,z,0.7071f,0.7071f,-0.025f,0.008f,0.012f*s,0.105f*s,y+0.070f,0.025f*s,rgba_mix(p.trunk,0.82f));
    faceted_canopy(c,x-0.105f*s,z+0.040f*s,0.135f*s,0.108f*s,y+0.072f,0.175f*s,low,seed^0x7du);
    faceted_canopy(c,x+0.105f*s,z-0.045f*s,0.120f*s,0.098f*s,y+0.085f,0.160f*s,high,seed^0x33u);
    faceted_canopy(c,x+0.005f*s,z+0.080f*s,0.105f*s,0.090f*s,y+0.125f,0.145f*s,rgba_mix(high,1.04f),seed^0xa1u);
}

static void runner(const rcam *c, const odg_actor *a) {
    float x=odg_fx_to_float(a->x);
    float z=odg_fx_to_float(a->z);
    float gy=terrain_yf(x,z);
    visual_palette p=palette();
    uint32_t signal=actor_base_color(a->id);
    uint32_t shell=a->flash_ticks!=0u?0xe9eef0ffu:
        rgba_lerp(p.building_alt,signal,a->type==ODG_ACTOR_PLAYER?116u:82u);
    actor_shadow(c,x,z,0.34f);
    /* Identity rule: one readable cube. Lighting/material may shade its six faces, but
     * no chassis, armour belt, visor, stacked pod or silhouette-changing attachment.
     * The world now establishes the scale hierarchy: actors stay compact while trees,
     * ore and infrastructure carry the environmental mass. */
    avatar_cube(c,a,x,z,gy+0.045f+odg_fx_to_float(a->vertical_offset_fx),0.46f,shell);
}

static void sky_soft_ellipse(uint32_t w,uint32_t h,int32_t cx,int32_t cy,int32_t rx,int32_t ry,uint32_t color,uint32_t opacity) {
    int32_t minx,maxx,miny,maxy,y;
    int64_t rr;
    if(rx<=0||ry<=0||opacity==0u)return;
    minx=cx-rx;maxx=cx+rx;miny=cy-ry;maxy=cy+ry;
    if(minx<0)minx=0;
    if(miny<0)miny=0;
    if(maxx>=(int32_t)w)maxx=(int32_t)w-1;
    if(maxy>=(int32_t)h)maxy=(int32_t)h-1;
    if(maxx<minx||maxy<miny)return;
    rr=(int64_t)rx*rx*(int64_t)ry*ry;
    for(y=miny;y<=maxy;++y){
        int32_t x;int64_t dy=(int64_t)y-cy;
        for(x=minx;x<=maxx;++x){
            int64_t dx=(int64_t)x-cx;
            int64_t d=dx*dx*(int64_t)ry*ry+dy*dy*(int64_t)rx*rx;
            if(d<rr){
                uint32_t i=((uint32_t)y*w+(uint32_t)x)*4u;
                uint32_t old=((uint32_t)g_odg_framebuffer[i]<<24)|((uint32_t)g_odg_framebuffer[i+1u]<<16)|((uint32_t)g_odg_framebuffer[i+2u]<<8)|255u;
                uint32_t a=(uint32_t)(((rr-d)*(int64_t)opacity)/rr);
                uint32_t out=rgba_lerp(old,color,a);
                g_odg_framebuffer[i]=(uint8_t)(out>>24);g_odg_framebuffer[i+1u]=(uint8_t)(out>>16);g_odg_framebuffer[i+2u]=(uint8_t)(out>>8);
            }
        }
    }
}

static void render_sky_clouds(const rcam *c,uint32_t phase,uint32_t sky_horizon) {
    static const int32_t dir[8][2]={{32767,0},{23170,23170},{0,32767},{-23170,23170},{-32767,0},{-23170,-23170},{0,-32767},{23170,-23170}};
    static const uint8_t height_pct[8]={18,25,14,30,20,11,27,16};
    uint32_t ci;
    uint32_t cloud=rgba_lerp(sky_horizon,0xe2e8e7ffu,58u);
    uint32_t day_alpha=phase<32u?38u:12u;
    for(ci=0u;ci<8u;++ci){
        uint32_t ai=(ci*3u+1u)&7u;
        float wx=(float)dir[ai][0]/32767.0f,wz=(float)dir[ai][1]/32767.0f;
        float side=wx*c->right_x+wz*c->right_z;
        float front=wx*c->forward_x+wz*c->forward_z;
        if(front>-0.30f){
            int32_t px=(int32_t)((float)c->w*0.5f+side*(float)c->w*0.49f);
            int32_t py=(int32_t)((float)c->h*(float)height_pct[ci]/100.0f);
            int32_t rx=(int32_t)((float)c->w*(0.034f+(float)(ci&3u)*0.007f));
            int32_t ry=(int32_t)((float)c->h*(0.017f+(float)(ci&1u)*0.007f));
            int32_t drift=(int32_t)((phase+(ci*7u))&15u)-7;
            sky_soft_ellipse(c->w,c->h,px+drift,py,rx,ry,cloud,day_alpha);
            sky_soft_ellipse(c->w,c->h,px-rx/3+drift,py+ry/3,rx*2/3,ry*3/4,rgba_mix(cloud,0.94f),day_alpha/2u);
            sky_soft_ellipse(c->w,c->h,px+rx/3+drift,py-ry/4,rx/2,ry*2/3,rgba_mix(cloud,1.03f),day_alpha/2u);
        }
    }
}

static void clear_frame(const rcam *c) {
    uint32_t x,y;
    uint32_t w=g_odg.width,h=g_odg.height;
    visual_palette p=palette();
    uint32_t phase=(odg_day_phase_permille()*64u)/1000u;
    uint32_t daylight=odg_daylight_permille();
    uint32_t day_h=phase<32u?(phase<=16u?phase:32u-phase):0u;
    uint32_t light=66u+(daylight*190u)/1000u;if(light>256u)light=256u;
    float lf=(float)light/256.0f;
    float top_scale=phase<32u?(0.72f+0.34f*lf):(0.50f+0.40f*lf);
    float horizon_scale=phase<32u?(0.76f+0.28f*lf):(0.60f+0.28f*lf);
    uint32_t sky_top=rgba_mix(p.sky_top,top_scale);
    uint32_t sky_horizon=rgba_mix(p.sky_horizon,horizon_scale);
    uint32_t sky_mid=rgba_lerp(sky_top,sky_horizon,132u);
    uint32_t haze=rgba_lerp(sky_horizon,p.fog,54u);
    uint32_t split=(h*45u)/100u;
    int32_t sun_x=(int32_t)(w/2u),sun_y=(int32_t)(h/3u),sun_r=(int32_t)((h*11u)/100u),core_r=(int32_t)((h*15u)/1000u);
    int celestial_visible=0;
    g_daylight_q8=light;g_daylight_f=(float)light/256.0f;
    if(sun_r<20) sun_r=20;
    if(core_r<4) core_r=4;

    /* Celestial azimuth is WORLD-fixed. Rotating the camera changes where sun/moon
     * appears on screen; it no longer sticks to the same screen coordinate. */
    {
        static const int32_t d16[16][2]={{32767,0},{30273,12539},{23170,23170},{12539,30273},{0,32767},{-12539,30273},{-23170,23170},{-30273,12539},{-32767,0},{-30273,-12539},{-23170,-23170},{-12539,-30273},{0,-32767},{12539,-30273},{23170,-23170},{30273,-12539}};
        uint32_t ai=(phase>>2u)&15u;float sx=(float)d16[ai][0]/32767.0f,sz=(float)d16[ai][1]/32767.0f;
        float side,front;
        if(phase>=32u){sx=-sx;sz=-sz;}
        g_sun_world_x=sx;g_sun_world_z=sz;
        side=sx*c->right_x+sz*c->right_z;front=sx*c->forward_x+sz*c->forward_z;
        if(front>-0.38f){
            float elev=phase<32u?(float)day_h/16.0f:0.34f;
            sun_x=(int32_t)((float)w*0.5f+side*(float)w*0.44f);
            sun_y=(int32_t)((float)h*(0.51f-elev*0.36f));
            celestial_visible=1;
        }
    }

    odg_memset(g_odg_depth,0,(size_t)w*(size_t)h*sizeof(g_odg_depth[0]));
    for(y=0u;y<h;++y){
        uint32_t base,r,g,b;
        if(y<=split){uint32_t t=split!=0u?(y*256u)/split:0u;if(t<168u)base=rgba_lerp(sky_top,sky_mid,(t*256u)/168u);else base=rgba_lerp(sky_mid,sky_horizon,((t-168u)*256u)/88u);}
        else{uint32_t den=h>split?h-split:1u;uint32_t t=((y-split)*256u)/den;if(t>256u)t=256u;base=rgba_lerp(haze,p.fog,t/2u);}
        r=(base>>24)&255u;g=(base>>16)&255u;b=(base>>8)&255u;
        for(x=0u;x<w;++x){uint32_t i=(y*w+x)*4u;g_odg_framebuffer[i]=(uint8_t)r;g_odg_framebuffer[i+1u]=(uint8_t)g;g_odg_framebuffer[i+2u]=(uint8_t)b;g_odg_framebuffer[i+3u]=255u;}
    }
    render_sky_clouds(c,phase,sky_horizon);
    if(celestial_visible){
        uint32_t scatter=phase<32u?rgba_lerp(sky_horizon,p.coast_edge,44u):rgba_lerp(sky_horizon,0xaebdcbffu,48u);
        uint32_t scatter_alpha=phase<32u?22u:11u;
        /* Atmospheric forward scatter is world-anchored through sun_x/sun_y.  A broad
         * low-opacity lobe gives the sky depth without a full-screen bloom pass and,
         * unlike a vignette flare, rotates correctly when the camera turns. */
        sky_soft_ellipse(w,h,sun_x,sun_y+sun_r/5,sun_r*3,sun_r*2,scatter,scatter_alpha);
    }
    if(celestial_visible){
        int32_t minx=sun_x-sun_r,maxx=sun_x+sun_r,miny=sun_y-sun_r,maxy=sun_y+sun_r,rr=sun_r*sun_r,cr=core_r*core_r;
        uint32_t glow=phase<32u?rgba_mix(p.accent,0.92f):0xaebdcbffu;
        if(minx<0) minx=0;
        if(miny<0) miny=0;
        if(maxx>=(int32_t)w) maxx=(int32_t)w-1;
        if(maxy>=(int32_t)h) maxy=(int32_t)h-1;
        for(y=(uint32_t)miny;y<=(uint32_t)maxy;++y)for(x=(uint32_t)minx;x<=(uint32_t)maxx;++x){int32_t dx=(int32_t)x-sun_x,dy=(int32_t)y-sun_y,d2=dx*dx+dy*dy;uint32_t i,col;if(d2>=rr)continue;i=(y*w+x)*4u;col=((uint32_t)g_odg_framebuffer[i]<<24)|((uint32_t)g_odg_framebuffer[i+1u]<<16)|((uint32_t)g_odg_framebuffer[i+2u]<<8)|255u;col=rgba_lerp(col,glow,(uint32_t)(((int64_t)(rr-d2)*58)/rr));if(d2<cr){uint32_t core=(uint32_t)(((int64_t)(cr-d2)*150)/cr)+46u;if(core>218u)core=218u;col=rgba_lerp(col,0xf4eee2ffu,core);}g_odg_framebuffer[i]=(uint8_t)(col>>24);g_odg_framebuffer[i+1u]=(uint8_t)(col>>16);g_odg_framebuffer[i+2u]=(uint8_t)(col>>8);g_odg_framebuffer[i+3u]=255u;}
    }
    /* Night reference stars are world-space bearings, not a screen overlay. They move
     * across the viewport only when the camera/world-time changes, making it visually
     * obvious that the sky is not glued to the camera. */
    if(phase>=30u){
        static const int32_t sd[16][2]={{32767,0},{30273,12539},{23170,23170},{12539,30273},{0,32767},{-12539,30273},{-23170,23170},{-30273,12539},{-32767,0},{-30273,-12539},{-23170,-23170},{-12539,-30273},{0,-32767},{12539,-30273},{23170,-23170},{30273,-12539}};
        static const uint8_t sy[12]={13,21,9,28,17,34,12,25,7,31,19,14};
        uint32_t si;
        for(si=0u;si<12u;++si){
            uint32_t ai=(si*5u+3u+(phase>>3u))&15u;
            float sx=(float)sd[ai][0]/32767.0f,sz=(float)sd[ai][1]/32767.0f;
            float side=sx*c->right_x+sz*c->right_z,front=sx*c->forward_x+sz*c->forward_z;
            if(front>-0.18f){
                int32_t px=(int32_t)((float)w*0.5f+side*(float)w*0.47f);
                int32_t py=(int32_t)((float)h*((float)sy[si]/100.0f));
                if(px>=1&&px+1<(int32_t)w&&py>=1&&py+1<(int32_t)h){
                    uint32_t alpha=phase<36u?(phase-30u)*28u:168u;uint32_t yy,xx;
                    if(alpha>180u)alpha=180u;
                    for(yy=(uint32_t)(py-1);yy<=(uint32_t)(py+1);++yy)for(xx=(uint32_t)(px-1);xx<=(uint32_t)(px+1);++xx){
                        uint32_t ii=(yy*w+xx)*4u;uint32_t old=((uint32_t)g_odg_framebuffer[ii]<<24)|((uint32_t)g_odg_framebuffer[ii+1u]<<16)|((uint32_t)g_odg_framebuffer[ii+2u]<<8)|255u;
                        uint32_t aa=(xx==(uint32_t)px&&yy==(uint32_t)py)?alpha:alpha/3u;uint32_t cc=rgba_lerp(old,0xeaf6ffffu,aa);
                        g_odg_framebuffer[ii]=(uint8_t)(cc>>24);g_odg_framebuffer[ii+1u]=(uint8_t)(cc>>16);g_odg_framebuffer[ii+2u]=(uint8_t)(cc>>8);g_odg_framebuffer[ii+3u]=255u;
                    }
                }
            }
        }
    }

    g_odg.render_triangles=0u;g_odg.render_pixels_touched=0u;
}


static uint32_t decor_hash(int64_t x,int64_t z);

static float distant_relief_corner(float x,float z){
    int64_t global_fx_x=g_render_center_global_fx_x+(int64_t)(x*(float)ODG_FX_ONE);
    int64_t global_fx_z=g_render_center_global_fx_z+(int64_t)(z*(float)ODG_FX_ONE);
    int64_t gx=odg_floor_div_i64_internal(global_fx_x,(int64_t)ODG_FX_ONE);
    int64_t gz=odg_floor_div_i64_internal(global_fx_z,(int64_t)ODG_FX_ONE);
    uint32_t broad=visual_smooth_noise_scale(gx,gz,84u,UINT32_C(0x6a09e667));
    uint32_t medium=visual_smooth_noise_scale(gx,gz,37u,UINT32_C(0xbb67ae85));
    uint32_t fine=visual_smooth_noise_scale(gx,gz,19u,UINT32_C(0x3c6ef372));
    float bn=(float)broad/255.0f,mn=(float)medium/255.0f,fn=(float)fine/255.0f;
    float n=bn*0.56f+mn*0.31f+fn*0.13f;
    float ridge=mn>0.5f?(mn-0.5f)*2.0f:(0.5f-mn)*2.0f;
    float ax=x<0.0f?-x:x,az=z<0.0f?-z:z,far=ax>az?ax:az;
    float fade=smoothstep01((far-64.0f)/112.0f);
    /* Presentation-only macro relief now has a true ridge component.  It remains fully
     * continuous in global coordinates, starts outside the simulation mesh and gains
     * amplitude gradually, so the streamed world reads as land continuing for kilometres
     * rather than a low skirt wrapped around a 128m square. */
    return (0.16f+n*n*6.15f+ridge*ridge*2.10f)*fade;
}

static float outer_horizon_relief_corner(float x,float z){
    int64_t global_fx_x=g_render_center_global_fx_x+(int64_t)(x*(float)ODG_FX_ONE);
    int64_t global_fx_z=g_render_center_global_fx_z+(int64_t)(z*(float)ODG_FX_ONE);
    int64_t gx=odg_floor_div_i64_internal(global_fx_x,(int64_t)ODG_FX_ONE);
    int64_t gz=odg_floor_div_i64_internal(global_fx_z,(int64_t)ODG_FX_ONE);
    uint32_t broad=visual_smooth_noise_scale(gx,gz,176u,UINT32_C(0x243f6a88));
    uint32_t ridge_noise=visual_smooth_noise_scale(gx,gz,73u,UINT32_C(0x85a308d3));
    float n=(float)broad/255.0f;
    float rn=(float)ridge_noise/255.0f;
    float ridge=rn>0.5f?(rn-0.5f)*2.0f:(0.5f-rn)*2.0f;
    float ax=x<0.0f?-x:x,az=z<0.0f?-z:z,far=ax>az?ax:az;
    float fade=smoothstep01((far-224.0f)/240.0f);
    /* A second, coarse visual LOD carries the skyline beyond the streaming ring.  It
     * begins at exactly zero displacement where the 8m mesh ends, then grows only in
     * the atmospheric distance.  This breaks the old ruler-straight map edge without
     * changing authoritative height, collision, chunks or navigation. */
    return (5.0f+n*n*n*30.0f+ridge*ridge*18.0f)*fade;
}

static void render_outer_horizon(const rcam *c){
    visual_palette p=palette();int32_t gz;
    /* 16m cells are sufficient this far away and cost far less than extending the near
     * 8m presentation mesh to the skyline.  The outer edge is intentionally pushed into
     * heavy aerial perspective so it dissolves into the sky instead of becoming a new
     * visible square boundary. */
    for(gz=-544;gz<544;gz+=16){int32_t gx;for(gx=-544;gx<544;gx+=16){
        float x0=(float)gx,z0=(float)gz,x1=x0+16.0f,z1=z0+16.0f,cx=x0+8.0f,cz=z0+8.0f;
        float h00,h10,h11,h01,hc,dsx,dsz,slope,sun,far,u;uint32_t base,aerial,noise;
        if(gx>=-224&&gx<224&&gz>=-224&&gz<224)continue;
        if(!world_point_maybe_visible(c,cx,cz,13.0f))continue;
        h00=terrain_yf(x0,z0)+distant_relief_corner(x0,z0)+outer_horizon_relief_corner(x0,z0);
        h10=terrain_yf(x1,z0)+distant_relief_corner(x1,z0)+outer_horizon_relief_corner(x1,z0);
        h11=terrain_yf(x1,z1)+distant_relief_corner(x1,z1)+outer_horizon_relief_corner(x1,z1);
        h01=terrain_yf(x0,z1)+distant_relief_corner(x0,z1)+outer_horizon_relief_corner(x0,z1);
        hc=(h00+h10+h11+h01)*0.25f;
        if(hc<=2.0f){
            uint32_t t=hc<=0.20f?0u:(uint32_t)(((hc-0.20f)/1.80f)*256.0f);
            if(t>256u)t=256u;
            base=rgba_lerp(p.land_low,p.land_mid,t);
        }else{
            uint32_t t=hc>=8.8f?256u:(uint32_t)(((hc-2.0f)/6.8f)*256.0f);
            if(t>256u)t=256u;
            base=rgba_lerp(p.land_mid,p.land_high,t);
        }
        dsx=((h10+h11)-(h00+h01))*0.03125f;
        dsz=((h01+h11)-(h00+h10))*0.03125f;
        slope=(dsx<0.0f?-dsx:dsx)+(dsz<0.0f?-dsz:dsz);
        sun=0.97f-dsx*g_sun_world_x*0.11f-dsz*g_sun_world_z*0.11f;
        if(sun<0.82f)sun=0.82f;
        if(sun>1.08f)sun=1.08f;
        if(slope>0.12f){uint32_t exposed=(uint32_t)((slope-0.12f)*46.0f);if(exposed>38u)exposed=38u;base=rgba_lerp(base,p.rock,exposed);}
        base=rgba_mix(base,sun);
        {
            int64_t center_gx=odg_floor_div_i64_internal(g_render_center_global_fx_x,(int64_t)ODG_FX_ONE);
            int64_t center_gz=odg_floor_div_i64_internal(g_render_center_global_fx_z,(int64_t)ODG_FX_ONE);
            noise=visual_smooth_noise_scale(center_gx+(int64_t)gx,center_gz+(int64_t)gz,112u,UINT32_C(0x13198a2e));
            base=rgba_mix(base,0.978f+((float)noise/255.0f)*0.030f);
        }
        far=(cx<0.0f?-cx:cx)>(cz<0.0f?-cz:cz)?(cx<0.0f?-cx:cx):(cz<0.0f?-cz:cz);
        u=smoothstep01((far-224.0f)/320.0f);
        aerial=80u+(uint32_t)(u*92.0f);
        if(aerial>180u)aerial=180u;
        base=rgba_lerp(base,p.fog,aerial);
        quad(c,(rv3){x0,h00-0.070f,z0},(rv3){x1,h10-0.070f,z0},(rv3){x1,h11-0.070f,z1},(rv3){x0,h01-0.070f,z1},base);
    }}
}

static void render_distant_terrain(const rcam *c){
    visual_palette p=palette();int32_t gz;
    render_outer_horizon(c);
    /* Coarse presentation-only terrain extends well beyond the 128x128 simulation window.
     * The active world remains exact; this ring exists so the horizon never reveals the
     * streaming window as a flat square edge. */
    for(gz=-224;gz<224;gz+=8){int32_t gx;for(gx=-224;gx<224;gx+=8){
        float x0=(float)gx,z0=(float)gz,x1=x0+8.0f,z1=z0+8.0f,cx=x0+4.0f,cz=z0+4.0f;
        float h00,h10,h11,h01,hc;uint32_t base,noise;
        /* The exact simulation terrain spans [-64,+64) in render-local metres.  Render
         * the immediately adjacent 8m ring as well: the previous [-72,+72) exclusion
         * accidentally left an 8m void around the streamed window that read as a dark
         * moat at the horizon.  This coarse mesh now meets the active mesh exactly. */
        if(gx>=-(int32_t)ODG_WORLD_HALF_CELLS&&gx<(int32_t)ODG_WORLD_HALF_CELLS&&
           gz>=-(int32_t)ODG_WORLD_HALF_CELLS&&gz<(int32_t)ODG_WORLD_HALF_CELLS)continue;
        if(!world_point_maybe_visible(c,cx,cz,7.0f))continue;
        h00=terrain_yf(x0,z0)+distant_relief_corner(x0,z0);
        h10=terrain_yf(x1,z0)+distant_relief_corner(x1,z0);
        h11=terrain_yf(x1,z1)+distant_relief_corner(x1,z1);
        h01=terrain_yf(x0,z1)+distant_relief_corner(x0,z1);
        hc=(h00+h10+h11+h01)*0.25f;
        /* Continuous elevation material removes the three horizontal colour shelves that
         * used to reveal the coarse far-terrain mesh at the skyline. */
        if(hc<=1.25f){
            uint32_t t=hc<=0.15f?0u:(uint32_t)(((hc-0.15f)/1.10f)*256.0f);
            if(t>256u)t=256u;
            base=rgba_lerp(p.land_low,p.land_mid,t);
        }else{
            uint32_t t=hc>=4.40f?256u:(uint32_t)(((hc-1.25f)/3.15f)*256.0f);
            if(t>256u)t=256u;
            base=rgba_lerp(p.land_mid,p.land_high,t);
        }
        {
            float dsx=((h10+h11)-(h00+h01))*0.0625f;
            float dsz=((h01+h11)-(h00+h10))*0.0625f;
            float slope=(dsx<0.0f?-dsx:dsx)+(dsz<0.0f?-dsz:dsz);
            float sun=0.98f-dsx*g_sun_world_x*0.14f-dsz*g_sun_world_z*0.14f;
            if(sun<0.84f)sun=0.84f;
            if(sun>1.10f)sun=1.10f;
            if(slope>0.11f){
                uint32_t exposed=(uint32_t)((slope-0.11f)*54.0f);
                if(exposed>24u)exposed=24u;
                base=rgba_lerp(base,p.rock,exposed);
            }
            base=rgba_mix(base,sun);
        }
        {
            int64_t center_gx=odg_floor_div_i64_internal(g_render_center_global_fx_x,(int64_t)ODG_FX_ONE);
            int64_t center_gz=odg_floor_div_i64_internal(g_render_center_global_fx_z,(int64_t)ODG_FX_ONE);
            noise=visual_smooth_noise_scale(center_gx+(int64_t)gx,center_gz+(int64_t)gz,56u,UINT32_C(0x91e10da5));
            base=rgba_mix(base,0.974f+((float)noise/255.0f)*0.038f);
        }
        {
            float ax=cx<0.0f?-cx:cx,az=cz<0.0f?-cz:cz;
            float far=ax>az?ax:az;
            float u=(far-(float)ODG_WORLD_HALF_CELLS)/96.0f;
            uint32_t aerial;
            if(u<0.0f)u=0.0f;
            if(u>1.0f)u=1.0f;
            u=smoothstep01(u);
            aerial=18u+(uint32_t)(u*64.0f);
            base=rgba_lerp(base,p.fog,aerial);
        }
        quad(c,(rv3){x0,h00-0.052f,z0},(rv3){x1,h10-0.052f,z0},(rv3){x1,h11-0.052f,z1},(rv3){x0,h01-0.052f,z1},base);
    }}
}

static uint32_t rgba_average4(uint32_t a,uint32_t b,uint32_t c,uint32_t d){
    uint32_t r=(((a>>24)&255u)+((b>>24)&255u)+((c>>24)&255u)+((d>>24)&255u)+2u)>>2u;
    uint32_t g=(((a>>16)&255u)+((b>>16)&255u)+((c>>16)&255u)+((d>>16)&255u)+2u)>>2u;
    uint32_t bl=(((a>>8)&255u)+((b>>8)&255u)+((c>>8)&255u)+((d>>8)&255u)+2u)>>2u;
    return (r<<24)|(g<<16)|(bl<<8)|255u;
}

/* One material sample per terrain vertex.  The terrain heightfield is authoritative, but
 * its visual response is continuous: macro vegetation, exposed mineral, biome tint and
 * directional light all derive from global coordinates.  Caching this once per frame is
 * both smoother and substantially cheaper than evaluating the material four times for
 * every near camera cell. */
static uint32_t terrain_vertex_material(const visual_palette *p,
                                        const uint32_t *biome_cache,uint32_t biome_w,uint32_t biome_h,
                                        int64_t biome_min_cx,int64_t biome_min_cz,
                                        int64_t world_gx,int64_t world_gz,float h,float sx,float sz,float curvature){
    uint32_t noise=visual_smooth_noise_scale(world_gx,world_gz,14u,UINT32_C(0x53a91d27));
    uint32_t smooth_b=visual_smooth_noise_scale(world_gx,world_gz,24u,UINT32_C(0xb8472c61));
    uint32_t macro=visual_smooth_noise_scale(world_gx,world_gz,40u,UINT32_C(0x1f3d5b79));
    uint32_t base,shade;
    uint8_t owner;
    float light=(0.48f+0.60f*g_daylight_f)-sx*g_sun_world_x*0.185f-sz*g_sun_world_z*0.185f;
    float local_light=0.0f;
    float grain=((float)noise-127.5f)*0.00024f;
    if(g_daylight_f<0.90f&&g_odg.artifact_count!=0u){
        int64_t local_cell_x=world_gx-odg_global_center_cell_x_internal();
        int64_t local_cell_z=world_gz-odg_global_center_cell_z_internal();
        int64_t local_fx_x=local_cell_x*(int64_t)ODG_FX_ONE;
        int64_t local_fx_z=local_cell_z*(int64_t)ODG_FX_ONE;
        if(local_fx_x>=INT32_MIN&&local_fx_x<=INT32_MAX&&local_fx_z>=INT32_MIN&&local_fx_z<=INT32_MAX){
            uint32_t torch=odg_artifact_light_permille_internal((int32_t)local_fx_x,(int32_t)local_fx_z);
            local_light=((float)torch/1000.0f)*(0.46f*(1.0f-g_daylight_f));
            light+=local_light;
        }
    }
    if(light<0.58f)light=0.58f;
    if(light>1.18f)light=1.18f;
    if(h<=0.70f){
        uint32_t t=h<=0.20f?0u:(uint32_t)(((h-0.20f)/0.50f)*256.0f);
        if(t>256u)t=256u;
        base=rgba_lerp(p->land_low,p->land_mid,t);
    }else if(h<=1.95f){
        uint32_t t=(uint32_t)(((h-0.70f)/1.25f)*256.0f);
        if(t>256u)t=256u;
        base=rgba_lerp(p->land_mid,p->land_high,t);
    }else{
        base=p->land_high;
    }
    if(macro>128u)base=rgba_lerp(base,p->land_high,10u+((macro-128u)*34u)/127u);
    else base=rgba_lerp(base,p->land_low,10u+((128u-macro)*31u)/128u);
    {
        float macro_exposure=0.966f+((float)macro/255.0f)*0.064f;
        base=rgba_mix(base,macro_exposure);
    }
    base=rgba_lerp(base,p->rock,2u+(smooth_b*8u)/255u);
    base=rgba_lerp(base,p->coast,1u+((255u-noise)*5u)/255u);
    /* Reuse the existing low-frequency fields as material masks. This broadens the
     * palette at landscape scale without adding another noise sample or exposing cells. */
    if(smooth_b<128u){
        uint32_t dry=((128u-smooth_b)*34u)/128u;
        base=rgba_lerp(base,p->coast,dry);
    }else if(smooth_b>128u&&h<1.48f){
        uint32_t damp=((smooth_b-128u)*28u)/127u;
        if(macro<112u)damp=(damp*3u)/4u;
        base=rgba_lerp(base,p->leaf_low,damp);
    }
    if(macro>184u&&noise<132u)base=rgba_lerp(base,p->rock,1u+((macro-184u)*7u)/71u);
    base=biome_surface_transition(base,p,biome_cache,biome_w,biome_h,
                                  biome_min_cx,biome_min_cz,world_gx,world_gz);
    {
        float asx=sx<0.0f?-sx:sx,asz=sz<0.0f?-sz:sz;
        float slope=asx+asz;
        if(slope>0.090f){
            uint32_t rock_mix=(uint32_t)((slope-0.090f)*88.0f);
            if(rock_mix>32u)rock_mix=32u;
            base=rgba_lerp(base,p->rock,rock_mix);
        }else if(noise>146u&&macro>110u){
            uint32_t organic=((noise-146u)*12u)/109u;
            if(organic>12u)organic=12u;
            base=rgba_lerp(base,p->leaf_low,organic);
        }
        /* Cheap terrain self-occlusion from the already cached heightfield. Positive
         * curvature means the vertex sits below its four neighbours: darken that shallow
         * concavity and bias it very slightly toward organic material. Negative curvature
         * catches exposed crests. This is presentation-only and adds no world queries. */
        if(curvature>0.014f){
            float cavity=1.0f-(curvature-0.014f)*0.72f;
            uint32_t organic=(uint32_t)((curvature-0.014f)*145.0f);
            if(cavity<0.935f)cavity=0.935f;
            if(organic>10u)organic=10u;
            base=rgba_mix(base,cavity);
            base=rgba_lerp(base,p->leaf_low,organic);
        }else if(curvature<-0.018f){
            uint32_t crest=(uint32_t)((-curvature-0.018f)*118.0f);
            if(crest>9u)crest=9u;
            base=rgba_lerp(base,p->coast_edge,crest);
        }
        if(h<0.72f&&smooth_b>136u){
            uint32_t valley=((smooth_b-136u)*8u)/119u;
            if(valley>8u)valley=8u;
            base=rgba_lerp(base,p->leaf_low,valley);
        }else if(h>1.25f&&macro<106u){
            uint32_t dry=((106u-macro)*8u)/106u;
            if(dry>8u)dry=8u;
            base=rgba_lerp(base,p->coast,dry);
        }
    }
    shade=rgba_mix(base,light+grain);
    if(local_light>0.01f){
        uint32_t warm=(uint32_t)(local_light*96.0f);
        if(warm>42u)warm=42u;
        shade=rgba_lerp(shade,0xd99048ffu,warm);
    }
    if(light>1.01f){
        uint32_t warm=(uint32_t)((light-1.01f)*205.0f);
        if(warm>22u)warm=22u;
        shade=rgba_lerp(shade,p->coast_edge,warm);
    }else if(light<0.97f){
        uint32_t cool=(uint32_t)((0.97f-light)*118.0f);
        if(cool>16u)cool=16u;
        shade=rgba_lerp(shade,p->fog,cool);
    }
    owner=odg_chunk_owner_at_global_cell(world_gx,world_gz);
    if(owner!=ODG_OWNER_NONE){
        uint32_t owner_id=ODG_ID_FROM_OWNER(owner);
        uint32_t mix=owner_id==ODG_PLAYER_ID?42u:30u;
        shade=rgba_lerp(shade,actor_base_color(owner_id),mix);
    }
    return shade;
}

static void ground_cell_quad(const rcam *c,float x0,float z0,float x1,float z1,
                             float h00,float h10,float h11,float h01,uint32_t shade){
    quad(c,(rv3){x0,h00-0.050f,z0},(rv3){x1,h10-0.050f,z0},
           (rv3){x1,h11-0.050f,z1},(rv3){x0,h01-0.050f,z1},shade);
}

static void ground_and_grid(const rcam *c){
    enum { TERRAIN_CACHE_SIDE=ODG_GRID_SIZE+3u, TERRAIN_COLOR_SIDE=ODG_GRID_SIZE+1u };
    static float height_cache[TERRAIN_CACHE_SIDE*TERRAIN_CACHE_SIDE];
    static uint32_t color_cache[TERRAIN_COLOR_SIDE*TERRAIN_COLOR_SIDE];
    visual_palette p=palette();
    int64_t active_min_cx=odg_floor_div_i64_internal(g_render_origin_cell_x,(int64_t)ODG_CHUNK_SIZE_CELLS);
    int64_t active_min_cz=odg_floor_div_i64_internal(g_render_origin_cell_z,(int64_t)ODG_CHUNK_SIZE_CELLS);
    int64_t active_max_cx=odg_floor_div_i64_internal(g_render_origin_cell_x+(int64_t)ODG_GRID_SIZE-1,(int64_t)ODG_CHUNK_SIZE_CELLS);
    int64_t active_max_cz=odg_floor_div_i64_internal(g_render_origin_cell_z+(int64_t)ODG_GRID_SIZE-1,(int64_t)ODG_CHUNK_SIZE_CELLS);
    int64_t biome_min_cx=active_min_cx-1,biome_min_cz=active_min_cz-1;
    int64_t biome_max_cx=active_max_cx+1,biome_max_cz=active_max_cz+1;
    uint32_t biome_w=(uint32_t)(biome_max_cx-biome_min_cx+1),biome_h=(uint32_t)(biome_max_cz-biome_min_cz+1);
    uint32_t biome_cache[49];
    uint32_t bz_i,bx_i,z;
    if(biome_w>7u)biome_w=7u;
    if(biome_h>7u)biome_h=7u;
    for(bz_i=0u;bz_i<biome_h;++bz_i){
        for(bx_i=0u;bx_i<biome_w;++bx_i){
            odg_chunk_descriptor d;uint64_t required=0u;
            biome_cache[bz_i*7u+bx_i]=ODG_BIOME_PLAIN;
            if(odg_chunk_descriptor_get(biome_min_cx+(int64_t)bx_i,biome_min_cz+(int64_t)bz_i,&d,sizeof(d),&required)==ODG_STATUS_OK)
                biome_cache[bz_i*7u+bx_i]=d.biome;
        }
    }
    /* Cache the active heightfield including a one-vertex normal border.  The same
     * authoritative height samples feed geometry and broad visual normals, eliminating
     * duplicated world-height queries while retaining the exact gameplay surface. */
    for(z=0u;z<TERRAIN_CACHE_SIDE;++z){
        uint32_t x;
        float lz=-(float)ODG_WORLD_HALF_CELLS-1.0f+(float)z;
        for(x=0u;x<TERRAIN_CACHE_SIDE;++x){
            float lx=-(float)ODG_WORLD_HALF_CELLS-1.0f+(float)x;
            height_cache[z*TERRAIN_CACHE_SIDE+x]=terrain_yf(lx,lz);
        }
    }
    for(z=0u;z<TERRAIN_COLOR_SIDE;++z){
        uint32_t x;
        for(x=0u;x<TERRAIN_COLOR_SIDE;++x){
            uint32_t hi=(z+1u)*TERRAIN_CACHE_SIDE+(x+1u);
            float sx=(height_cache[hi+1u]-height_cache[hi-1u])*0.5f;
            float sz=(height_cache[hi+TERRAIN_CACHE_SIDE]-height_cache[hi-TERRAIN_CACHE_SIDE])*0.5f;
            float curvature=(height_cache[hi-1u]+height_cache[hi+1u]+
                             height_cache[hi-TERRAIN_CACHE_SIDE]+height_cache[hi+TERRAIN_CACHE_SIDE])*0.25f-
                            height_cache[hi];
            int64_t world_gx=g_render_origin_cell_x+(int64_t)x;
            int64_t world_gz=g_render_origin_cell_z+(int64_t)z;
            color_cache[z*TERRAIN_COLOR_SIDE+x]=terrain_vertex_material(&p,biome_cache,biome_w,biome_h,
                                                                         biome_min_cx,biome_min_cz,
                                                                         world_gx,world_gz,height_cache[hi],sx,sz,curvature);
        }
    }
    /* Near terrain receives RGB interpolation over the unchanged 1m geometry; the far
     * field uses the average cached material because atmospheric perspective already hides
     * sub-cell gradients there. This preserves the improvement while bounding mobile cost. */
    for(z=0u;z<ODG_GRID_SIZE;++z){
        uint32_t x;
        for(x=0u;x<ODG_GRID_SIZE;++x){
            float x0=-(float)ODG_WORLD_HALF_CELLS+(float)x,x1=x0+1.0f;
            float z0=-(float)ODG_WORLD_HALF_CELLS+(float)z,z1=z0+1.0f;
            float cx=x0+0.5f,cz=z0+0.5f,dx=cx-c->cam_x,dz=cz-c->cam_z,d2=dx*dx+dz*dz;
            uint32_t hi=(z+1u)*TERRAIN_CACHE_SIDE+(x+1u);
            float h00=height_cache[hi],h10=height_cache[hi+1u];
            float h01=height_cache[hi+TERRAIN_CACHE_SIDE],h11=height_cache[hi+TERRAIN_CACHE_SIDE+1u];
            uint32_t ci=z*TERRAIN_COLOR_SIDE+x;
            uint32_t c00,c10,c11,c01;
            if(!world_point_maybe_visible(c,cx,cz,0.78f))continue;
            c00=color_cache[ci];c10=color_cache[ci+1u];
            c01=color_cache[ci+TERRAIN_COLOR_SIDE];c11=color_cache[ci+TERRAIN_COLOR_SIDE+1u];
            if(d2<441.0f){
                quad_color(c,(rv3){x0,h00-0.050f,z0},c00,(rv3){x1,h10-0.050f,z0},c10,
                             (rv3){x1,h11-0.050f,z1},c11,(rv3){x0,h01-0.050f,z1},c01);
            }else{
                ground_cell_quad(c,x0,z0,x1,z1,h00,h10,h11,h01,rgba_average4(c00,c10,c11,c01));
            }
        }
    }
}

static void render_surface_water(const rcam *c){
    const float water_y=0.220f;
    int z;
    /* Coarse translucent water is layered over the authoritative terrain substrate.
     * Sampling the exact terrain function prevents coast seams when the floating origin moves. */
    for(z=-ODG_WORLD_HALF_CELLS;z<ODG_WORLD_HALF_CELLS;z+=2){
        int x;for(x=-ODG_WORLD_HALF_CELLS;x<ODG_WORLD_HALF_CELLS;x+=2){
            float x0=(float)x,z0=(float)z,x1=x0+2.0f,z1=z0+2.0f;
            float h00=terrain_yf(x0,z0),h10=terrain_yf(x1,z0),h01=terrain_yf(x0,z1),h11=terrain_yf(x1,z1);
            float min_h=h00;if(h10<min_h)min_h=h10;if(h01<min_h)min_h=h01;if(h11<min_h)min_h=h11;
            if(min_h>=water_y||!world_point_maybe_visible(c,x0+1.0f,z0+1.0f,1.5f))continue;
            tri_alpha(c,(rv3){x0,water_y,z0},(rv3){x1,water_y,z0},(rv3){x1,water_y,z1},0x5f9fb866u);
            tri_alpha(c,(rv3){x0,water_y,z0},(rv3){x1,water_y,z1},(rv3){x0,water_y,z1},0x5f9fb866u);
        }
    }
}

static uint32_t weather_mix32(uint32_t x){x^=x>>16u;x*=UINT32_C(0x7feb352d);x^=x>>15u;x*=UINT32_C(0x846ca68b);x^=x>>16u;return x;}
static void render_weather_rain(const rcam *c){
    uint32_t rain=g_odg.weather_rain_permille;uint32_t count,i;
    if(rain<40u)return;
    count=10u+(rain*54u)/1000u;
    for(i=0u;i<count;++i){
        uint32_t h=weather_mix32(i*7919u+(uint32_t)(g_odg.tick/2u));
        float x=c->cam_x+((float)((int32_t)(h&1023u)-512)/512.0f)*9.0f;
        float z=c->cam_z+((float)((int32_t)((h>>10u)&1023u)-512)/512.0f)*9.0f;
        float phase=(float)((h+(uint32_t)(g_odg.tick*37u))&1023u)/1023.0f;
        float ground=terrain_yf(x,z);float y=ground+0.4f+(1.0f-phase)*5.5f;
        line_overlay(c,(rv3){x,y,z},(rv3){x-0.035f,y-0.42f,z+0.025f},0xbcd9e2a8u);
    }
}

static void render_routes(const rcam *c) {
    /* v15 Open Domain has no fixed arena road mask. A future route system must be
     * generated from global chunk coordinates so it cannot jump on recenter. */
    (void)c;
}

static void ground_ribbon_line(const rcam *c,float ax,float az,float bx,float bz,
                               float halfw,float yoff,uint32_t col){
    float dx=bx-ax,dz=bz-az,den=(dx<0.0f?-dx:dx)+(dz<0.0f?-dz:dz);
    float nx=0.0f,nz=0.0f;
    if(den<=0.0001f)return;
    nx=-dz*(halfw/den);nz=dx*(halfw/den);
    quad(c,(rv3){ax+nx,terrain_yf(ax+nx,az+nz)+yoff,az+nz},
           (rv3){ax-nx,terrain_yf(ax-nx,az-nz)+yoff,az-nz},
           (rv3){bx-nx,terrain_yf(bx-nx,bz-nz)+yoff,bz-nz},
           (rv3){bx+nx,terrain_yf(bx+nx,bz+nz)+yoff,bz+nz},col);
}

static void territory_contour_line(const rcam *c,float ax,float az,float bx,float bz,uint32_t col) {
    visual_palette p=palette();
    float mx=(ax+bx)*0.5f,mz=(az+bz)*0.5f,dx=mx-c->cam_x,dz=mz-c->cam_z,d2=dx*dx+dz*dz;
    float outer_w=0.068f,inner_w=0.023f;
    uint32_t shoulder,core;
    if(d2>1764.0f)return;
    shoulder=rgba_lerp(rgba_mix(p.road,0.64f),col,38u);
    core=rgba_lerp(rgba_mix(col,0.84f),p.land_mid,34u);
    /* Beyond ~28m the ownership stain itself already communicates the region. Narrowing
     * and fading the inlay prevents marching-square pieces from becoming dotted pixels on
     * the horizon, while nearby boundaries keep their precise physical read. */
    if(d2>784.0f){
        float fade=(d2-784.0f)/(1764.0f-784.0f);
        if(fade>1.0f)fade=1.0f;
        outer_w=0.068f-(0.031f*fade);inner_w=0.023f-(0.011f*fade);
        shoulder=rgba_lerp(shoulder,p.land_mid,28u+(uint32_t)(fade*72.0f));
        core=rgba_lerp(core,p.land_mid,18u+(uint32_t)(fade*74.0f));
    }
    ground_ribbon_line(c,ax,az,bx,bz,outer_w,0.078f,shoulder);
    ground_disc16(c,ax,az,outer_w,0.078f,shoulder);
    ground_disc16(c,bx,bz,outer_w,0.078f,shoulder);
    ground_ribbon_line(c,ax,az,bx,bz,inner_w,0.089f,core);
    ground_disc16(c,ax,az,inner_w,0.089f,core);
    ground_disc16(c,bx,bz,inner_w,0.089f,core);
}

static void territory_contour_edge_points(float x0,float z0,uint32_t edge,float *out_x,float *out_z) {
    if(edge==0u){*out_x=x0+0.5f;*out_z=z0;}
    else if(edge==1u){*out_x=x0+1.0f;*out_z=z0+0.5f;}
    else if(edge==2u){*out_x=x0+0.5f;*out_z=z0+1.0f;}
    else {*out_x=x0;*out_z=z0+0.5f;}
}

static void territory_contour_pair(const rcam *c,float x0,float z0,uint32_t ea,uint32_t eb,uint32_t col) {
    float ax,az,bx,bz;
    territory_contour_edge_points(x0,z0,ea,&ax,&az);
    territory_contour_edge_points(x0,z0,eb,&bx,&bz);
    territory_contour_line(c,ax,az,bx,bz,col);
}

static void render_territory_edges(const rcam *c) {
    uint32_t owner_id;
    /* Marching squares runs on ownership sampled at cell centres. Chunk borders query
     * the global ledger, so a contour crossing a streamed-window edge has no seam. */
    for(owner_id=0u;owner_id<ODG_MAX_ACTORS;++owner_id) {
        uint8_t owner=ODG_OWNER_FROM_ID(owner_id);
        uint32_t col=rgba_mix(actor_base_color(owner_id),owner_id==ODG_PLAYER_ID?0.98f:0.80f);
        int32_t z;
        for(z=-1;z<(int32_t)ODG_GRID_SIZE;++z) {
            int32_t x;
            for(x=-1;x<(int32_t)ODG_GRID_SIZE;++x) {
                int64_t gx=g_render_origin_cell_x+(int64_t)x;
                int64_t gz=g_render_origin_cell_z+(int64_t)z;
                uint32_t mask=0u;
                float x0=-(float)ODG_WORLD_HALF_CELLS+(float)x+0.5f;
                float z0=-(float)ODG_WORLD_HALF_CELLS+(float)z+0.5f;
                if(!world_point_maybe_visible(c,x0+0.5f,z0+0.5f,1.5f)) continue;
                if(odg_chunk_owner_at_global_cell(gx,gz)==owner)mask|=1u;
                if(odg_chunk_owner_at_global_cell(gx+1,gz)==owner)mask|=2u;
                if(odg_chunk_owner_at_global_cell(gx+1,gz+1)==owner)mask|=4u;
                if(odg_chunk_owner_at_global_cell(gx,gz+1)==owner)mask|=8u;
                switch(mask) {
                    case 0u:case 15u:break;
                    case 1u: territory_contour_pair(c,x0,z0,3u,0u,col);break;
                    case 2u: territory_contour_pair(c,x0,z0,0u,1u,col);break;
                    case 3u: territory_contour_pair(c,x0,z0,3u,1u,col);break;
                    case 4u: territory_contour_pair(c,x0,z0,1u,2u,col);break;
                    case 5u:
                        /* Stable ambiguity choice from global parity avoids frame-to-frame
                         * topology flicker while keeping isolated corners separate. */
                        if(((gx^gz)&INT64_C(1))==0){territory_contour_pair(c,x0,z0,3u,0u,col);territory_contour_pair(c,x0,z0,1u,2u,col);}
                        else{territory_contour_pair(c,x0,z0,0u,1u,col);territory_contour_pair(c,x0,z0,2u,3u,col);}break;
                    case 6u: territory_contour_pair(c,x0,z0,0u,2u,col);break;
                    case 7u: territory_contour_pair(c,x0,z0,3u,2u,col);break;
                    case 8u: territory_contour_pair(c,x0,z0,2u,3u,col);break;
                    case 9u: territory_contour_pair(c,x0,z0,0u,2u,col);break;
                    case 10u:
                        if(((gx^gz)&INT64_C(1))==0){territory_contour_pair(c,x0,z0,0u,1u,col);territory_contour_pair(c,x0,z0,2u,3u,col);}
                        else{territory_contour_pair(c,x0,z0,3u,0u,col);territory_contour_pair(c,x0,z0,1u,2u,col);}break;
                    case 11u: territory_contour_pair(c,x0,z0,1u,2u,col);break;
                    case 12u: territory_contour_pair(c,x0,z0,3u,1u,col);break;
                    case 13u: territory_contour_pair(c,x0,z0,0u,1u,col);break;
                    case 14u: territory_contour_pair(c,x0,z0,3u,0u,col);break;
                    default:break;
                }
            }
        }
    }
}


static void trail_ribbon_segment(const rcam *c,int32_t ax_fx,int32_t az_fx,
                                 int32_t bx_fx,int32_t bz_fx,float halfw,
                                 float yoff,uint32_t col) {
    int64_t dx_fx=(int64_t)bx_fx-ax_fx;
    int64_t dz_fx=(int64_t)bz_fx-az_fx;
    uint64_t len2=(uint64_t)(dx_fx*dx_fx+dz_fx*dz_fx);
    uint32_t len_fx;
    float ax,az,bx,bz,nx,nz;
    if(len2<(uint64_t)(ODG_FX_ONE/16)*(uint64_t)(ODG_FX_ONE/16)) return;
    len_fx=odg_isqrt_u64(len2);
    if(len_fx==0u) return;
    ax=odg_fx_to_float(ax_fx);az=odg_fx_to_float(az_fx);
    bx=odg_fx_to_float(bx_fx);bz=odg_fx_to_float(bz_fx);
    nx=-(float)dz_fx/(float)len_fx*halfw;
    nz=(float)dx_fx/(float)len_fx*halfw;
    quad(c,
         (rv3){ax+nx,terrain_yf(ax+nx,az+nz)+yoff,az+nz},
         (rv3){ax-nx,terrain_yf(ax-nx,az-nz)+yoff,az-nz},
         (rv3){bx-nx,terrain_yf(bx-nx,bz-nz)+yoff,bz-nz},
         (rv3){bx+nx,terrain_yf(bx+nx,bz+nz)+yoff,bz+nz},col);
}

static void render_trails(const rcam *c) {
    uint32_t id;
    visual_palette p=palette();
    float beat=music_visual_beat();
    float outer_w=0.124f+beat*0.005f;
    float inner_w=0.078f+beat*0.003f;
    for(id=0u;id<ODG_MAX_ACTORS;++id) {
        const odg_actor *a=&g_odg.actors[id];
        uint32_t i;
        uint32_t outer,inner,signal;
        if(!a->active||a->hp==0u||!a->trail_active||a->trail_path_len==0u) continue;
        signal=actor_base_color(id);
        /* A trail is a physical trace in the domain, not a luminous HUD ribbon. The
         * mineral bed anchors it to the ground while the narrow core preserves identity. */
        outer=rgba_lerp(rgba_mix(p.road,0.62f),signal,id==ODG_PLAYER_ID?44u:34u);
        inner=rgba_lerp(rgba_mix(signal,id==ODG_PLAYER_ID?0.92f:0.84f),p.coast_edge,id==ODG_PLAYER_ID?24u:18u);
        if(beat>0.0f)inner=rgba_mix(inner,1.0f+beat*0.020f);
        for(i=1u;i<a->trail_path_len;++i) {
            int32_t ax,az,bx,bz;
            float midx,midz;
            if(!game_local_to_render_fx(a->trail_path_x[i-1u],a->trail_path_z[i-1u],&ax,&az) ||
               !game_local_to_render_fx(a->trail_path_x[i],a->trail_path_z[i],&bx,&bz))continue;
            midx=odg_fx_to_float((int32_t)(((int64_t)ax+bx)/2));
            midz=odg_fx_to_float((int32_t)(((int64_t)az+bz)/2));
            if(!world_point_maybe_visible(c,midx,midz,1.2f)) continue;
            trail_ribbon_segment(c,ax,az,bx,bz,outer_w,0.072f,outer);
            trail_ribbon_segment(c,ax,az,bx,bz,inner_w,0.077f,inner);
            if(i+1u<a->trail_path_len){
                int64_t d1x=(int64_t)a->trail_path_x[i]-a->trail_path_x[i-1u];
                int64_t d1z=(int64_t)a->trail_path_z[i]-a->trail_path_z[i-1u];
                int64_t d2x=(int64_t)a->trail_path_x[i+1u]-a->trail_path_x[i];
                int64_t d2z=(int64_t)a->trail_path_z[i+1u]-a->trail_path_z[i];
                if(d1x*d2z!=d1z*d2x){
                    float px=odg_fx_to_float(bx),pz=odg_fx_to_float(bz);
                    ground_disc16(c,px,pz,outer_w,0.072f,outer);
                    ground_disc16(c,px,pz,inner_w,0.077f,inner);
                }
            }
        }
        {
            uint32_t last=a->trail_path_len-1u;
            int32_t ax,az,bx,bz;
            if(!game_local_to_render_fx(a->trail_path_x[last],a->trail_path_z[last],&ax,&az) ||
               !game_local_to_render_fx(a->x,a->z,&bx,&bz))continue;
            trail_ribbon_segment(c,ax,az,bx,bz,outer_w,0.072f,outer);
            trail_ribbon_segment(c,ax,az,bx,bz,inner_w,0.077f,inner);
            if(world_point_maybe_visible(c,odg_fx_to_float(ax),odg_fx_to_float(az),0.8f)) {
                float px=odg_fx_to_float(ax),pz=odg_fx_to_float(az);
                ground_disc16(c,px,pz,outer_w,0.072f,outer);
                ground_disc16(c,px,pz,inner_w,0.077f,inner);
            }
        }
    }
}



static void render_boundaries(const rcam *c) {
    (void)c; /* API v8 uses an irregular coastline instead of a square wall. */
}

static void render_building(const rcam *c,float x,float z,float hx,float hz,float h,uint32_t base) {
    float y=terrain_yf(x,z);
    float upper=h*0.12f;
    uint32_t wall=rgba_mix(base,0.90f);
    uint32_t roof=rgba_mix(base,0.62f);
    uint32_t trim=rgba_mix(base,1.02f);
    uint32_t glass=rgba_mix(palette().glass,0.82f);
    uint32_t band;
    if (upper<0.28f) upper=0.28f;
    ground_shadow(c,x,z,hx*1.04f,hz*1.04f,1.0f);
    box_y(c,x,z,hx*1.08f,hz*1.08f,y,0.16f,rgba_mix(base,0.42f));
    box_y(c,x,z,hx,hz,y+0.14f,h,wall);
    /* Recessed upper mass and a thin coping line establish architectural scale
     * without turning the skyline into stacked toy blocks. */
    box_y(c,x,z,hx*0.78f,hz*0.76f,y+h+0.14f,upper,roof);
    box_y(c,x,z,hx*0.84f,hz*0.82f,y+h+upper+0.14f,0.055f,trim);
    if (hx>0.70f && hz>0.70f) {
        for(band=0u;band<3u;++band){
            float wy=y+0.40f+(h-0.70f)*(float)(band+1u)/4.0f;
            box_y(c,x,z+hz+0.022f,hx*0.70f,0.022f,wy,0.095f,glass);
            box_y(c,x-hx-0.022f,z,0.022f,hz*0.68f,wy+0.035f,0.075f,rgba_mix(glass,0.86f));
        }
        box_y(c,x,z+hz+0.028f,hx*0.10f,0.035f,y+0.14f,0.70f,rgba_mix(base,0.46f));
        if (h>4.0f) {
            uint32_t pulse=(uint32_t)((g_odg.tick+(uint64_t)((x+z+128.0f)*5.0f))%UINT64_C(90));
            float glow=(pulse<45u?(float)pulse:(float)(90u-pulse))/45.0f;
            box_y(c,x,z,0.045f,0.045f,y+h+upper+0.19f,0.52f,rgba_mix(palette().accent,0.62f+glow*0.18f));
        }
    }
}

static void render_distant_landmarks(const rcam *c) {
    /* Fixed v14 arena landmarks are retired in Open Domain. */
    (void)c;
}

enum { TREE_VISUAL_MIXED=0u,TREE_VISUAL_BROADLEAF=1u,TREE_VISUAL_CONIFER=2u };

static uint32_t flora_tree_visual_form(uint32_t species_id){
    /* Morphology belongs to presentation, not generic flora gameplay. Keep the mapping
     * explicit here so adding a pear/conifer species never leaks species checks into
     * nutrition, harvesting, AI or persistence. */
    if(species_id==ODG_FLORA_SPECIES_APPLE_TREE)return TREE_VISUAL_BROADLEAF;
    return TREE_VISUAL_MIXED;
}

static void render_tree(const rcam *c,float x,float z,float s,uint32_t visual_form) {
    float y=terrain_yf(x,z);visual_palette p=palette();
    uint32_t hx=(uint32_t)(x*37.0f+4096.0f),hz=(uint32_t)(z*53.0f+8192.0f),v=visual_hash2(hx,hz);
    float lean_x=((float)((v>>5u)&15u)-7.5f)*0.006f*s,lean_z=((float)((v>>9u)&15u)-7.5f)*0.006f*s;
    float beat=(float)odg_music_beat_q16_internal()/65535.0f,sway=(beat*0.016f+0.003f)*s;
    float sx=((v&1u)!=0u?sway:-sway)+lean_x,sz=((v&2u)!=0u?sway:-sway)+lean_z;
    uint32_t low=rgba_mix(p.leaf_low,0.91f+(float)((v>>13u)&7u)*0.028f),high=rgba_mix(p.leaf_high,0.94f+(float)((v>>17u)&7u)*0.026f);
    ground_shadow(c,x,z,0.72f*s,0.60f*s,0.64f);
    /* Large resource trees need a grounded base, not a narrow pole disappearing under a
     * crown. Four low roots, a wider butt and a slimmer upper trunk create scale cues while
     * remaining deterministic low-poly geometry. */
    oriented_box_y(c,x,z, 1.0f, 0.0f, 0.09f*s,0.00f,0.18f*s,0.040f*s,y+0.020f,0.050f*s,rgba_mix(p.trunk,0.72f));
    oriented_box_y(c,x,z, 0.0f, 1.0f,-0.08f*s,0.00f,0.16f*s,0.038f*s,y+0.022f,0.048f*s,rgba_mix(p.trunk,0.76f));
    oriented_box_y(c,x,z, 0.7071f,0.7071f,0.00f,0.08f*s,0.15f*s,0.034f*s,y+0.024f,0.045f*s,rgba_mix(p.trunk,0.80f));
    prism8_y(c,x,z,0.112f*s,y+0.035f,0.31f*s,rgba_mix(p.trunk,0.82f));
    prism8_y(c,x,z,0.086f*s,y+0.28f*s,0.58f*s,rgba_mix(p.trunk,0.92f));
    if(visual_form==TREE_VISUAL_BROADLEAF||(visual_form==TREE_VISUAL_MIXED&&(v&8u)==0u)){
        /* Broadleaf: three suspended crowns. They no longer inherit a geological base
         * ring from the ground, so the silhouette has visible trunk and real air gaps. */
        oriented_box_y(c,x,z,0.7071f,0.7071f,-0.11f,0.05f,0.032f*s,0.19f*s,y+0.59f*s,0.25f*s,rgba_mix(p.trunk,0.92f));
        oriented_box_y(c,x,z,-0.7071f,0.7071f,0.10f,0.05f,0.030f*s,0.18f*s,y+0.64f*s,0.22f*s,rgba_mix(p.trunk,0.88f));
        faceted_canopy(c,x-0.27f*s+sx*0.35f,z+0.10f*s+sz*0.35f,0.56f*s,0.46f*s,y+0.63f*s,0.64f*s,low,v^0x91u);
        faceted_canopy(c,x+0.29f*s+sx*0.60f,z-0.13f*s+sz*0.60f,0.52f*s,0.43f*s,y+0.70f*s,0.61f*s,rgba_mix(low,1.05f),v^0x52u);
        faceted_canopy(c,x-0.05f*s+sx*0.74f,z-0.29f*s+sz*0.74f,0.43f*s,0.37f*s,y+0.77f*s,0.52f*s,rgba_mix(low,0.95f),v^0xb4u);
        faceted_canopy(c,x+0.02f*s+sx,z+0.11f*s+sz,0.46f*s,0.40f*s,y+0.96f*s,0.54f*s,high,v^0x2du);
    }else{
        /* Conifer: irregular eight-sided tapered tiers. Pointed, layered foliage replaces
         * the previous ground-anchored rock masses without returning to pyramid sprites. */
        faceted_cone_y(c,x+sx*0.18f,z+sz*0.18f,0.58f*s,0.52f*s,y+0.43f*s,0.80f*s,rgba_mix(low,0.84f),v^0x31u);
        faceted_cone_y(c,x+sx*0.43f,z+sz*0.43f,0.48f*s,0.43f*s,y+0.72f*s,0.77f*s,rgba_mix(low,0.94f),v^0x63u);
        faceted_cone_y(c,x+sx*0.68f,z+sz*0.68f,0.37f*s,0.33f*s,y+1.02f*s,0.71f*s,low,v^0x77u);
        faceted_cone_y(c,x+sx*0.92f,z+sz*0.92f,0.25f*s,0.22f*s,y+1.31f*s,0.58f*s,high,v^0xc5u);
    }
}

static void render_flora_fruit(const rcam *c,uint32_t species_id,float x,float z,float s,uint32_t count,uint32_t variant){
    static const float offsets[9][3]={
        {-0.49f,-0.08f,0.67f},{ 0.49f, 0.04f,0.70f},{-0.34f,-0.39f,0.64f},
        { 0.31f,-0.42f,0.68f},{-0.43f, 0.31f,0.72f},{ 0.40f, 0.34f,0.75f},
        {-0.12f,-0.49f,0.78f},{ 0.10f, 0.47f,0.81f},{ 0.00f,-0.24f,0.58f}
    };
    uint32_t i,visible,start,col;float y;
    if(species_id!=ODG_FLORA_SPECIES_APPLE_TREE||count==0u)return;
    visible=count<9u?count:9u;start=variant%9u;y=terrain_yf(x,z);
    if((variant%3u)==0u)col=0xc95643ffu;
    else if((variant%3u)==1u)col=0xb74335ffu;
    else col=0xd79b3dffu;
    for(i=0u;i<visible;++i){
        uint32_t oi=(start+i)%9u;float r=0.040f*s;
        octa(c,x+offsets[oi][0]*s,z+offsets[oi][1]*s,r,y+offsets[oi][2]*s,0.080f*s,rgba_mix(col,0.92f+(float)(i%3u)*0.06f));
    }
}

typedef void (*spatial_render_cb)(const rcam *c,uint32_t id);

/* The renderer never walks historical entity arrays.  It asks the derived chunk index
 * only for refs intersecting the current 128x128 render window, then keeps the ordinary
 * frustum test as second-stage culling.  Existence therefore scales independently from
 * presentation cost. */
static void render_visible_spatial(const rcam *c,uint32_t kind,spatial_render_cb cb) {
    const odg_spatial_ref *refs;
    uint32_t count=0u;
    int64_t cx0=odg_floor_div_i64_internal(g_render_origin_cell_x,(int64_t)ODG_CHUNK_SIZE_CELLS);
    int64_t cz0=odg_floor_div_i64_internal(g_render_origin_cell_z,(int64_t)ODG_CHUNK_SIZE_CELLS);
    int64_t cx1=odg_floor_div_i64_internal(g_render_origin_cell_x+(int64_t)ODG_GRID_SIZE-1,(int64_t)ODG_CHUNK_SIZE_CELLS);
    int64_t cz1=odg_floor_div_i64_internal(g_render_origin_cell_z+(int64_t)ODG_GRID_SIZE-1,(int64_t)ODG_CHUNK_SIZE_CELLS);
    int64_t cx;
    if(cb==NULL)return;
    refs=odg_entities_spatial_refs(&count);
    if(refs==NULL||count==0u)return;
    for(cx=cx0;cx<=cx1;++cx){
        uint32_t pos=odg_entities_spatial_lower_bound(cx,cz0);
        while(pos<count && refs[pos].chunk_x==cx && refs[pos].chunk_z<=cz1){
            if(refs[pos].chunk_z>=cz0 && refs[pos].kind==kind)cb(c,refs[pos].id);
            ++pos;
        }
    }
}

/* Sparse silhouettes beyond the streamed gameplay window give Open Domain continuity.
 * They are globally aligned and presentation-only. The 90m inner cutoff keeps their
 * appearance deep inside aerial fog so decorative silhouettes never look reachable. */
static void render_distant_scenery(const rcam *c){
    const int64_t spacing=24;
    int64_t center_x=odg_floor_div_i64_internal(g_render_center_global_fx_x,(int64_t)ODG_FX_ONE);
    int64_t center_z=odg_floor_div_i64_internal(g_render_center_global_fx_z,(int64_t)ODG_FX_ONE);
    int64_t min_x=center_x-216,max_x=center_x+216,min_z=center_z-216,max_z=center_z+216;
    int64_t gx0=odg_floor_div_i64_internal(min_x,spacing)*spacing,gz0=odg_floor_div_i64_internal(min_z,spacing)*spacing;
    int64_t gz;
    visual_palette p=palette();
    for(gz=gz0;gz<=max_z;gz+=spacing){
        int64_t gx;
        for(gx=gx0;gx<=max_x;gx+=spacing){
            uint32_t h=visual_hash_cell64(odg_floor_div_i64_internal(gx,spacing),odg_floor_div_i64_internal(gz,spacing),UINT32_C(0x4f1bbcdc));
            float x,z,ax,az,far,y,s,blend;
            uint32_t low,high;
            if((h&7u)>2u)continue;
            x=render_global_cell_center_local_f(gx,1)+((float)((h>>8u)&255u)/255.0f-0.5f)*9.0f;
            z=render_global_cell_center_local_f(gz,0)+((float)((h>>16u)&255u)/255.0f-0.5f)*9.0f;
            ax=x<0.0f?-x:x;az=z<0.0f?-z:z;far=ax>az?ax:az;
            if(far<90.0f||far>210.0f||!world_point_maybe_visible(c,x,z,3.0f))continue;
            y=terrain_yf(x,z)+distant_relief_corner(x,z)-0.02f;
            s=0.98f+(float)((h>>5u)&15u)*0.040f;
            blend=smoothstep01((far-90.0f)/116.0f);
            low=rgba_lerp(rgba_mix(p.leaf_low,0.78f),p.fog,72u+(uint32_t)(blend*94.0f));
            high=rgba_lerp(rgba_mix(p.leaf_high,0.84f),p.fog,66u+(uint32_t)(blend*98.0f));
            if((h&0x18u)==0u){
                uint32_t rock=rgba_lerp(rgba_mix(p.rock,0.78f),p.fog,76u+(uint32_t)(blend*106.0f));
                octa(c,x,z,0.48f*s,y,0.58f*s,rock);
            }else{
                prism8_y(c,x,z,0.055f*s,y,0.58f*s,rgba_lerp(p.trunk,p.fog,98u+(uint32_t)(blend*96.0f)));
                faceted_cone_y(c,x,z,0.41f*s,0.37f*s,y+0.30f*s,0.92f*s,low,h^0x73u);
                faceted_cone_y(c,x,z,0.29f*s,0.26f*s,y+0.72f*s,0.72f*s,high,h^0xb1u);
            }
        }
    }
}

static void render_resource_node_ref(const rcam *c,uint32_t id) {
    const odg_resource_node *r;
    float x,z,y,scale,tree_scale;
    uint32_t variant;
    visual_palette p=palette();
    if(id>=g_odg.resource_count)return;
    r=&g_odg_resources[id];
    if(!r->active) return;
    x=render_global_fx_local_f(r->global_fx_x,1);z=render_global_fx_local_f(r->global_fx_z,0);
    if(!world_point_maybe_visible(c,x,z,(r->kind==ODG_RESOURCE_TREE||r->kind==ODG_RESOURCE_FLORA)?4.5f:3.0f)) return;
    y=terrain_yf(x,z);
    variant=(uint32_t)(r->stable_id^(r->stable_id>>32u));
    /* Resource silhouettes deliberately dominate the compact cube actor. Trees occupy
     * several vertical metres and ore deposits read as real masses instead of pebbles. */
    scale=1.10f+(float)(variant&255u)/255.0f*0.40f;
    tree_scale=(float)odg_resource_physical_height_milli_internal(r)/1500.0f;
    if(r->kind==ODG_RESOURCE_TREE||r->kind==ODG_RESOURCE_FLORA){
        if(tree_scale<0.12f)tree_scale=0.12f;
        if(r->state==ODG_RESOURCE_STATE_DEPLETED){
            ground_shadow(c,x,z,0.34f,0.29f,0.40f);
            prism8_y(c,x,z,0.17f,y+0.03f,0.34f,rgba_mix(p.trunk,0.76f));
        }else{
            float dx=(float)((int32_t)((variant>>8u)&15u)-7)*0.010f;
            float dz=(float)((int32_t)((variant>>12u)&15u)-7)*0.010f;
            render_tree(c,x+dx,z+dz,tree_scale,flora_tree_visual_form(r->species_id));
            if(r->species_id!=0u&&r->fruit_count!=0u)
                render_flora_fruit(c,r->species_id,x+dx,z+dz,tree_scale,r->fruit_count,r->variant);
        }
    }else if(r->kind==ODG_RESOURCE_STONE){
        uint32_t rock=rgba_mix(p.rock,0.86f+(float)((variant>>5u)&15u)*0.018f);
        if(r->state==ODG_RESOURCE_STATE_DEPLETED){
            faceted_rock(c,x,z,0.25f,0.20f,y,0.11f,rgba_mix(rock,0.62f),variant^0x41u);
        }else{
            ground_shadow(c,x,z,0.74f*scale,0.61f*scale,0.54f);
            faceted_rock(c,x,z,0.72f*scale,0.61f*scale,y+0.01f,0.58f*scale,rock,variant^0x11u);
            faceted_rock(c,x-0.47f*scale,z+0.23f*scale,0.40f*scale,0.33f*scale,y,0.34f*scale,rgba_mix(rock,0.82f),variant^0x72u);
            faceted_rock(c,x+0.41f*scale,z-0.29f*scale,0.37f*scale,0.31f*scale,y,0.31f*scale,rgba_mix(rock,1.10f),variant^0xa3u);
            faceted_rock(c,x+0.14f*scale,z+0.43f*scale,0.29f*scale,0.24f*scale,y,0.25f*scale,rgba_mix(rock,0.94f),variant^0xc4u);
        }
    }else if(r->kind==ODG_RESOURCE_COAL||r->kind==ODG_RESOURCE_IRON){
        uint32_t rock=rgba_mix(p.rock,0.64f);
        uint32_t vein=r->kind==ODG_RESOURCE_COAL?0x292c2dffu:rgba_mix(p.accent,0.78f);
        if(r->state==ODG_RESOURCE_STATE_DEPLETED){
            ground_shadow(c,x,z,0.50f,0.38f,0.72f);
            faceted_rock(c,x+0.24f,z-0.10f,0.22f,0.16f,y,0.12f,rgba_mix(rock,0.52f),variant^0x35u);
        }else{
            /* Geological deposits are exposed seams at a cave mouth, never freestanding
             * ore boulders sprinkled over grass. The dark recess is presentation-only;
             * the deterministic cave + vein queries remain gameplay authority. */
            ground_shadow(c,x,z,0.86f*scale,0.66f*scale,0.88f);
            faceted_rock(c,x-0.58f*scale,z+0.18f*scale,0.34f*scale,0.25f*scale,y,0.24f*scale,rock,variant^0x18u);
            faceted_rock(c,x+0.57f*scale,z+0.16f*scale,0.33f*scale,0.24f*scale,y,0.27f*scale,rgba_mix(rock,0.91f),variant^0x8bu);
            faceted_rock(c,x,z-0.48f*scale,0.48f*scale,0.23f*scale,y,0.30f*scale,rgba_mix(rock,0.78f),variant^0x96u);
            octa(c,x-0.20f*scale,z-0.38f*scale,0.055f*scale,y+0.18f*scale,0.105f*scale,vein);
            octa(c,x+0.08f*scale,z-0.43f*scale,0.052f*scale,y+0.20f*scale,0.098f*scale,rgba_mix(vein,1.08f));
            octa(c,x+0.30f*scale,z-0.32f*scale,0.045f*scale,y+0.16f*scale,0.090f*scale,rgba_mix(vein,0.88f));
        }
    }
}

static void render_fauna(const rcam *c){
    uint32_t i;
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i){
        const odg_fauna_entity *e=&g_odg.fauna[i];uint32_t base;float age_scale=1.0f;
        if(!e->active||!e->local_resident||e->hp==0u)continue;
        if(!world_point_maybe_visible(c,odg_fx_to_float(e->x),odg_fx_to_float(e->z),2.2f))continue;
        if(e->life_stage==ODG_FAUNA_STAGE_YOUNG)age_scale=0.56f;else if(e->life_stage==ODG_FAUNA_STAGE_JUVENILE)age_scale=0.76f;else if(e->life_stage==ODG_FAUNA_STAGE_OLD)age_scale=1.04f;
        if(e->species_id==ODG_FAUNA_SPECIES_FOREST_DEER){
            base=(e->variant&1u)?0x805a3dffu:0x9a704dffu;
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,0.0f,0.34f*age_scale,0.0f,0.22f*age_scale,0.46f*age_scale,0.42f*age_scale,0.0f,base);
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,0.0f,0.50f*age_scale,0.42f*age_scale,0.15f*age_scale,0.16f*age_scale,0.28f*age_scale,0.0f,rgba_mix(base,1.05f));
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,-0.15f*age_scale,0.16f*age_scale,-0.25f*age_scale,0.055f*age_scale,0.07f*age_scale,0.32f*age_scale,0.0f,rgba_mix(base,0.72f));
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15, 0.15f*age_scale,0.16f*age_scale,-0.25f*age_scale,0.055f*age_scale,0.07f*age_scale,0.32f*age_scale,0.0f,rgba_mix(base,0.72f));
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,-0.15f*age_scale,0.16f*age_scale, 0.25f*age_scale,0.055f*age_scale,0.07f*age_scale,0.32f*age_scale,0.0f,rgba_mix(base,0.72f));
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15, 0.15f*age_scale,0.16f*age_scale, 0.25f*age_scale,0.055f*age_scale,0.07f*age_scale,0.32f*age_scale,0.0f,rgba_mix(base,0.72f));
            /* Tail/ears make the silhouette readable at gameplay distance. Adult males
             * get compact antler prongs without changing collision or AI morphology. */
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,0.0f,0.48f*age_scale,-0.49f*age_scale,0.045f*age_scale,0.08f*age_scale,0.14f*age_scale,0.0f,rgba_mix(base,1.18f));
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,-0.11f*age_scale,0.68f*age_scale,0.43f*age_scale,0.035f*age_scale,0.045f*age_scale,0.14f*age_scale,0.0f,rgba_mix(base,0.82f));
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15, 0.11f*age_scale,0.68f*age_scale,0.43f*age_scale,0.035f*age_scale,0.045f*age_scale,0.14f*age_scale,0.0f,rgba_mix(base,0.82f));
            if(e->sex==ODG_FAUNA_SEX_MALE&&e->life_stage>=ODG_FAUNA_STAGE_ADULT){
                uint32_t antler=0xb8a47bffu;
                surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,-0.09f*age_scale,0.78f*age_scale,0.43f*age_scale,0.025f*age_scale,0.025f*age_scale,0.25f*age_scale,0.0f,antler);
                surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15, 0.09f*age_scale,0.78f*age_scale,0.43f*age_scale,0.025f*age_scale,0.025f*age_scale,0.25f*age_scale,0.0f,antler);
                surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,-0.15f*age_scale,0.88f*age_scale,0.43f*age_scale,0.09f*age_scale,0.018f*age_scale,0.035f*age_scale,0.0f,antler);
                surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15, 0.15f*age_scale,0.88f*age_scale,0.43f*age_scale,0.09f*age_scale,0.018f*age_scale,0.035f*age_scale,0.0f,antler);
            }
        }else if(e->species_id==ODG_FAUNA_SPECIES_MEADOW_RABBIT){
            base=(e->variant&1u)?0xa99078ffu:0x776a5dffu;
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,0.0f,0.14f*age_scale,-0.04f,0.16f*age_scale,0.22f*age_scale,0.22f*age_scale,0.0f,base);
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,0.0f,0.22f*age_scale,0.18f*age_scale,0.12f*age_scale,0.12f*age_scale,0.18f*age_scale,0.0f,rgba_mix(base,1.04f));
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,-0.07f,0.40f*age_scale,0.18f*age_scale,0.035f,0.035f,0.26f*age_scale,0.0f,rgba_mix(base,1.08f));
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15, 0.07f,0.40f*age_scale,0.18f*age_scale,0.035f,0.035f,0.26f*age_scale,0.0f,rgba_mix(base,1.08f));
        }else if(e->species_id==ODG_FAUNA_SPECIES_FIELD_FOWL){
            uint32_t accent;
            base=(e->variant&1u)?0xb07a45ffu:0x69513fff;
            accent=(e->sex==ODG_FAUNA_SEX_MALE)?0xc74435ffu:0x9c6a4affu;
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,0.0f,0.18f*age_scale,-0.03f,0.17f*age_scale,0.23f*age_scale,0.24f*age_scale,0.0f,base);
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,0.0f,0.30f*age_scale,0.18f*age_scale,0.11f*age_scale,0.11f*age_scale,0.13f*age_scale,0.0f,rgba_mix(base,1.08f));
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,0.0f,0.31f*age_scale,0.31f*age_scale,0.055f*age_scale,0.035f*age_scale,0.07f*age_scale,0.0f,0xd7a339ffu);
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,0.0f,0.42f*age_scale,0.18f*age_scale,0.04f*age_scale,0.035f*age_scale,0.06f*age_scale,0.0f,accent);
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,-0.075f*age_scale,0.055f*age_scale,-0.02f,0.025f*age_scale,0.025f*age_scale,0.12f*age_scale,0.0f,0x8a6b45ffu);
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,0.075f*age_scale,0.055f*age_scale,-0.02f,0.025f*age_scale,0.025f*age_scale,0.12f*age_scale,0.0f,0x8a6b45ffu);
        }else if(e->species_id==ODG_FAUNA_SPECIES_ORCHARD_BIRD){
            float x=odg_fx_to_float(e->x),z=odg_fx_to_float(e->z),y=terrain_yf(x,z)+odg_fx_to_float(e->y_offset_fx);
            float fx=(float)e->face_x_q15/(float)ODG_Q15_ONE,fz=(float)e->face_z_q15/(float)ODG_Q15_ONE;
            float rx=-fz,rz=fx;
            base=(e->variant&1u)?0x5d7590ffu:0x8b684effu;
            ground_shadow(c,x,z,0.18f,0.13f,0.30f);
            octa(c,x,z,0.13f*age_scale,y+0.12f*age_scale,0.20f*age_scale,base);
            octa(c,x+rx*0.105f*age_scale,z+rz*0.105f*age_scale,0.075f*age_scale,y+0.13f*age_scale,0.075f*age_scale,rgba_mix(base,0.88f));
            octa(c,x-rx*0.105f*age_scale,z-rz*0.105f*age_scale,0.075f*age_scale,y+0.13f*age_scale,0.075f*age_scale,rgba_mix(base,0.88f));
            octa(c,x+fx*0.13f*age_scale,z+fz*0.13f*age_scale,0.08f*age_scale,y+0.20f*age_scale,0.12f*age_scale,rgba_mix(base,1.08f));
            octa(c,x+fx*0.225f*age_scale,z+fz*0.225f*age_scale,0.032f*age_scale,y+0.215f*age_scale,0.045f*age_scale,0xd4a14bffu);
            octa(c,x-fx*0.145f*age_scale,z-fz*0.145f*age_scale,0.055f*age_scale,y+0.11f*age_scale,0.075f*age_scale,rgba_mix(base,0.72f));
        }else if(e->species_id==ODG_FAUNA_SPECIES_RIVER_FISH){
            float yoff=odg_fx_to_float(e->y_offset_fx);
            float tail=(e->variant&1u)?0.16f:0.13f;
            base=(e->variant&1u)?0x4c7f8dffu:0x6b8b78ffu;
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,0.0f,0.14f*age_scale,0.0f,
                        0.10f*age_scale,0.28f*age_scale,0.16f*age_scale,yoff,base);
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,0.0f,0.15f*age_scale,-0.34f*age_scale,
                        0.025f*age_scale,tail*age_scale,0.22f*age_scale,yoff,rgba_mix(base,0.82f));
        }else if(e->species_id==ODG_FAUNA_SPECIES_NIGHT_STALKER){
            uint32_t hide=(e->variant&1u)?0x252a2effu:0x31343affu;
            uint32_t eye=0xc88a42ffu;
            float yoff=odg_fx_to_float(e->y_offset_fx);
            /* Low, long and asymmetrical enough to read as its own creature rather than
             * a recoloured deer/zombie. Bright eyes are tiny orientation cues, not lamps. */
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,0.0f,0.27f*age_scale,-0.05f,
                        0.20f*age_scale,0.42f*age_scale,0.30f*age_scale,yoff,hide);
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,0.0f,0.42f*age_scale,0.39f*age_scale,
                        0.15f*age_scale,0.18f*age_scale,0.22f*age_scale,yoff,rgba_mix(hide,0.88f));
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,-0.13f*age_scale,0.13f*age_scale,0.18f*age_scale,
                        0.045f*age_scale,0.055f*age_scale,0.40f*age_scale,yoff,rgba_mix(hide,0.72f));
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,0.13f*age_scale,0.13f*age_scale,0.18f*age_scale,
                        0.045f*age_scale,0.055f*age_scale,0.40f*age_scale,yoff,rgba_mix(hide,0.72f));
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,-0.13f*age_scale,0.13f*age_scale,-0.27f*age_scale,
                        0.045f*age_scale,0.055f*age_scale,0.36f*age_scale,yoff,rgba_mix(hide,0.70f));
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,0.13f*age_scale,0.13f*age_scale,-0.27f*age_scale,
                        0.045f*age_scale,0.055f*age_scale,0.36f*age_scale,yoff,rgba_mix(hide,0.70f));
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,-0.075f*age_scale,0.50f*age_scale,0.52f*age_scale,
                        0.024f,0.024f,0.035f,yoff,eye);
            surface_box(c,e->x,e->z,e->face_x_q15,e->face_z_q15,0.075f*age_scale,0.50f*age_scale,0.52f*age_scale,
                        0.024f,0.024f,0.035f,yoff,eye);
        }
    }
}

static void render_fauna_nests(const rcam *c){
    uint32_t i;
    for(i=0u;i<ODG_FAUNA_MAX_NESTS;++i){
        const odg_fauna_nest *n=&g_odg.fauna_nests[i];
        const odg_fauna_nesting_definition *profile;
        float x,z,y;
        uint32_t k;
        if(!n->active||!n->local_resident)continue;
        profile=odg_fauna_nesting_internal(n->species_id);
        x=odg_fx_to_float(n->x);z=odg_fx_to_float(n->z);
        if(!world_point_maybe_visible(c,x,z,2.4f))continue;
        y=terrain_yf(x,z)+(float)(profile!=NULL?profile->height_offset_milli:120)/1000.0f;
        if(n->substrate==ODG_NEST_SUBSTRATE_TREE){
            static const float tree_ring[8][2]={{0.19f,0.0f},{0.134f,0.134f},{0.0f,0.19f},{-0.134f,0.134f},{-0.19f,0.0f},{-0.134f,-0.134f},{0.0f,-0.19f},{0.134f,-0.134f}};
            const odg_resource_node *host=NULL;uint32_t ri;
            for(ri=0u;ri<g_odg.resource_count;++ri){
                if(g_odg_resources[ri].active&&g_odg_resources[ri].local_resident&&
                   g_odg_resources[ri].stable_id==n->host_resource_stable_id){host=&g_odg_resources[ri];break;}
            }
            if(host!=NULL){
                int32_t dx=n->x-host->x,dz=n->z-host->z,dir_x,dir_z;
                uint32_t length_fx=odg_isqrt_u64((uint64_t)odg_dist2(n->x,n->z,host->x,host->z));
                float length=odg_fx_to_float((int32_t)length_fx);
                float branch_y=(float)(profile!=NULL?profile->height_offset_milli:120)/1000.0f-0.055f;
                odg_normalize_q15(dx,dz,&dir_x,&dir_z);
                surface_box(c,host->x,host->z,dir_x,dir_z,0.0f,0.0f,length*0.5f,
                            0.038f,length*0.5f+0.025f,0.055f,branch_y,0x5b3f2dffu);
            }else{
                box_y(c,x,z,0.24f,0.035f,y-0.035f,0.04f,0x5b3f2dffu);
            }
            for(k=0u;k<8u;++k)octa(c,x+tree_ring[k][0],z+tree_ring[k][1],0.052f,y,0.065f,0x6c4932ffu);
        }else{
            static const float ground_ring[8][2]={{0.16f,0.0f},{0.113f,0.113f},{0.0f,0.16f},{-0.113f,0.113f},{-0.16f,0.0f},{-0.113f,-0.113f},{0.0f,-0.16f},{0.113f,-0.113f}};
            box_y(c,x,z,0.12f,0.12f,y-0.018f,0.025f,0x66533cffu);
            for(k=0u;k<8u;++k)octa(c,x+ground_ring[k][0],z+ground_ring[k][1],0.042f,y,0.045f,0x806044ffu);
        }
        for(k=0u;k<n->egg_count&&k<3u;++k){
            float dx=((float)(int32_t)k-1.0f)*0.06f;
            uint32_t shell=(k&1u)?0xe2d7c5ffu:0xf0e7d8ffu;
            octa(c,x+dx,z,0.038f,y+0.045f,0.062f,shell);
        }
    }
}

static void render_resource_nodes(const rcam *c) {
    render_visible_spatial(c,ODG_SPATIAL_KIND_RESOURCE,render_resource_node_ref);
}

static uint32_t construction_shell_color(uint32_t material_tier) {
    visual_palette p=palette();
    if(material_tier==ODG_MATERIAL_IRON)return rgba_mix(p.building_alt,0.92f);
    if(material_tier==ODG_MATERIAL_STONE)return rgba_mix(p.rock,0.92f);
    return rgba_mix(p.trunk,0.90f);
}

static void render_construction_module(const rcam *c,float x,float z,float y,uint32_t material_tier,
                                       uint32_t shape,uint32_t face,uint32_t edge,int shadow) {
    float top_y=(float)odg_construction_shape_height_milli_internal(shape)/1000.0f;
    if(shadow!=0){
        float shadow_scale=shape==ODG_CONSTRUCTION_SHAPE_FLOOR?0.96f:
                           shape==ODG_CONSTRUCTION_SHAPE_ROOF?1.02f:0.88f;
        ground_shadow(c,x,z,shadow_scale,shadow_scale,shape==ODG_CONSTRUCTION_SHAPE_ROOF?0.42f:0.62f);
    }
    if(shape==ODG_CONSTRUCTION_SHAPE_FLOOR){
        beveled_box_y(c,x,z,0.72f,0.72f,0.045f,y+0.015f,0.105f,face);
        box_y(c,x,z-0.55f,0.54f,0.012f,y+0.116f,0.012f,edge);
        box_y(c,x,z+0.55f,0.54f,0.012f,y+0.116f,0.012f,edge);
        box_y(c,x-0.55f,z,0.012f,0.54f,y+0.116f,0.012f,edge);
        box_y(c,x+0.55f,z,0.012f,0.54f,y+0.116f,0.012f,edge);
        return;
    }
    if(shape==ODG_CONSTRUCTION_SHAPE_DOORWAY){
        const float post=0.13f,offset=0.57f,lintel=0.17f;
        /* Four corner posts keep the centre traversable from every approach; this
         * orientation-free silhouette matches the zero ground-collider authority. */
        box_y(c,x-offset,z-offset,post,post,y+0.02f,top_y-lintel,face);
        box_y(c,x+offset,z-offset,post,post,y+0.02f,top_y-lintel,face);
        box_y(c,x-offset,z+offset,post,post,y+0.02f,top_y-lintel,face);
        box_y(c,x+offset,z+offset,post,post,y+0.02f,top_y-lintel,face);
        box_y(c,x,z-offset,0.70f,post,y+top_y-lintel,lintel,rgba_mix(face,1.02f));
        box_y(c,x,z+offset,0.70f,post,y+top_y-lintel,lintel,rgba_mix(face,1.02f));
        box_y(c,x-offset,z,post,0.44f,y+top_y-lintel,lintel,rgba_mix(face,0.98f));
        box_y(c,x+offset,z,post,0.44f,y+top_y-lintel,lintel,rgba_mix(face,0.98f));
        return;
    }
    if(shape==ODG_CONSTRUCTION_SHAPE_ROOF){
        const float slab_h=0.20f,underside=1.84f;
        /* Roof is a shallow overhanging cap. construction.c owns support and airspace;
         * the renderer does not invent a second hidden physical body. */
        beveled_box_y(c,x,z,0.80f,0.80f,0.055f,y+underside,slab_h,face);
        box_y(c,x,z-0.66f,0.58f,0.018f,y+underside-0.055f,0.055f,edge);
        box_y(c,x,z+0.66f,0.58f,0.018f,y+underside-0.055f,0.055f,edge);
        box_y(c,x-0.66f,z,0.018f,0.58f,y+underside-0.055f,0.055f,edge);
        box_y(c,x+0.66f,z,0.018f,0.58f,y+underside-0.055f,0.055f,edge);
        return;
    }
    /* WALL is the only solid ground blocker, so its visible mass fills the collider. */
    beveled_box_y(c,x,z,0.72f,0.72f,0.06f,y+0.02f,top_y-0.02f,face);
    if(material_tier==ODG_MATERIAL_WOOD){
        box_y(c,x,z+0.725f,0.64f,0.018f,y+0.34f,0.045f,edge);
        box_y(c,x,z+0.725f,0.64f,0.018f,y+0.86f,0.045f,edge);
        box_y(c,x,z+0.725f,0.64f,0.018f,y+1.38f,0.045f,edge);
    }else if(material_tier==ODG_MATERIAL_STONE){
        box_y(c,x-0.35f,z+0.725f,0.018f,0.018f,y+0.24f,1.38f,edge);
        box_y(c,x+0.35f,z+0.725f,0.018f,0.018f,y+0.24f,1.38f,edge);
        box_y(c,x,z+0.725f,0.66f,0.018f,y+0.88f,0.025f,edge);
    }else{
        box_y(c,x,z+0.725f,0.58f,0.018f,y+0.14f,0.055f,edge);
        box_y(c,x,z+0.725f,0.58f,0.018f,y+1.60f,0.055f,edge);
        box_y(c,x-0.58f,z+0.725f,0.025f,0.018f,y+0.22f,1.34f,edge);
        box_y(c,x+0.58f,z+0.725f,0.025f,0.018f,y+0.22f,1.34f,edge);
    }
}

static void render_construction_damage(const rcam *c,float x,float z,float y,uint32_t shape,
                                       uint32_t health,uint32_t max_health,uint32_t edge) {
    uint32_t scar;int level=1;float top_y;
    if(max_health==0u||health==0u||health>=max_health)return;
    if((uint64_t)health*2u<(uint64_t)max_health)level=2;
    if((uint64_t)health*4u<(uint64_t)max_health)level=3;
    scar=rgba_mix(edge,0.70f);top_y=(float)odg_construction_shape_height_milli_internal(shape)/1000.0f;
    /* Damage is presentation-only. Thin depth-tested fracture paths keep integrity readable
     * without turning damage into fake holes or changing renderer-side collision geometry. */
    if(shape==ODG_CONSTRUCTION_SHAPE_FLOOR||shape==ODG_CONSTRUCTION_SHAPE_ROOF){
        float sy=shape==ODG_CONSTRUCTION_SHAPE_FLOOR?y+0.126f:y+2.050f;
        line_overlay(c,(rv3){x-0.46f,sy,z-0.18f},(rv3){x-0.12f,sy,z+0.02f},scar);
        line_overlay(c,(rv3){x-0.12f,sy,z+0.02f},(rv3){x+0.18f,sy,z-0.08f},scar);
        if(level>=2){
            line_overlay(c,(rv3){x-0.12f,sy,z+0.02f},(rv3){x+0.02f,sy,z+0.36f},scar);
            line_overlay(c,(rv3){x+0.18f,sy,z-0.08f},(rv3){x+0.48f,sy,z+0.16f},scar);
        }
        if(level>=3)line_overlay(c,(rv3){x+0.02f,sy,z+0.36f},(rv3){x-0.30f,sy,z+0.52f},scar);
        return;
    }
    if(shape==ODG_CONSTRUCTION_SHAPE_DOORWAY){
        line_overlay(c,(rv3){x-0.574f,y+1.34f,z-0.574f},(rv3){x-0.574f,y+1.04f,z-0.574f},scar);
        line_overlay(c,(rv3){x-0.574f,y+1.04f,z-0.574f},(rv3){x-0.574f,y+0.78f,z-0.574f},scar);
        if(level>=2){
            line_overlay(c,(rv3){x+0.574f,y+1.20f,z+0.574f},(rv3){x+0.574f,y+0.88f,z+0.574f},scar);
            line_overlay(c,(rv3){x-0.12f,y+top_y-0.06f,z+0.574f},(rv3){x+0.22f,y+top_y-0.06f,z+0.574f},scar);
        }
        if(level>=3)line_overlay(c,(rv3){x+0.574f,y+0.88f,z+0.574f},(rv3){x+0.574f,y+0.62f,z+0.574f},scar);
        return;
    }
    /* WALL cracks are mirrored on all four exposed faces so orbiting the camera cannot
     * make structural damage disappear. */
    line_overlay(c,(rv3){x-0.34f,y+1.42f,z+0.727f},(rv3){x-0.12f,y+1.12f,z+0.727f},scar);
    line_overlay(c,(rv3){x-0.12f,y+1.12f,z+0.727f},(rv3){x-0.27f,y+0.82f,z+0.727f},scar);
    line_overlay(c,(rv3){x+0.34f,y+1.42f,z-0.727f},(rv3){x+0.12f,y+1.12f,z-0.727f},scar);
    line_overlay(c,(rv3){x+0.12f,y+1.12f,z-0.727f},(rv3){x+0.27f,y+0.82f,z-0.727f},scar);
    line_overlay(c,(rv3){x+0.727f,y+1.30f,z-0.34f},(rv3){x+0.727f,y+1.02f,z-0.10f},scar);
    line_overlay(c,(rv3){x-0.727f,y+1.30f,z+0.34f},(rv3){x-0.727f,y+1.02f,z+0.10f},scar);
    if(level>=2){
        line_overlay(c,(rv3){x-0.12f,y+1.12f,z+0.728f},(rv3){x+0.18f,y+0.98f,z+0.728f},scar);
        line_overlay(c,(rv3){x+0.12f,y+1.12f,z-0.728f},(rv3){x-0.18f,y+0.98f,z-0.728f},scar);
        line_overlay(c,(rv3){x+0.728f,y+1.02f,z-0.10f},(rv3){x+0.728f,y+0.76f,z+0.18f},scar);
        line_overlay(c,(rv3){x-0.728f,y+1.02f,z+0.10f},(rv3){x-0.728f,y+0.76f,z-0.18f},scar);
    }
    if(level>=3){
        line_overlay(c,(rv3){x-0.27f,y+0.82f,z+0.729f},(rv3){x-0.04f,y+0.52f,z+0.729f},scar);
        line_overlay(c,(rv3){x+0.27f,y+0.82f,z-0.729f},(rv3){x+0.04f,y+0.52f,z-0.729f},scar);
    }
}

static void render_construction_node_ref(const rcam *c,uint32_t id) {
    const odg_construction_block *b;float x,z,y,integrity;uint32_t face,edge;
    if(id>=g_odg_construction_count)return;
    b=&g_odg_construction_blocks[id];if(!b->active)return;
    x=render_global_fx_local_f(b->global_fx_x,1);z=render_global_fx_local_f(b->global_fx_z,0);
    if(!world_point_maybe_visible(c,x,z,2.2f))return;
    y=terrain_yf(x,z);face=construction_shell_color(b->material_tier);
    integrity=b->max_health!=0u?(float)b->health/(float)b->max_health:1.0f;
    if(integrity<0.0f)integrity=0.0f;
    if(integrity>1.0f)integrity=1.0f;
    face=rgba_mix(face,0.74f+0.26f*integrity);edge=rgba_mix(face,0.70f);
    render_construction_module(c,x,z,y,b->material_tier,b->shape,face,edge,1);
    render_construction_damage(c,x,z,y,b->shape,b->health,b->max_health,edge);
}

static void render_construction_nodes(const rcam *c) {
    render_visible_spatial(c,ODG_SPATIAL_KIND_CONSTRUCTION,render_construction_node_ref);
}

static uint32_t artifact_shell_color(const odg_artifact *a) {
    visual_palette p=palette();
    if(a==NULL) return p.building;
    if(a->material_tier==ODG_MATERIAL_IRON) return rgba_lerp(rgba_mix(p.rock,1.16f),p.building_alt,46u);
    if(a->material_tier==ODG_MATERIAL_STONE) return rgba_mix(p.rock,0.96f);
    return rgba_lerp(rgba_mix(p.trunk,1.02f),p.coast,24u);
}

static void render_artifact_node_ref(const rcam *c,uint32_t id) {
    const odg_artifact *a;
    float x,z,y;
    uint32_t shell,owner,tech;
    visual_palette p=palette();
    if(id>=g_odg.artifact_count)return;
    a=&g_odg_artifacts[id];
    if(!a->active) return;
    x=render_global_fx_local_f(a->global_fx_x,1);z=render_global_fx_local_f(a->global_fx_z,0);
    if(!world_point_maybe_visible(c,x,z,2.2f)) return;
    y=terrain_yf(x,z);
    if(a->item_type==ODG_ITEM_RAFT&&a->local_resident){
        odg_surface_sample water;
        if(odg_environment_surface_local(a->x,a->z,&water)&&(water.flags&ODG_SURFACE_FLAG_WATER)!=0u)
            y+=(float)water.water_depth_milli/1000.0f;
    }
    shell=artifact_shell_color(a);
    owner=a->owner_actor_id<ODG_MAX_ACTORS?actor_base_color(a->owner_actor_id):p.neutral_turret;
    tech=rgba_mix(owner,0.95f+music_visual_beat()*0.18f);
    if(a->item_type==ODG_ITEM_WORKBENCH){
        uint32_t dark=rgba_mix(shell,0.71f),top=rgba_mix(shell,1.10f);
        ground_shadow(c,x,z,0.76f,0.50f,0.60f);
        /* four legs + braces + split top + vise: workshop silhouette, not a table cube */
        box_y(c,x-0.52f,z-0.30f,0.065f,0.065f,y+0.03f,0.42f,dark);box_y(c,x+0.52f,z-0.30f,0.065f,0.065f,y+0.03f,0.42f,dark);
        box_y(c,x-0.52f,z+0.30f,0.065f,0.065f,y+0.03f,0.42f,dark);box_y(c,x+0.52f,z+0.30f,0.065f,0.065f,y+0.03f,0.42f,dark);
        box_y(c,x,z-0.30f,0.52f,0.045f,y+0.18f,0.075f,rgba_mix(shell,0.76f));
        box_y(c,x,z+0.30f,0.52f,0.045f,y+0.18f,0.075f,rgba_mix(shell,0.76f));
        box_y(c,x-0.33f,z,0.33f,0.43f,y+0.42f,0.13f,top);box_y(c,x+0.36f,z,0.31f,0.43f,y+0.42f,0.13f,rgba_mix(top,0.92f));
        /* Flush plank seams and a front steel rail add material scale without changing
         * the collision silhouette or turning the bench into decorative clutter. */
        box_y(c,x-0.06f,z,0.018f,0.41f,y+0.548f,0.014f,rgba_mix(dark,0.92f));
        box_y(c,x+0.08f,z,0.018f,0.41f,y+0.548f,0.014f,rgba_mix(dark,0.86f));
        box_y(c,x,z+0.405f,0.58f,0.022f,y+0.455f,0.055f,rgba_mix(p.building_alt,0.72f));
        box_y(c,x+0.34f,z-0.22f,0.12f,0.10f,y+0.56f,0.12f,rgba_mix(p.building_alt,0.84f));
        box_y(c,x+0.34f,z-0.32f,0.17f,0.035f,y+0.61f,0.055f,tech);
    }else if(a->item_type==ODG_ITEM_SMITHY){
        uint32_t stone=rgba_lerp(shell,p.rock,112u),dark=rgba_mix(p.sky_top,0.62f);
        ground_shadow(c,x,z,0.74f,0.62f,0.70f);
        beveled_box_y(c,x,z,0.64f,0.54f,0.11f,y+0.035f,0.42f,stone);
        /* Recessed fire mouth with an iron lintel and hearth lip; the warm core remains
         * contained inside the stone body instead of reading as an emissive sticker. */
        box_y(c,x,z+0.545f,0.28f,0.026f,y+0.13f,0.20f,dark);
        box_y(c,x,z+0.578f,0.31f,0.026f,y+0.325f,0.045f,rgba_mix(p.building_alt,0.70f));
        box_y(c,x,z+0.586f,0.32f,0.045f,y+0.105f,0.045f,rgba_mix(p.rock,0.74f));
        box_y(c,x,z+0.575f,0.19f,0.018f,y+0.19f,0.075f,rgba_mix(p.ammo,0.72f+music_visual_beat()*0.10f));
        tapered_box_y(c,x,z,0.53f,0.46f,0.43f,0.36f,y+0.44f,0.16f,rgba_mix(stone,0.80f));
        /* chimney stack with cap */
        box_y(c,x+0.34f,z-0.12f,0.10f,0.11f,y+0.55f,0.54f,rgba_mix(stone,0.66f));
        box_y(c,x+0.34f,z-0.12f,0.14f,0.15f,y+1.08f,0.065f,rgba_mix(stone,0.56f));
        /* anvil: narrow waist, broad head, technology ownership line */
        box_y(c,x-0.30f,z-0.05f,0.075f,0.08f,y+0.54f,0.20f,rgba_mix(p.building_alt,0.74f));
        box_y(c,x-0.30f,z-0.05f,0.24f,0.10f,y+0.72f,0.08f,rgba_mix(p.building_alt,1.06f));
        box_y(c,x-0.30f,z+0.055f,0.11f,0.025f,y+0.80f,0.035f,tech);
    }else if(a->item_type==ODG_ITEM_CHEST){
        uint32_t band=rgba_lerp(shell,p.building_alt,126u),lid=rgba_mix(shell,0.88f);
        ground_shadow(c,x,z,0.58f,0.44f,0.56f);
        beveled_box_y(c,x,z,0.53f,0.38f,0.075f,y+0.045f,0.36f,shell);
        tapered_box_y(c,x,z,0.57f,0.42f,0.49f,0.34f,y+0.40f,0.16f,lid);
        /* metal straps, recessed latch, hinges, handle and low feet */
        box_y(c,x-0.32f,z,0.035f,0.39f,y+0.07f,0.46f,band);box_y(c,x+0.32f,z,0.035f,0.39f,y+0.07f,0.46f,band);
        box_y(c,x,z+0.405f,0.092f,0.026f,y+0.18f,0.18f,rgba_mix(band,0.76f));
        box_y(c,x,z+0.432f,0.045f,0.018f,y+0.235f,0.080f,tech);
        box_y(c,x-0.24f,z-0.405f,0.085f,0.026f,y+0.49f,0.060f,rgba_mix(band,0.72f));
        box_y(c,x+0.24f,z-0.405f,0.085f,0.026f,y+0.49f,0.060f,rgba_mix(band,0.72f));
        box_y(c,x-0.13f,z,0.025f,0.025f,y+0.55f,0.13f,band);box_y(c,x+0.13f,z,0.025f,0.025f,y+0.55f,0.13f,band);
        box_y(c,x,z,0.13f,0.025f,y+0.66f,0.035f,band);
        box_y(c,x-0.38f,z,0.075f,0.060f,y+0.025f,0.055f,rgba_mix(shell,0.58f));
        box_y(c,x+0.38f,z,0.075f,0.060f,y+0.025f,0.055f,rgba_mix(shell,0.58f));
    }else if(a->item_type==ODG_ITEM_RAFT){
        uint32_t wood=rgba_mix(p.trunk,0.92f),dark=rgba_mix(p.trunk,0.62f),rope=rgba_mix(p.coast,0.86f);
        /* Low, broad one-seat raft. The waterline comes from the same authoritative
         * surface sample used by vehicle physics, so the craft does not float above a
         * visual-only plane or sink to the lake floor. */
        ground_shadow(c,x,z,0.92f,0.68f,0.22f);
        box_y(c,x-0.48f,z,0.20f,0.68f,y-0.08f,0.16f,wood);
        box_y(c,x,z,0.20f,0.68f,y-0.08f,0.16f,rgba_mix(wood,0.94f));
        box_y(c,x+0.48f,z,0.20f,0.68f,y-0.08f,0.16f,wood);
        box_y(c,x,z-0.43f,0.78f,0.035f,y+0.06f,0.05f,dark);
        box_y(c,x,z+0.43f,0.78f,0.035f,y+0.06f,0.05f,dark);
        box_y(c,x-0.28f,z,0.025f,0.67f,y+0.07f,0.035f,rope);
        box_y(c,x+0.28f,z,0.025f,0.67f,y+0.07f,0.035f,rope);
        if(a->aux_u32==0u){
            /* Paddle left on an empty craft; occupied rafts keep the silhouette clean. */
            box_y(c,x+0.70f,z,0.035f,0.04f,y+0.10f,0.82f,dark);
        }
    }else if(a->item_type==ODG_ITEM_TORCH){
        float flicker=0.93f+0.10f*(float)((visual_hash2((uint32_t)a->instance_id,(uint32_t)g_odg.tick)&31u))/31.0f;
        uint32_t flame=rgba_mix(0xe39a45ffu,flicker);
        ground_shadow(c,x,z,0.22f,0.18f,0.32f);
        box_y(c,x,z,0.035f,0.035f,y+0.02f,0.72f,rgba_mix(p.trunk,0.68f));
        octa(c,x,z,0.075f,y+0.70f,0.18f,flame);
        octa(c,x,z,0.035f,y+0.76f,0.10f,0xf2c768ffu);
    }else if(a->item_type==ODG_ITEM_NIGHT_SHARD){
        float pulse=0.88f+0.13f*(float)((visual_hash2((uint32_t)a->instance_id,(uint32_t)(g_odg.tick/3u))&31u))/31.0f;
        uint32_t crystal=rgba_mix(0x7dd6d9ffu,pulse),core=rgba_mix(0xc5fbffffu,pulse);
        ground_shadow(c,x,z,0.28f,0.24f,0.34f);
        /* A ward is deliberately unlike a torch: a cold crystalline tripod grown from
         * the Stalker's shard, so its stronger radius reads as supernatural rather
         * than as a better wooden flame. */
        tapered_box_y(c,x,z,0.085f,0.085f,0.035f,0.035f,y+0.03f,0.68f,crystal);
        tapered_box_y(c,x-0.12f,z+0.06f,0.055f,0.055f,0.025f,0.025f,y+0.04f,0.43f,rgba_mix(crystal,0.82f));
        tapered_box_y(c,x+0.11f,z+0.07f,0.050f,0.050f,0.020f,0.020f,y+0.04f,0.37f,rgba_mix(crystal,0.74f));
        octa(c,x,z,0.052f,y+0.70f,0.13f,core);
    }else{
        /* Every deployable artifact remains physically visible even before receiving a
         * bespoke morphology. Gameplay never creates an invisible collider/container. */
        ground_shadow(c,x,z,0.44f,0.36f,0.42f);
        beveled_box_y(c,x,z,0.38f,0.32f,0.07f,y+0.03f,0.42f,shell);
        box_y(c,x,z+0.325f,0.11f,0.025f,y+0.16f,0.12f,tech);
    }
}

static void render_artifact_nodes(const rcam *c) {
    render_visible_spatial(c,ODG_SPATIAL_KIND_ARTIFACT,render_artifact_node_ref);
}

static void render_construction_placement_ghost(const rcam *c,const odg_actor *p,const odg_item_stack *selected) {
    const odg_item_definition *definition;int32_t ax=0,az=0;int valid;float x,z,y;uint32_t signal,face,edge;
    if(g_render_remote_rebased!=0u||p==NULL||selected==NULL||selected->quantity==0u)return;
    definition=odg_item_definition_internal(selected->type_id);
    if(definition==NULL||(definition->capability_bits&ODG_ITEM_CAP_CONSTRUCT)==0u)return;
    valid=odg_construction_placement_candidate_internal(p,&ax,&az,NULL,NULL);
    x=valid?odg_fx_to_float(ax):odg_fx_to_float(p->x)+(float)p->face_x_q15/(float)ODG_Q15_ONE*1.80f;
    z=valid?odg_fx_to_float(az):odg_fx_to_float(p->z)+(float)p->face_z_q15/(float)ODG_Q15_ONE*1.80f;
    y=terrain_yf(x,z)+0.03f;signal=valid?0x72d6b8ffu:0xe36d67ffu;
    face=rgba_lerp(construction_shell_color(selected->material_tier),signal,valid?38u:62u);
    edge=rgba_mix(signal,1.08f);
    {
        uint32_t shape=odg_construction_selected_shape_internal(p->id);
        float outline_y=y+(float)odg_construction_shape_height_milli_internal(shape)/1000.0f+0.025f;
        render_construction_module(c,x,z,y,selected->material_tier,shape,rgba_mix(face,0.78f),edge,1);
        line_overlay(c,(rv3){x-0.72f,outline_y,z-0.72f},(rv3){x+0.72f,outline_y,z-0.72f},edge);
        line_overlay(c,(rv3){x+0.72f,outline_y,z-0.72f},(rv3){x+0.72f,outline_y,z+0.72f},edge);
        line_overlay(c,(rv3){x+0.72f,outline_y,z+0.72f},(rv3){x-0.72f,outline_y,z+0.72f},edge);
        line_overlay(c,(rv3){x-0.72f,outline_y,z+0.72f},(rv3){x-0.72f,outline_y,z-0.72f},edge);
    }
}

static void render_artifact_placement_ghost(const rcam *c,const odg_actor *p,const odg_item_stack *selected) {
    int32_t ax=0,az=0;
    int valid;
    float x,z,y;
    uint32_t col;
    if(g_render_remote_rebased!=0u || p==NULL || selected==NULL || selected->quantity==0u) return;
    if(!odg_artifact_item_deployable_internal(selected->type_id)) return;
    valid=odg_artifact_placement_candidate_for_item_internal(p,selected->type_id,&ax,&az);
    x=valid?odg_fx_to_float(ax):odg_fx_to_float(p->x)+(float)p->face_x_q15/(float)ODG_Q15_ONE*1.80f;
    z=valid?odg_fx_to_float(az):odg_fx_to_float(p->z)+(float)p->face_z_q15/(float)ODG_Q15_ONE*1.80f;
    {
        visual_palette gp=palette();
        uint32_t body,edge,dark;
        y=terrain_yf(x,z)+0.03f;
        if(selected->type_id==ODG_ITEM_RAFT){
            odg_surface_sample water;int32_t sx=valid?ax:p->x,sz=valid?az:p->z;
            if(odg_environment_surface_local(sx,sz,&water)&&(water.flags&ODG_SURFACE_FLAG_WATER)!=0u)
                y+=(float)water.water_depth_milli/1000.0f;
        }
        col=valid?0x72d6b8ffu:0xe36d67ffu;
        /* The preview keeps the physical identity of the selected artifact.  Validation
         * is an edge/signal colour, not a giant fluorescent replacement material. */
        body=rgba_lerp(gp.building_alt,col,valid?42u:58u);
        body=rgba_mix(body,0.72f);edge=rgba_mix(col,1.08f);dark=rgba_mix(body,0.62f);
        ground_shadow(c,x,z,0.70f,0.56f,0.30f);
        if(selected->type_id==ODG_ITEM_WORKBENCH){
            box_y(c,x-0.50f,z-0.29f,0.055f,0.055f,y,0.40f,dark);
            box_y(c,x+0.50f,z-0.29f,0.055f,0.055f,y,0.40f,dark);
            box_y(c,x-0.50f,z+0.29f,0.055f,0.055f,y,0.40f,dark);
            box_y(c,x+0.50f,z+0.29f,0.055f,0.055f,y,0.40f,dark);
            box_y(c,x-0.31f,z,0.31f,0.41f,y+0.40f,0.12f,body);
            box_y(c,x+0.34f,z,0.30f,0.41f,y+0.40f,0.12f,rgba_mix(body,0.90f));
            box_y(c,x+0.34f,z-0.24f,0.11f,0.09f,y+0.53f,0.12f,rgba_mix(gp.building_alt,0.78f));
            line_overlay(c,(rv3){x-0.65f,y+0.54f,z-0.43f},(rv3){x+0.65f,y+0.54f,z-0.43f},edge);
            line_overlay(c,(rv3){x+0.65f,y+0.54f,z-0.43f},(rv3){x+0.65f,y+0.54f,z+0.43f},edge);
            line_overlay(c,(rv3){x+0.65f,y+0.54f,z+0.43f},(rv3){x-0.65f,y+0.54f,z+0.43f},edge);
            line_overlay(c,(rv3){x-0.65f,y+0.54f,z+0.43f},(rv3){x-0.65f,y+0.54f,z-0.43f},edge);
        }else if(selected->type_id==ODG_ITEM_SMITHY){
            box_y(c,x,z,0.62f,0.52f,y,0.40f,body);
            tapered_box_y(c,x,z,0.51f,0.44f,0.41f,0.34f,y+0.40f,0.14f,rgba_mix(body,0.78f));
            box_y(c,x+0.33f,z-0.12f,0.095f,0.105f,y+0.48f,0.52f,dark);
            box_y(c,x+0.33f,z-0.12f,0.135f,0.145f,y+0.99f,0.06f,rgba_mix(dark,0.88f));
            box_y(c,x-0.29f,z-0.05f,0.07f,0.075f,y+0.50f,0.18f,rgba_mix(gp.building_alt,0.76f));
            box_y(c,x-0.29f,z-0.05f,0.22f,0.09f,y+0.67f,0.07f,rgba_mix(gp.building_alt,0.96f));
            line_overlay(c,(rv3){x-0.62f,y+0.58f,z-0.52f},(rv3){x+0.62f,y+0.58f,z-0.52f},edge);
            line_overlay(c,(rv3){x+0.62f,y+0.58f,z-0.52f},(rv3){x+0.62f,y+0.58f,z+0.52f},edge);
            line_overlay(c,(rv3){x+0.62f,y+0.58f,z+0.52f},(rv3){x-0.62f,y+0.58f,z+0.52f},edge);
            line_overlay(c,(rv3){x-0.62f,y+0.58f,z+0.52f},(rv3){x-0.62f,y+0.58f,z-0.52f},edge);
            line_overlay(c,(rv3){x+0.33f,y+0.60f,z-0.12f},(rv3){x+0.33f,y+1.08f,z-0.12f},edge);
        }else if(selected->type_id==ODG_ITEM_RAFT){
            uint32_t wood=rgba_lerp(rgba_mix(gp.trunk,0.88f),col,valid?36u:64u);
            box_y(c,x-0.48f,z,0.20f,0.68f,y-0.10f,0.16f,wood);
            box_y(c,x,z,0.20f,0.68f,y-0.10f,0.16f,rgba_mix(wood,0.94f));
            box_y(c,x+0.48f,z,0.20f,0.68f,y-0.10f,0.16f,wood);
            line_overlay(c,(rv3){x-0.82f,y+0.08f,z-0.68f},(rv3){x+0.82f,y+0.08f,z-0.68f},edge);
            line_overlay(c,(rv3){x+0.82f,y+0.08f,z-0.68f},(rv3){x+0.82f,y+0.08f,z+0.68f},edge);
            line_overlay(c,(rv3){x+0.82f,y+0.08f,z+0.68f},(rv3){x-0.82f,y+0.08f,z+0.68f},edge);
            line_overlay(c,(rv3){x-0.82f,y+0.08f,z+0.68f},(rv3){x-0.82f,y+0.08f,z-0.68f},edge);
        }else if(selected->type_id==ODG_ITEM_TORCH){
            box_y(c,x,z,0.035f,0.035f,y,0.70f,dark);
            octa(c,x,z,0.072f,y+0.68f,0.17f,body);
            line_overlay(c,(rv3){x-0.18f,y+0.02f,z},(rv3){x+0.18f,y+0.02f,z},edge);
            line_overlay(c,(rv3){x,y+0.02f,z-0.18f},(rv3){x,y+0.02f,z+0.18f},edge);
        }else if(selected->type_id==ODG_ITEM_NIGHT_SHARD){
            tapered_box_y(c,x,z,0.085f,0.085f,0.035f,0.035f,y+0.02f,0.67f,body);
            tapered_box_y(c,x-0.12f,z+0.06f,0.050f,0.050f,0.020f,0.020f,y+0.03f,0.42f,rgba_mix(body,0.80f));
            tapered_box_y(c,x+0.11f,z+0.07f,0.048f,0.048f,0.020f,0.020f,y+0.03f,0.36f,rgba_mix(body,0.72f));
            octa(c,x,z,0.048f,y+0.69f,0.12f,edge);
        }else{
            uint32_t band=rgba_lerp(body,gp.building_alt,110u);
            box_y(c,x,z,0.51f,0.37f,y,0.35f,body);
            tapered_box_y(c,x,z,0.54f,0.40f,0.47f,0.33f,y+0.35f,0.14f,rgba_mix(body,0.80f));
            box_y(c,x-0.31f,z,0.03f,0.38f,y+0.03f,0.43f,band);
            box_y(c,x+0.31f,z,0.03f,0.38f,y+0.03f,0.43f,band);
            line_overlay(c,(rv3){x-0.54f,y+0.51f,z-0.39f},(rv3){x+0.54f,y+0.51f,z-0.39f},edge);
            line_overlay(c,(rv3){x+0.54f,y+0.51f,z-0.39f},(rv3){x+0.54f,y+0.51f,z+0.39f},edge);
            line_overlay(c,(rv3){x+0.54f,y+0.51f,z+0.39f},(rv3){x-0.54f,y+0.51f,z+0.39f},edge);
            line_overlay(c,(rv3){x-0.54f,y+0.51f,z+0.39f},(rv3){x-0.54f,y+0.51f,z-0.39f},edge);
        }
        /* Thin footprint reticle stays readable on slopes without becoming a grid. */
        line_overlay(c,(rv3){x-0.66f,y+0.015f,z-0.52f},(rv3){x+0.66f,y+0.015f,z-0.52f},edge);
        line_overlay(c,(rv3){x+0.66f,y+0.015f,z-0.52f},(rv3){x+0.66f,y+0.015f,z+0.52f},edge);
        line_overlay(c,(rv3){x+0.66f,y+0.015f,z+0.52f},(rv3){x-0.66f,y+0.015f,z+0.52f},edge);
        line_overlay(c,(rv3){x-0.66f,y+0.015f,z+0.52f},(rv3){x-0.66f,y+0.015f,z-0.52f},edge);
    }
}

static void render_world_obstacle(const rcam *c,const odg_obstacle *o) {
    float x=odg_fx_to_float(o->x),z=odg_fx_to_float(o->z);
    float hx=odg_fx_to_float(o->hx),hz=odg_fx_to_float(o->hz),h=odg_fx_to_float(o->height_fx);
    float y=terrain_yf(x,z);
    visual_palette p=palette();
    if(o->palette==2u){
        float s=(hx<hz?hx:hz)*0.92f;
        if (s < 1.25f) s = 1.25f;
        if (s > 2.10f) s = 2.10f;
        render_tree(c,x-hx*0.38f,z-hz*0.22f,s,TREE_VISUAL_MIXED);
        render_tree(c,x+hx*0.30f,z+hz*0.24f,s*0.88f,TREE_VISUAL_MIXED);
        if(hx>1.8f||hz>1.8f)render_tree(c,x+hx*0.12f,z-hz*0.48f,s*0.72f,TREE_VISUAL_MIXED);
    }else if(o->palette==1u){
        uint32_t col=p.rock;
        uint32_t seed=visual_hash2((uint32_t)(x*19.0f+8192.0f),(uint32_t)(z*23.0f+16384.0f));
        ground_shadow(c,x,z,hx*0.90f,hz*0.82f,0.68f);
        faceted_rock(c,x,z,hx*0.82f,hz*0.78f,y+0.01f,h*0.72f,col,seed);
        faceted_rock(c,x-hx*0.42f,z+hz*0.20f,hx*0.42f,hz*0.38f,y,h*0.45f,rgba_mix(col,0.82f),seed^0x47u);
        faceted_rock(c,x+hx*0.38f,z-hz*0.29f,hx*0.36f,hz*0.32f,y,h*0.52f,rgba_mix(col,1.08f),seed^0x91u);
    }else{
        render_building(c,x,z,hx,hz,h,p.building);
    }
}

static uint32_t turret_color(const odg_turret *t) {
    if (!t || t->owner==ODG_TURRET_NEUTRAL) return palette().neutral_turret;
    return actor_base_color(ODG_ID_FROM_OWNER(t->owner));
}

static uint32_t turret_shell_color(const odg_turret *t,visual_palette p){
    if(t==NULL)return p.building_alt;
    if(t->material_tier==ODG_MATERIAL_WOOD)return rgba_lerp(p.trunk,p.building,54u);
    if(t->material_tier==ODG_MATERIAL_STONE)return rgba_lerp(p.rock,p.building_alt,56u);
    if(t->material_tier==ODG_MATERIAL_IRON)return rgba_lerp(p.building_alt,0xaeb8bdffu,74u);
    return p.building_alt;
}

static uint32_t turret_trim_color(const odg_turret *t,visual_palette p){
    uint32_t shell=turret_shell_color(t,p);
    if(t!=NULL&&t->material_tier==ODG_MATERIAL_WOOD)return rgba_lerp(shell,0xa47a52ffu,78u);
    if(t!=NULL&&t->material_tier==ODG_MATERIAL_STONE)return rgba_lerp(shell,0xd0d4d2ffu,42u);
    if(t!=NULL&&t->material_tier==ODG_MATERIAL_IRON)return rgba_lerp(shell,0xe5eceeffu,70u);
    return rgba_mix(shell,1.08f);
}

static void turret_head_direction(const odg_turret *t,int32_t *out_x,int32_t *out_z) {
    static const int32_t dirs[8][2]={{0,32767},{23170,23170},{32767,0},{23170,-23170},{0,-32767},{-23170,-23170},{-32767,0},{-23170,23170}};
    if(!t||!out_x||!out_z)return;
    if(t->carried_by<ODG_MAX_ACTORS){*out_x=g_odg.actors[t->carried_by].face_x_q15;*out_z=g_odg.actors[t->carried_by].face_z_q15;}
    else if(t->owner!=ODG_TURRET_NEUTRAL && (t->head_x_q15!=0||t->head_z_q15!=0)){*out_x=t->head_x_q15;*out_z=t->head_z_q15;}
    else{uint32_t d=(uint32_t)((g_odg.tick/96u+(uint64_t)t->id*3u)%UINT64_C(8));*out_x=dirs[d][0];*out_z=dirs[d][1];}
}

static void screen_blend_pixel(int32_t x,int32_t y,uint32_t color){
    uint32_t a=color&255u,inv;
    uint8_t *dst;
    if(x<0||y<0||x>=(int32_t)g_odg.width||y>=(int32_t)g_odg.height||a==0u)return;
    dst=&g_odg_framebuffer[((uint32_t)y*g_odg.width+(uint32_t)x)*4u];
    if(a>=255u){dst[0]=(uint8_t)(color>>24u);dst[1]=(uint8_t)(color>>16u);dst[2]=(uint8_t)(color>>8u);dst[3]=255u;return;}
    inv=255u-a;
    dst[0]=(uint8_t)((((color>>24u)&255u)*a+(uint32_t)dst[0]*inv)/255u);
    dst[1]=(uint8_t)((((color>>16u)&255u)*a+(uint32_t)dst[1]*inv)/255u);
    dst[2]=(uint8_t)((((color>>8u)&255u)*a+(uint32_t)dst[2]*inv)/255u);
    dst[3]=255u;
}

static void screen_fill_rect(int32_t x0,int32_t y0,int32_t w,int32_t h,uint32_t color){
    int32_t y;
    if(w<=0||h<=0)return;
    for(y=0;y<h;++y){int32_t x;for(x=0;x<w;++x)screen_blend_pixel(x0+x,y0+y,color);}
}

static uint32_t screen_text_width(const char *text,uint32_t scale){
    uint32_t n=0u;if(text==NULL||scale==0u)return 0u;while(text[n]!='\0')++n;
    return n==0u?0u:n*(6u*scale)-scale;
}

static void screen_draw_glyph(int32_t x,int32_t y,char ch,uint32_t scale,uint32_t color){
    uint8_t rows[7];uint32_t r;
    if(scale==0u||!odg_glyph5x7_internal(ch,rows))return;
    for(r=0u;r<7u;++r){uint32_t bit;for(bit=0u;bit<5u;++bit){
        if((rows[r]&(uint8_t)(1u<<(4u-bit)))!=0u)
            screen_fill_rect(x+(int32_t)(bit*scale),y+(int32_t)(r*scale),(int32_t)scale,(int32_t)scale,color);
    }}
}

static void screen_draw_text(int32_t x,int32_t y,const char *text,uint32_t scale,uint32_t color){
    uint32_t i=0u;if(text==NULL)return;
    while(text[i]!='\0'){screen_draw_glyph(x+(int32_t)(i*6u*scale),y,text[i],scale,color);++i;}
}

static void queue_world_label(const rcam *c,float x,float y,float z,const char *text,uint32_t scale,
                              uint32_t fg,uint32_t bg,uint32_t border){
    cv3 cp;rpv pp;odg_render_label *label;uint32_t i=0u;
    if(c==NULL||text==NULL||text[0]=='\0'||scale==0u||g_render_label_count>=ODG_RENDER_LABEL_MAX)return;
    cp=world_to_camera(c,(rv3){x,y,z});pp=project_camera(c,cp);
    if(!pp.valid||pp.sx<-80.0f||pp.sx>(float)c->w+80.0f||pp.sy<-50.0f||pp.sy>(float)c->h+50.0f)return;
    label=&g_render_labels[g_render_label_count++];
    label->center_x=(int32_t)pp.sx;label->center_y=(int32_t)pp.sy;
    label->fg=fg;label->bg=bg;label->border=border;label->scale=(uint8_t)scale;
    while(i+1u<ODG_RENDER_LABEL_TEXT_MAX&&text[i]!='\0'){label->text[i]=text[i];++i;}label->text[i]='\0';
}

static void render_screen_labels(void){
    int32_t rx[ODG_RENDER_LABEL_MAX],ry[ODG_RENDER_LABEL_MAX],rw[ODG_RENDER_LABEL_MAX],rh[ODG_RENDER_LABEL_MAX];
    uint32_t i,resolved=0u;
    for(i=0u;i<g_render_label_count;++i){
        const odg_render_label *l=&g_render_labels[i];
        uint32_t scale=l->scale,w=screen_text_width(l->text,scale),h=7u*scale;
        int32_t pad=(int32_t)(3u+scale),x=l->center_x-(int32_t)w/2-pad,y=l->center_y-(int32_t)h/2-pad;
        int32_t bw=(int32_t)w+pad*2,bh=(int32_t)h+pad*2;
        uint32_t attempt=0u;
        if(x<2)x=2;
        if(x+bw>(int32_t)g_odg.width-2)x=(int32_t)g_odg.width-bw-2;
        /* Resolve caption collisions deterministically upward.  Dense pickup clusters may
         * contain several cards in less than a metre; text must remain readable rather
         * than turning into the overlapping debug-strip look of the old renderer. */
        while(attempt<12u){
            uint32_t j;int overlap=0;
            for(j=0u;j<resolved;++j){
                if(x<rx[j]+rw[j]+3&&x+bw+3>rx[j]&&y<ry[j]+rh[j]+3&&y+bh+3>ry[j]){overlap=1;break;}
            }
            if(!overlap)break;
            y-=bh+4;++attempt;
        }
        if(y<2)y=2;
        if(y+bh>(int32_t)g_odg.height-2)y=(int32_t)g_odg.height-bh-2;
        rx[resolved]=x;ry[resolved]=y;rw[resolved]=bw;rh[resolved]=bh;++resolved;
        {
            uint32_t accent=(l->border&UINT32_C(0xffffff00))|UINT32_C(0xd8);
            uint32_t subtle=(l->border&UINT32_C(0xffffff00))|UINT32_C(0x4a);
            /* Compact glass telemetry: one strong accent edge and a quiet baseline are
             * easier to read over terrain than the old four-sided debug-looking box. */
            screen_fill_rect(x,y,bw,bh,l->bg);
            screen_fill_rect(x,y,2,bh,accent);
            screen_fill_rect(x+2,y,bw-2,1,subtle);
            screen_fill_rect(x+2,y+bh-1,bw-2,1,subtle);
        }
        screen_draw_text(x+pad+1,y+pad+1,l->text,scale,0x00000078u);
        screen_draw_text(x+pad,y+pad,l->text,scale,l->fg);
    }
}

static uint32_t u32_digits(uint32_t v,char out[4]){uint32_t n=0u;if(v>=100u){out[n++]=(char)('0'+(v/100u)%10u);}if(v>=10u){out[n++]=(char)('0'+(v/10u)%10u);}out[n++]=(char)('0'+v%10u);out[n]='\0';return n;}
static void turret_ammo_label(const rcam *c,const odg_turret *t,float x,float z,uint32_t col){
    const odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];char left[4],right[4],text[12];uint32_t li,ri,i=0u,j;
    if(t->owner!=ODG_OWNER_FROM_ID(ODG_PLAYER_ID)||t->carried_by!=ODG_TURRET_NONE||p->hp==0u)return;
    if(g_render_remote_rebased==0u){
        int64_t pgx,pgz,dx,dz,range=((int64_t)11*ODG_FX_ONE)/4;
        odg_local_fx_to_global_fx_internal(p->x,p->z,&pgx,&pgz);dx=t->global_fx_x-pgx;dz=t->global_fx_z-pgz;
        if(dx>range||dx<-range||dz>range||dz<-range||dx*dx+dz*dz>range*range)return;
    }
    li=u32_digits(t->ammo,left);ri=u32_digits(t->max_ammo,right);
    for(j=0u;j<li;++j){text[i++]=left[j];}
    text[i++]=' ';text[i++]='/';text[i++]=' ';
    for(j=0u;j<ri;++j){text[i++]=right[j];}
    text[i]='\0';
    queue_world_label(c,x,terrain_yf(x,z)+1.62f,z,text,(c->w>=1080u&&c->h>=1080u?2u:1u),0xe7eef0ffu,0x081117b8u,col);
}


static void render_turret_ref(const rcam *c,uint32_t i) {
    visual_palette p=palette();
    if(i>=g_odg.turret_count)return;

        const odg_turret *t=&g_odg_turrets[i];
        float x,z,fx,fz;
        int32_t hdx=0,hdz=ODG_Q15_ONE;
        uint32_t col;
        float ammo_ratio;
        if (!t->active) return;
        x=render_global_fx_local_f(t->global_fx_x,1);z=render_global_fx_local_f(t->global_fx_z,0);
        if(!world_point_maybe_visible(c,x,z,3.2f)) return;
        col=turret_color(t);
        ammo_ratio=t->max_ammo!=0u?(float)t->ammo/(float)t->max_ammo:0.0f;
        turret_head_direction(t,&hdx,&hdz);
        fx=(float)hdx/(float)ODG_Q15_ONE;fz=(float)hdz/(float)ODG_Q15_ONE;
        {
            float ground;
            float y0;
            uint32_t core_phase=(uint32_t)((g_odg.tick+(uint64_t)i*11u)%UINT64_C(72));
            float core_wave=(core_phase<36u?(float)core_phase:(float)(72u-core_phase))/36.0f;
            if (t->carried_by<ODG_MAX_ACTORS) {
                const odg_actor *carrier=&g_odg.actors[t->carried_by];
                float ax=odg_fx_to_float(carrier->x),az=odg_fx_to_float(carrier->z);
                float cfx=(float)carrier->face_x_q15/(float)ODG_Q15_ONE;
                float cfz=(float)carrier->face_z_q15/(float)ODG_Q15_ONE;
                float rx=cfz,rz=-cfx;
                uint32_t shell=turret_shell_color(t,p),trim=turret_trim_color(t,p),signal=rgba_mix(col,0.94f);
                x=ax+rx*0.43f-cfx*0.10f;z=az+rz*0.43f-cfz*0.10f;
                ground=terrain_yf(ax,az);y0=ground+0.70f;
                oriented_box_y(c,x,z,cfx,cfz,0.0f,0.0f,0.25f,0.20f,y0,0.17f,rgba_mix(shell,0.78f));
                oriented_box_y(c,x,z,cfx,cfz,0.0f,0.0f,0.08f,0.08f,y0+0.11f,0.42f,trim);
                oriented_box_y(c,x,z,cfx,cfz,0.0f,0.11f,0.25f,0.12f,y0+0.42f,0.15f,shell);
                oriented_box_y(c,x,z,cfx,cfz,0.0f,0.30f,0.055f,0.22f,y0+0.46f,0.07f,rgba_mix(trim,0.82f));
                box_y(c,x+rx*0.18f,z+rz*0.18f,0.035f,0.035f,y0+0.51f,0.10f,signal);
            } else {
                uint32_t shell=turret_shell_color(t,p),trim=turret_trim_color(t,p),signal=rgba_mix(col,0.96f);
                uint32_t dark=rgba_mix(shell,0.54f),metal=rgba_lerp(trim,p.building_alt,94u);
                ground=terrain_yf(x,z);y0=ground;
                ground_shadow(c,x,z,0.68f,0.58f,0.82f);
                /* Four deployed feet and braced foundation: the silhouette reads as a
                 * planted machine rather than a T made from toy blocks. */
                oriented_box_y(c,x,z,fx,fz,-0.34f,-0.18f,0.075f,0.24f,y0+0.035f,0.11f,dark);
                oriented_box_y(c,x,z,fx,fz, 0.34f,-0.18f,0.075f,0.24f,y0+0.035f,0.11f,dark);
                oriented_box_y(c,x,z,fx,fz,-0.34f, 0.18f,0.075f,0.24f,y0+0.035f,0.11f,rgba_mix(dark,1.05f));
                oriented_box_y(c,x,z,fx,fz, 0.34f, 0.18f,0.075f,0.24f,y0+0.035f,0.11f,rgba_mix(dark,1.05f));
                prism8_y(c,x,z,0.48f,y0+0.055f,0.15f,rgba_mix(dark,0.92f));
                prism8_y(c,x,z,0.36f,y0+0.15f,0.17f,rgba_mix(shell,0.86f));
                /* Bearing + technological core. Nation colour is a contained signal,
                 * while material tier continues to own the physical shell. */
                prism8_y(c,x,z,0.135f,y0+0.28f,0.43f,metal);
                prism8_y(c,x,z,0.175f,y0+0.49f,0.26f,rgba_mix(signal,0.78f+core_wave*0.20f));
                prism8_y(c,x,z,0.285f,y0+0.74f,0.105f,rgba_mix(trim,0.94f));
                /* Faceted gimbal housing: compact centre mass, separate servos and rear
                 * counterweight keep the head mechanical without reading as a giant T. */
                prism8_y(c,x,z,0.295f,y0+0.82f,0.245f,rgba_mix(shell,0.94f));
                oriented_box_y(c,x,z,fx,fz,0.0f,0.17f,0.225f,0.13f,y0+0.85f,0.17f,rgba_mix(shell,1.03f));
                oriented_box_y(c,x,z,fx,fz,-0.29f,0.015f,0.072f,0.13f,y0+0.84f,0.18f,metal);
                oriented_box_y(c,x,z,fx,fz, 0.29f,0.015f,0.072f,0.13f,y0+0.84f,0.18f,metal);
                oriented_box_y(c,x,z,fx,fz,0.0f,-0.21f,0.18f,0.14f,y0+0.82f,0.16f,rgba_mix(dark,0.88f));
                box_y(c,x-fz*0.29f,z+fx*0.29f,0.026f,0.026f,y0+0.90f,0.075f,signal);
                box_y(c,x+fz*0.29f,z-fx*0.29f,0.026f,0.026f,y0+0.90f,0.075f,signal);
                /* Three-stage cannon: armoured breech, narrow barrel, dark muzzle brake. */
                oriented_box_y(c,x,z,fx,fz,0.0f,0.34f,0.105f,0.29f,y0+0.88f,0.115f,rgba_mix(metal,0.80f));
                oriented_box_y(c,x,z,fx,fz,0.0f,0.65f,0.052f,0.31f,y0+0.895f,0.066f,rgba_mix(trim,0.78f));
                oriented_box_y(c,x,z,fx,fz,0.0f,0.96f,0.079f,0.075f,y0+0.878f,0.096f,rgba_mix(dark,0.82f));
                oriented_box_y(c,x,z,fx,fz,0.0f,1.06f,0.052f,0.040f,y0+0.89f,0.058f,rgba_mix(metal,0.66f));
                /* Lock optic is offset from the cannon axis and emits only a restrained
                 * nation-colour signal; the shell keeps its material identity. */
                oriented_box_y(c,x,z,fx,fz,0.16f,0.04f,0.060f,0.078f,y0+1.035f,0.095f,rgba_mix(p.glass,0.88f));
                box_y(c,x+fz*0.16f,z-fx*0.16f,0.021f,0.021f,y0+1.065f,0.038f,signal);
                /* Tier-specific protection changes the physical carcass, not gameplay. */
                if(t->material_tier==ODG_MATERIAL_WOOD){
                    uint32_t plank=rgba_mix(shell,1.10f),grain=rgba_mix(trim,0.66f);
                    oriented_box_y(c,x,z,fx,fz,-0.25f,-0.02f,0.045f,0.27f,y0+0.40f,0.42f,rgba_mix(trim,0.86f));
                    oriented_box_y(c,x,z,fx,fz, 0.25f,-0.02f,0.045f,0.27f,y0+0.40f,0.42f,rgba_mix(trim,0.86f));
                    /* Timber tier is built, not painted: broad plank guards, dark joins and
                     * a split base ring make the wooden silhouette obvious from any orbit. */
                    oriented_box_y(c,x,z,fx,fz,-0.22f,-0.19f,0.070f,0.25f,y0+0.33f,0.115f,plank);
                    oriented_box_y(c,x,z,fx,fz, 0.22f,-0.19f,0.070f,0.25f,y0+0.33f,0.115f,plank);
                    oriented_box_y(c,x,z,fx,fz, 0.00f,-0.26f,0.28f,0.040f,y0+0.50f,0.075f,grain);
                    oriented_box_y(c,x,z,fx,fz, 0.00f, 0.27f,0.31f,0.045f,y0+0.24f,0.085f,rgba_mix(plank,0.84f));
                }else if(t->material_tier==ODG_MATERIAL_STONE){
                    uint32_t mortar=rgba_mix(dark,0.82f),cap=rgba_mix(shell,1.12f);
                    oriented_box_y(c,x,z,fx,fz,-0.27f,0.0f,0.085f,0.24f,y0+0.44f,0.32f,rgba_mix(shell,1.05f));
                    oriented_box_y(c,x,z,fx,fz, 0.27f,0.0f,0.085f,0.24f,y0+0.44f,0.32f,rgba_mix(shell,1.05f));
                    /* Stone tier uses squat buttresses and visible course breaks. */
                    oriented_box_y(c,x,z,fx,fz,-0.31f,-0.20f,0.105f,0.17f,y0+0.18f,0.24f,rgba_mix(shell,0.90f));
                    oriented_box_y(c,x,z,fx,fz, 0.31f,-0.20f,0.105f,0.17f,y0+0.18f,0.24f,rgba_mix(shell,0.90f));
                    oriented_box_y(c,x,z,fx,fz, 0.00f,-0.31f,0.34f,0.045f,y0+0.39f,0.055f,mortar);
                    oriented_box_y(c,x,z,fx,fz,-0.27f, 0.0f,0.105f,0.25f,y0+0.73f,0.055f,cap);
                    oriented_box_y(c,x,z,fx,fz, 0.27f, 0.0f,0.105f,0.25f,y0+0.73f,0.055f,cap);
                }else if(t->material_tier==ODG_MATERIAL_IRON){
                    uint32_t plate=rgba_mix(trim,1.10f),rivet=rgba_mix(metal,1.20f);
                    oriented_box_y(c,x,z,fx,fz,-0.29f,0.04f,0.045f,0.28f,y0+0.46f,0.40f,rgba_mix(trim,1.06f));
                    oriented_box_y(c,x,z,fx,fz, 0.29f,0.04f,0.045f,0.28f,y0+0.46f,0.40f,rgba_mix(trim,1.06f));
                    oriented_box_y(c,x,z,fx,fz,0.0f,-0.22f,0.20f,0.055f,y0+0.52f,0.28f,rgba_mix(metal,1.08f));
                    /* Iron tier gets a continuous armoured collar plus exposed fasteners.
                     * Small highlights read strongly while orbiting without neon outlines. */
                    oriented_box_y(c,x,z,fx,fz,0.0f,-0.30f,0.34f,0.045f,y0+0.40f,0.31f,rgba_mix(plate,0.92f));
                    oriented_box_y(c,x,z,fx,fz,0.0f, 0.29f,0.32f,0.045f,y0+0.49f,0.22f,rgba_mix(plate,0.84f));
                    box_y(c,x-fz*0.27f-fx*0.30f,z+fx*0.27f-fz*0.30f,0.024f,0.024f,y0+0.64f,0.045f,rivet);
                    box_y(c,x+fz*0.27f-fx*0.30f,z-fx*0.27f-fz*0.30f,0.024f,0.024f,y0+0.64f,0.045f,rivet);
                    box_y(c,x-fz*0.27f-fx*0.30f,z+fx*0.27f-fz*0.30f,0.024f,0.024f,y0+0.47f,0.045f,rgba_mix(rivet,0.88f));
                    box_y(c,x+fz*0.27f-fx*0.30f,z-fx*0.27f-fz*0.30f,0.024f,0.024f,y0+0.47f,0.045f,rgba_mix(rivet,0.88f));
                }
                /* Ammunition status is integrated into the pedestal as a protected
                 * vertical gauge. The old free-standing yellow post looked like a stray
                 * prop and disconnected visually from the turret body. */
                if(ammo_ratio>0.0f){
                    float rx=fz,rz=-fx;
                    float mx=x+rx*0.405f-fx*0.10f,mz=z+rz*0.405f-fz*0.10f;
                    float fill=0.055f+ammo_ratio*0.30f;
                    box_y(c,mx,mz,0.058f,0.050f,y0+0.22f,0.40f,rgba_mix(dark,0.72f));
                    box_y(c,mx+rx*0.004f,mz+rz*0.004f,0.030f,0.052f,y0+0.245f,fill,rgba_mix(p.ammo,0.82f));
                    box_y(c,mx,mz,0.068f,0.060f,y0+0.60f,0.035f,rgba_mix(metal,0.76f));
                }
            }
        }
        if (t->target_kind!=ODG_TURRET_TARGET_NONE &&
            t->target_global_cell_x!=INT64_MIN && t->target_global_cell_z!=INT64_MIN) {
            {
                float tx=render_global_cell_center_local_f(t->target_global_cell_x,1);
                float tz=render_global_cell_center_local_f(t->target_global_cell_z,0);
                if(world_point_maybe_visible(c,tx,tz,2.0f)) {
                if(t->aim_ticks!=0u) {
                    uint32_t warn=((t->aim_ticks/10u)&1u)!=0u?rgba_mix(p.ammo,0.85f):rgba_mix(p.ammo,1.22f);
                    line_overlay(c,(rv3){x,terrain_yf(x,z)+1.42f,z},
                                 (rv3){tx,terrain_yf(tx,tz)+0.17f,tz},warn);
                }
                if(t->beam_ticks!=0u) {
                    line_overlay(c,(rv3){x,terrain_yf(x,z)+1.36f,z},
                                 (rv3){tx,terrain_yf(tx,tz)+0.11f,tz},rgba_mix(p.ammo,1.25f));
                }
                }
            }
        }
        turret_ammo_label(c,t,x,z,rgba_mix(p.ammo,1.18f));
    
}

static void render_turrets(const rcam *c) {
    render_visible_spatial(c,ODG_SPATIAL_KIND_TURRET,render_turret_ref);
}


static uint32_t pickup_tier_color(const odg_world_pickup *pickup,visual_palette p){
    if(pickup==NULL)return p.accent;
    if(pickup->stack.material_tier==ODG_MATERIAL_WOOD)return rgba_lerp(p.building,0xa9805affu,150u);
    if(pickup->stack.material_tier==ODG_MATERIAL_STONE)return rgba_lerp(p.building_alt,0xb6bec5ffu,145u);
    if(pickup->stack.material_tier==ODG_MATERIAL_IRON)return rgba_lerp(p.accent,0xdce5e8ffu,116u);
    return p.accent;
}

static void render_world_pickup_ref(const rcam *c,uint32_t i){
    static const float dirs[8][2]={{0.0f,1.0f},{0.7071f,0.7071f},{1.0f,0.0f},{0.7071f,-0.7071f},{0.0f,-1.0f},{-0.7071f,-0.7071f},{-1.0f,0.0f},{-0.7071f,0.7071f}};
    visual_palette p=palette();float beat=music_visual_beat();
    if(i>=g_odg.pickup_count)return;
const odg_world_pickup *pickup=&g_odg_pickups[i];float x,z,y,bob,fx,fz;uint32_t phase,rot,col,edge;const char *label;
        if(!pickup->active) return;
        x=render_global_fx_local_f(pickup->global_fx_x,1);z=render_global_fx_local_f(pickup->global_fx_z,0);
        if(!world_point_maybe_visible(c,x,z,1.2f)) return;
        phase=(uint32_t)((g_odg.tick+(uint64_t)i*13u)%UINT64_C(80));if(phase>40u)phase=80u-phase;
        bob=(float)phase/40.0f*0.10f+beat*0.035f;y=terrain_yf(x,z)+0.20f+bob;
        rot=(uint32_t)((g_odg.tick/18u+(uint64_t)i*3u)%UINT64_C(8));fx=dirs[rot][0];fz=dirs[rot][1];
        col=pickup->stack.type_id==ODG_ITEM_AMMO?p.ammo:pickup_tier_color(pickup,p);edge=rgba_mix(col,1.12f+beat*0.20f);
        ground_shadow(c,x,z,0.30f,0.22f,0.42f);
        /* Physical tech card: material frame + recessed dark face + symmetric back.
         * Captions are handled separately by the crisp anchored label layer. */
        {
            uint32_t frame=pickup_tier_color(pickup,p),panel=rgba_lerp(p.sky_top,p.building_alt,46u);
            /* Material owns the frame; function owns the circuitry.  This prevents wood,
             * stone and iron chips from becoming three monochrome signboards while keeping
             * their tier readable at a glance. */
            uint32_t tech=pickup->stack.type_id==ODG_ITEM_AMMO?p.ammo:
                (pickup->stack.type_id==ODG_ITEM_REPROGRAM_CHIP?p.accent:p.ammo);
            uint32_t contacts=rgba_lerp(p.ammo,UINT32_C(0xd7b55cff),118u);
            float front=0.044f,back=-0.044f;
            oriented_box_y(c,x,z,fx,fz,0.0f,0.0f,0.27f,0.040f,y,0.60f,rgba_mix(frame,0.78f));
            oriented_box_y(c,x,z,fx,fz,0.0f,front,0.215f,0.006f,y+0.055f,0.47f,panel);
            oriented_box_y(c,x,z,fx,fz,0.0f,back,0.215f,0.006f,y+0.055f,0.47f,rgba_mix(panel,0.86f));
            /* A second restrained key on the reverse makes the double-sided object read
             * intentionally during rotation rather than flashing a featureless black face. */
            oriented_box_y(c,x,z,fx,fz,0.0f,back-0.004f,0.062f,0.007f,y+0.22f,0.19f,rgba_mix(tech,0.78f));
            oriented_box_y(c,x,z,fx,fz,-0.105f,back-0.004f,0.040f,0.007f,y+0.27f,0.045f,rgba_mix(tech,0.68f));
            oriented_box_y(c,x,z,fx,fz, 0.105f,back-0.004f,0.040f,0.007f,y+0.27f,0.045f,rgba_mix(tech,0.68f));
            /* frame rails and a small technology key at the bottom */
            oriented_box_y(c,x,z,fx,fz,-0.232f,front,0.018f,0.008f,y+0.025f,0.54f,rgba_mix(frame,1.10f));
            oriented_box_y(c,x,z,fx,fz, 0.232f,front,0.018f,0.008f,y+0.025f,0.54f,rgba_mix(frame,1.10f));
            oriented_box_y(c,x,z,fx,fz,0.0f,front,0.20f,0.008f,y+0.535f,0.035f,rgba_mix(frame,1.08f));
            oriented_box_y(c,x,z,fx,fz,0.0f,front,0.055f,0.009f,y+0.045f,0.055f,tech);
            if(pickup->stack.type_id==ODG_ITEM_AMMO){
                /* three magazine cells */
                oriented_box_y(c,x,z,fx,fz,-0.075f,front,0.026f,0.010f,y+0.19f,0.20f,tech);
                oriented_box_y(c,x,z,fx,fz, 0.000f,front,0.026f,0.010f,y+0.19f,0.20f,rgba_mix(tech,1.10f));
                oriented_box_y(c,x,z,fx,fz, 0.075f,front,0.026f,0.010f,y+0.19f,0.20f,tech);
            }else if(pickup->stack.type_id==ODG_ITEM_REPROGRAM_CHIP){
                /* circuit / network mark */
                oriented_box_y(c,x,z,fx,fz,0.0f,front,0.034f,0.010f,y+0.22f,0.16f,tech);
                oriented_box_y(c,x,z,fx,fz,-0.085f,front,0.028f,0.010f,y+0.18f,0.055f,rgba_mix(tech,1.10f));
                oriented_box_y(c,x,z,fx,fz, 0.085f,front,0.028f,0.010f,y+0.18f,0.055f,rgba_mix(tech,1.10f));
                oriented_box_y(c,x,z,fx,fz,-0.085f,front,0.028f,0.010f,y+0.33f,0.055f,rgba_mix(tech,0.92f));
                oriented_box_y(c,x,z,fx,fz, 0.085f,front,0.028f,0.010f,y+0.33f,0.055f,rgba_mix(tech,0.92f));
                /* Contact fingers make the chip read as hardware rather than a sign card. */
                oriented_box_y(c,x,z,fx,fz,-0.135f,front+0.004f,0.024f,0.013f,y-0.105f,0.170f,rgba_mix(contacts,0.82f));
                oriented_box_y(c,x,z,fx,fz,-0.045f,front+0.004f,0.024f,0.013f,y-0.105f,0.170f,contacts);
                oriented_box_y(c,x,z,fx,fz, 0.045f,front+0.004f,0.024f,0.013f,y-0.105f,0.170f,contacts);
                oriented_box_y(c,x,z,fx,fz, 0.135f,front+0.004f,0.024f,0.013f,y-0.105f,0.170f,rgba_mix(contacts,0.82f));
            }else if(pickup->stack.type_id==ODG_ITEM_ASCENSION_CHIP){
                /* upward tier mark */
                oriented_box_y(c,x,z,fx,fz,0.0f,front,0.030f,0.010f,y+0.17f,0.22f,tech);
                oriented_box_y(c,x,z,fx,fz,-0.058f,front,0.050f,0.010f,y+0.37f,0.045f,tech);
                oriented_box_y(c,x,z,fx,fz, 0.058f,front,0.050f,0.010f,y+0.37f,0.045f,tech);
                oriented_box_y(c,x,z,fx,fz,-0.135f,front+0.004f,0.024f,0.013f,y-0.105f,0.170f,rgba_mix(contacts,0.86f));
                oriented_box_y(c,x,z,fx,fz,-0.045f,front+0.004f,0.024f,0.013f,y-0.105f,0.170f,rgba_mix(contacts,1.04f));
                oriented_box_y(c,x,z,fx,fz, 0.045f,front+0.004f,0.024f,0.013f,y-0.105f,0.170f,rgba_mix(contacts,1.04f));
                oriented_box_y(c,x,z,fx,fz, 0.135f,front+0.004f,0.024f,0.013f,y-0.105f,0.170f,rgba_mix(contacts,0.86f));
            }
        }
        label=pickup->stack.type_id==ODG_ITEM_AMMO?"AMMO":
              (pickup->stack.type_id==ODG_ITEM_REPROGRAM_CHIP?"REPROG":
              (pickup->stack.type_id==ODG_ITEM_ASCENSION_CHIP?"ASCENT":""));
        /* The card remains a rotating physical object.  Its short caption is a crisp
         * presentation label anchored to the card instead of micro-segment 3D text. */
        if(label[0]!='\0' && odg_dist2(g_odg.actors[0].x,g_odg.actors[0].z,pickup->x,pickup->z)<(int64_t)(3*ODG_FX_ONE)*(3*ODG_FX_ONE)) {
            queue_world_label(c,x,y+0.82f,z,label,(c->w>=1080u&&c->h>=1080u?2u:1u),
                              0xe7eef0ffu,0x081117b8u,edge);
        }
    
}

static void render_world_pickups(const rcam *c){
    render_visible_spatial(c,ODG_SPATIAL_KIND_PICKUP,render_world_pickup_ref);
}



static uint32_t decor_hash(int64_t x,int64_t z) {
    uint32_t lo_x=(uint32_t)(uint64_t)x,lo_z=(uint32_t)(uint64_t)z;
    uint32_t hi_x=(uint32_t)((uint64_t)x>>32u),hi_z=(uint32_t)((uint64_t)z>>32u);
    return visual_hash2(lo_x^visual_hash2(hi_x,0x7f4a7c15u),
                        lo_z^visual_hash2(hi_z,0x9e3779b9u));
}

/* Lightweight world dressing. These pieces are deliberately below actor collision scale:
 * they enrich the domain without creating invisible blockers or changing authoritative
 * navigation. Every placement is deterministic and generated directly by the C renderer. */
static void render_micro_scenery(const rcam *c) {
    uint32_t gz;
    visual_palette p=palette();
    for(gz=3u;gz+3u<ODG_GRID_SIZE;gz+=5u){
        uint32_t gx;
        for(gx=3u;gx+3u<ODG_GRID_SIZE;gx+=5u){
            int64_t world_gx=g_render_origin_cell_x+(int64_t)gx;
            int64_t world_gz=g_render_origin_cell_z+(int64_t)gz;
            uint32_t h=decor_hash(world_gx,world_gz);
            float x,z,y,jx,jz;
            uint32_t kind;
            int has_object=(h&7u)<=2u;
            if(!world_point_maybe_visible(c,-(float)ODG_WORLD_HALF_CELLS+(float)gx+0.5f,
                                           -(float)ODG_WORLD_HALF_CELLS+(float)gz+0.5f,2.2f)) continue;
            jx=((float)((h>>8u)&255u)/255.0f-0.5f)*2.2f;
            jz=((float)((h>>16u)&255u)/255.0f-0.5f)*2.2f;
            x=-(float)ODG_WORLD_HALF_CELLS+(float)gx+0.5f+jx;
            z=-(float)ODG_WORLD_HALF_CELLS+(float)gz+0.5f+jz;
            y=terrain_yf(x,z);kind=(h>>24u)&3u;
            /* Tiny blades and twigs cease being useful once they project to a pixel. Keep
             * mineral pieces slightly farther because their broader silhouette survives. */
            if(has_object){
                float odx=x-c->cam_x,odz=z-c->cam_z,od2=odx*odx+odz*odz;
                if(od2>484.0f&&kind!=2u)has_object=0;
                else if(od2>625.0f)has_object=0;
            }
            /* Ground material detail has a separate density from physical props. This
             * fills large empty expanses without inventing colliders or exposing a grid. */
            if(((h>>20u)&3u)!=3u){
                float pdx=x-c->cam_x,pdz=z-c->cam_z;
                float pd2=pdx*pdx+pdz*pdz;
                /* Flat material decals are useful only once perspective makes their
                 * polygonal edge invisible. Near the camera, real sprigs/pebbles carry
                 * the detail instead, eliminating the large octagons seen in close shots. */
                if(pd2>420.0f){
                    uint32_t ground=y>1.70f?p.land_high:(y>0.80f?p.land_mid:p.land_low);
                    uint8_t owner=odg_chunk_owner_at_global_cell(world_gx,world_gz);
                    uint32_t patch;
                    if(owner!=ODG_OWNER_NONE)ground=territory_color(ODG_ID_FROM_OWNER(owner),ground);
                    patch=rgba_lerp(ground,((h>>7u)&1u)!=0u?p.rock:p.coast,6u+(h&3u));
                    ground_patch8(c,x,z,0.40f+(float)((h>>12u)&7u)*0.032f,h^0xa63u,patch);
                }
            }
            if(!has_object)continue;
            if(kind==0u){
                uint32_t c0=rgba_mix(p.leaf_high,0.76f+(float)((h>>4u)&7u)*0.040f);
                float grass_scale=0.82f+(float)((h>>10u)&7u)*0.035f;
                if(((h>>13u)&1u)!=0u)grass_tuft(c,x,z,y,grass_scale,c0,h);
                else grass_sprig(c,x,z,grass_scale*1.10f,c0,h);
            }else if(kind==1u){
                uint32_t shrub=rgba_mix(p.leaf_low,0.84f+(float)((h>>5u)&7u)*0.035f);
                low_shrub(c,x,z,y,0.88f+(float)((h>>9u)&7u)*0.025f,shrub,rgba_mix(p.leaf_high,0.94f),h);
            }else if(kind==2u){
                uint32_t rc=rgba_mix(p.rock,0.82f+(float)((h>>6u)&7u)*0.05f);
                faceted_rock(c,x,z,0.15f,0.12f,y+0.018f,0.18f,rc,h^0x5au);
            }else{
                uint32_t wood=rgba_mix(p.trunk,0.70f+(float)((h>>3u)&7u)*0.035f);
                float dir=((h>>11u)&1u)!=0u?0.7071f:-0.7071f;
                oriented_box_y(c,x,z,dir,0.7071f,0.0f,0.0f,0.035f,0.26f,y+0.025f,0.060f,wood);
                faceted_rock(c,x+0.18f,z-0.10f,0.10f,0.08f,y+0.01f,0.075f,rgba_mix(p.rock,0.72f),h^0x55u);
            }
        }
    }
}

static void render_near_ground_detail(const rcam *c){
    int32_t gz;visual_palette p=palette();
    /* Fine dressing establishes metre-scale depth without adding collision geometry.
     * The distribution is world deterministic but deliberately camera-LOD'd: nearby
     * clusters gain a second blade group, while distant marks become sparser and blend
     * into atmospheric colour instead of turning the horizon into stippled noise. */
    for(gz=1;gz<(int32_t)ODG_GRID_SIZE-1;gz+=2){
        int32_t gx;
        for(gx=1;gx<(int32_t)ODG_GRID_SIZE-1;gx+=2){
            int64_t world_gx=g_render_origin_cell_x+(int64_t)gx;
            int64_t world_gz=g_render_origin_cell_z+(int64_t)gz;
            uint32_t h=decor_hash(world_gx,world_gz);
            float x,z,dx,dz,d2,jx,jz,y,scale;
            uint32_t fog_mix=0u;
            if((h&1u)!=0u)continue;
            jx=((float)((h>>8u)&255u)/255.0f-0.5f)*1.35f;
            jz=((float)((h>>16u)&255u)/255.0f-0.5f)*1.35f;
            x=-(float)ODG_WORLD_HALF_CELLS+(float)gx+0.5f+jx;
            z=-(float)ODG_WORLD_HALF_CELLS+(float)gz+0.5f+jz;
            dx=x-c->cam_x;dz=z-c->cam_z;d2=dx*dx+dz*dz;
            if(d2<3.25f||d2>784.0f)continue;
            /* Above ~20m only one quarter of candidate marks survives. */
            if(d2>400.0f&&((h>>2u)&3u)!=0u)continue;
            if(!world_point_maybe_visible(c,x,z,0.55f))continue;
            y=terrain_yf(x,z);
            scale=0.72f+(float)((h>>11u)&7u)*0.045f;
            if(d2>256.0f){
                float t=(d2-256.0f)/(784.0f-256.0f);
                if(t>1.0f)t=1.0f;
                fog_mix=18u+(uint32_t)(t*74.0f);
                scale*=1.0f-t*0.16f;
            }
            /* High/dry ground and one deterministic fraction of all sites expose small
             * mineral pieces. Lower ground favours sparse living/dry blade clusters. */
            if(y>1.72f||((h>>26u)&7u)==0u){
                uint32_t mineral=rgba_mix(p.rock,0.68f+(float)((h>>5u)&7u)*0.047f);
                mineral=rgba_lerp(mineral,p.fog,fog_mix);
                faceted_rock(c,x,z,0.073f*scale,0.056f*scale,y+0.009f,0.050f*scale,mineral,h^0xd3u);
                if(d2<144.0f&&((h>>18u)&3u)==0u){
                    faceted_rock(c,x+0.092f*scale,z-0.058f*scale,0.042f*scale,0.034f*scale,
                                 terrain_yf(x+0.092f*scale,z-0.058f*scale)+0.007f,0.028f*scale,
                                 rgba_mix(mineral,0.84f),h^0x93u);
                }
            }else if(((h>>27u)&7u)==0u){
                uint32_t dry=rgba_lerp(rgba_mix(p.trunk,0.84f),p.coast,70u);
                dry=rgba_lerp(dry,p.fog,fog_mix);
                grass_sprig(c,x,z,scale,dry,h^0x61u);
                if(d2<169.0f&&((h>>19u)&1u)!=0u)
                    grass_sprig(c,x+0.095f*scale,z+0.052f*scale,scale*0.72f,rgba_mix(dry,0.91f),h^0x17u);
            }else{
                uint32_t leaf=rgba_mix(((h>>25u)&1u)!=0u?p.leaf_high:p.leaf_low,
                                       0.69f+(float)((h>>4u)&7u)*0.038f);
                leaf=rgba_lerp(leaf,p.fog,fog_mix);
                if(d2<121.0f&&((h>>22u)&7u)==0u)grass_tuft(c,x,z,y,scale*0.88f,leaf,h);
                else grass_sprig(c,x,z,scale,leaf,h);
                if(d2<169.0f&&((h>>20u)&3u)==0u)
                    grass_sprig(c,x-0.082f*scale,z+0.066f*scale,scale*0.68f,rgba_mix(leaf,0.94f),h^0xb7u);
            }
        }
    }
}

static void render_domain_beacons(const rcam *c) {
    /* Fixed v14 arena landmarks are retired in Open Domain. */
    (void)c;
}

static void render_particles(const rcam *c) {
    uint32_t i;
    for (i = 0u; i < ODG_MAX_PARTICLES; ++i) {
        odg_particle *p = &g_odg.particles[i];
        if (!p->active) continue;
        {
            int32_t rx,rz;float x,z;
            if(!game_local_to_render_fx(p->x,p->z,&rx,&rz))continue;
            x=odg_fx_to_float(rx);z=odg_fx_to_float(rz);
            if(!world_point_maybe_visible(c,x,z,1.0f))continue;
            octa(c,x,z,0.055f,terrain_yf(x,z),odg_fx_to_float(p->y_fx)+0.07f,p->color);
        }
    }
}

/* One deterministic, allocation-free finishing pass. The small frozen S-curve works
 * like Music Motion's camera/output boundary: lighting stays in the scene, while final
 * contrast and the restrained lens vignette are applied only after all geometry.
 *
 * The radial term is separable, so cache each normalized axis when the viewport changes.
 * The per-pixel path then contains no division, 64-bit multiply, or color multiply: one
 * byte addition selects a precombined tone/vignette table. This matters on mobile CPUs
 * where the full-resolution finishing pass is otherwise more expensive than the scene. */
static void postprocess_frame(void) {
    static uint8_t axis_x[ODG_MAX_RENDER_WIDTH];
    static uint8_t axis_y[ODG_MAX_RENDER_HEIGHT];
    static uint8_t radial_vignette[257];
    static uint8_t graded[25][256];
    static uint32_t cached_w=UINT32_MAX,cached_h=UINT32_MAX;
    static uint32_t initialized=0u;
    uint32_t x,y,w=g_odg.width,h=g_odg.height;
    uint8_t *pixel=g_odg_framebuffer;
    if(w==0u || h==0u) return;
    if(initialized==0u){
        uint32_t shade,v;
        for(v=0u;v<256u;++v){
            uint32_t smooth=(v*v*(765u-2u*v)+32512u)/65025u;
            uint32_t curve=(v*6u+smooth*2u+4u)/8u;
            for(shade=0u;shade<=24u;++shade)
                graded[shade][v]=(uint8_t)((curve*(256u-shade))>>8u);
        }
        for(v=0u;v<=256u;++v){
            /* 88/128 is the former 11/16 radial threshold; 256/128 is the
             * squared corner radius. The output remains the same subtle 0..24. */
            radial_vignette[v]=(uint8_t)(v<=88u?0u:((v-88u)*15u)/168u);
        }
        initialized=1u;
    }
    if(cached_w!=w){
        uint64_t denom=(uint64_t)w*(uint64_t)w;
        for(x=0u;x<w;++x){
            int64_t dx=(int64_t)(2u*x)-(int64_t)w;
            uint64_t square=(uint64_t)(dx*dx);
            axis_x[x]=(uint8_t)((square*128u+denom/2u)/denom);
        }
        cached_w=w;
    }
    if(cached_h!=h){
        uint64_t denom=(uint64_t)h*(uint64_t)h;
        for(y=0u;y<h;++y){
            int64_t dy=(int64_t)(2u*y)-(int64_t)h;
            uint64_t square=(uint64_t)(dy*dy);
            axis_y[y]=(uint8_t)((square*128u+denom/2u)/denom);
        }
        cached_h=h;
    }
    for(y=0u;y<h;++y){
        uint32_t y_term=axis_y[y];
        for(x=0u;x<w;++x){
            const uint8_t *table=graded[radial_vignette[(uint32_t)axis_x[x]+y_term]];
            pixel[0]=table[pixel[0]];
            pixel[1]=table[pixel[1]];
            pixel[2]=table[pixel[2]];
            pixel+=4;
        }
    }
}

static void preview_turn(uint32_t yaw_q16,float *out_sin,float *out_cos){
    uint32_t quadrant=(yaw_q16>>14u)&3u;
    float t=(float)(yaw_q16&0x3fffu)/16384.0f;float t2=t*t;
    float sv=t*(1.5707963f-0.6459641f*t2+0.0796926f*t2*t2);
    float u=1.0f-t;float u2=u*u;float cv=u*(1.5707963f-0.6459641f*u2+0.0796926f*u2*u2);
    if(quadrant==0u){*out_sin=sv;*out_cos=cv;}else if(quadrant==1u){*out_sin=cv;*out_cos=-sv;}
    else if(quadrant==2u){*out_sin=-sv;*out_cos=-cv;}else{*out_sin=-cv;*out_cos=sv;}
}

void odg_render_internal(void) {
    rcam c;
    uint32_t i;
    int showcase = g_odg.presentation_mode == ODG_PRESENTATION_SHOWCASE;
    int remote = g_odg.remote_view_active != 0u;
    int avatar_preview = g_odg.avatar_preview_active != 0u;
    int camera_preview = g_odg.camera_preview_active != 0u;
    uint32_t view_camera_mode = camera_preview ? g_odg.camera_preview_mode : g_odg.camera_mode;
    int64_t center_cell_x,center_cell_z;
    float anchor_x,anchor_z;
    if(remote){
        g_render_center_global_fx_x=g_odg.remote_view_global_fx_x;
        g_render_center_global_fx_z=g_odg.remote_view_global_fx_z;
        g_render_remote_rebased=1u;
    }else{
        g_render_center_global_fx_x=odg_global_center_cell_x_internal()*(int64_t)ODG_FX_ONE;
        g_render_center_global_fx_z=odg_global_center_cell_z_internal()*(int64_t)ODG_FX_ONE;
        g_render_remote_rebased=0u;
    }
    center_cell_x=odg_floor_div_i64_internal(g_render_center_global_fx_x,(int64_t)ODG_FX_ONE);
    center_cell_z=odg_floor_div_i64_internal(g_render_center_global_fx_z,(int64_t)ODG_FX_ONE);
    g_render_origin_cell_x=center_cell_x-(int64_t)ODG_WORLD_HALF_CELLS;
    g_render_origin_cell_z=center_cell_z-(int64_t)ODG_WORLD_HALF_CELLS;
    anchor_x=remote?0.0f:odg_fx_to_float(g_odg.camera_anchor_x);
    anchor_z=remote?0.0f:odg_fx_to_float(g_odg.camera_anchor_z);

    c.w = g_odg.width;
    c.h = g_odg.height;
    c.forward_x = (float)(remote ? g_odg.remote_view_dir_x_q15 : g_odg.camera_dir_x_q15) / (float)ODG_Q15_ONE;
    c.forward_z = (float)(remote ? g_odg.remote_view_dir_z_q15 : g_odg.camera_dir_z_q15) / (float)ODG_Q15_ONE;
    if(avatar_preview){float ps,pc;preview_turn(g_odg.avatar_preview_yaw_q16,&ps,&pc);c.forward_x=-ps;c.forward_z=-pc;}
    else if(camera_preview){float ps,pc;preview_turn(g_odg.camera_preview_yaw_q16,&ps,&pc);c.forward_x=-ps;c.forward_z=-pc;}
    if (c.forward_x == 0.0f && c.forward_z == 0.0f) c.forward_z = 1.0f;
    c.right_x = c.forward_z;
    c.right_z = -c.forward_x;

    if (avatar_preview) {
        const odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];
        anchor_x=odg_fx_to_float(p->x);anchor_z=odg_fx_to_float(p->z);
        c.sp=0.13f;c.focal=(float)g_odg.width*0.72f;
        c.cam_x=anchor_x-c.forward_x*3.25f;c.cam_z=anchor_z-c.forward_z*3.25f;
        c.cam_y=terrain_yf(anchor_x,anchor_z)+1.65f;
    } else if (remote) {
        c.sp = 0.11f;
        c.focal = (float)g_odg.width * 0.64f;
        if ((float)g_odg.height * 0.58f > c.focal) c.focal = (float)g_odg.height * 0.58f;
        c.cam_x = anchor_x + c.forward_x * 0.22f;
        c.cam_z = anchor_z + c.forward_z * 0.22f;
        c.cam_y = terrain_yf(0.0f,0.0f)+1.30f;
    } else if (showcase) {
        /* The start screen is a live C-rendered world, not a static wallpaper. A wider,
         * slightly asymmetric dolly reveals coast, districts, territory, actors and
         * infrastructure behind the glass UI. It is presentation-only and never enters
         * the deterministic state hash or gameplay camera. */
        uint32_t phase=(uint32_t)(g_odg.tick%UINT64_C(720));
        float triangle;
        float side;
        if (phase>360u) phase=720u-phase;
        triangle=((float)phase/360.0f)*2.0f-1.0f;
        side=0.95f+triangle*0.42f;
        anchor_x += c.forward_x*2.60f;
        anchor_z += c.forward_z*2.60f;
        c.sp = 0.285f;
        c.focal = (float)g_odg.width * 0.505f;
        if((float)g_odg.height*0.42f>c.focal)c.focal=(float)g_odg.height*0.42f;
        c.cam_x = anchor_x - c.forward_x*8.20f + c.right_x*side;
        c.cam_z = anchor_z - c.forward_z*8.20f + c.right_z*side;
        {
            float anchor_y=terrain_yf(anchor_x,anchor_z)+4.10f;
            float clearance=terrain_yf(c.cam_x,c.cam_z)+1.45f;
            c.cam_y=anchor_y>clearance?anchor_y:clearance;
        }
    } else {
        float beat=camera_preview?0.0f:music_visual_beat();
        c.sp = (float)(camera_preview?g_odg.camera_preview_pitch_q15:g_odg.camera_pitch_q15) / (float)ODG_Q15_ONE;
        if (view_camera_mode==ODG_CAMERA_MODE_FIRST_PERSON) {
            /* First person keeps the eye inside the logical cube but never renders its
             * interior. Music uses FOV rather than physical dolly to reduce nausea. */
            c.focal=(float)g_odg.width*(0.62f-beat*0.022f);
            if((float)g_odg.height*(0.54f-beat*0.016f)>c.focal)c.focal=(float)g_odg.height*(0.54f-beat*0.016f);
            c.cam_x=anchor_x+c.forward_x*0.16f;
            c.cam_z=anchor_z+c.forward_z*0.16f;
            c.cam_y=odg_fx_to_float(g_odg.camera_height_fx);
        } else {
            /* Slightly wider third-person framing keeps the cube from dominating the
             * screen and lets the now-larger ecology establish scale around it. */
            c.focal=(float)g_odg.width*(0.545f-beat*0.032f);
            if((float)g_odg.height*(0.465f-beat*0.022f)>c.focal)c.focal=(float)g_odg.height*(0.465f-beat*0.022f);
            {
                float visual_distance;
                if(camera_preview){
                    visual_distance=view_camera_mode==ODG_CAMERA_MODE_CLOSE?1.75f:(view_camera_mode==ODG_CAMERA_MODE_FAR?4.50f:3.05f);
                }else visual_distance=(float)g_odg.camera_distance_fx/(float)ODG_FX_ONE-beat*0.36f;
                if (visual_distance<1.05f) visual_distance=1.05f;
                c.cam_x=anchor_x-c.forward_x*visual_distance;
                c.cam_z=anchor_z-c.forward_z*visual_distance;
            }
            c.cam_y = odg_fx_to_float(g_odg.camera_height_fx);
        }
    }
    /* sqrt(1-sp^2) without libm: the pitch range is small enough that the fourth-order
     * series is visually indistinguishable here and keeps freestanding WASM dependency-free. */
    {
        float s2=c.sp*c.sp;
        c.cp=1.0f-0.5f*s2-0.125f*s2*s2;
    }

    prepare_depth_fog();
    g_render_label_count=0u;
    clear_frame(&c);
    if(avatar_preview){
        const odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];
        if(p->active&&p->hp!=0u)runner(&c,p);
        postprocess_frame();return;
    }
    render_distant_terrain(&c);
    ground_and_grid(&c);
    render_surface_water(&c);
    /* Ownership is already folded into the terrain material in ground_and_grid().
     * Roads and architecture remain physical structure above it. */
    render_routes(&c);
    render_distant_landmarks(&c);
    render_distant_scenery(&c);
    render_near_ground_detail(&c);
    render_territory_edges(&c);
    render_trails(&c);
    render_micro_scenery(&c);
    render_resource_nodes(&c);
    render_construction_nodes(&c);
    render_artifact_nodes(&c);
    render_boundaries(&c);
    render_turrets(&c);
    render_world_pickups(&c);
    render_fauna(&c);
    render_fauna_nests(&c);

    if(!remote)for (i = 0u; i < g_odg.obstacle_count; ++i) render_world_obstacle(&c,&g_odg.obstacles[i]);

    for (i = 0u; i < ODG_MAX_ACTORS; ++i) {
        odg_actor *a = &g_odg.actors[i];
        if (!a->active || a->hp == 0u) continue;
        if (!remote && !showcase && i==ODG_PLAYER_ID && view_camera_mode==ODG_CAMERA_MODE_FIRST_PERSON) continue;
        if(remote){
            odg_actor view=*a;int32_t rx,rz;
            if(!game_local_to_render_fx(a->x,a->z,&rx,&rz))continue;
            if(rx>(80*ODG_FX_ONE)||rx<-(80*ODG_FX_ONE)||rz>(80*ODG_FX_ONE)||rz<-(80*ODG_FX_ONE))continue;
            view.x=rx;view.z=rz;runner(&c,&view);
        }else runner(&c,a);

    }
    render_domain_beacons(&c);
    {
      const odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];
      const odg_item_stack *selected=odg_inventory_selected_const(&p->inventory);
      if (!remote && !camera_preview && p->hp!=0u && selected!=NULL && selected->type_id==ODG_ITEM_TURRET) {
        int32_t tx=0,tz=0;
        int valid=odg_turret_drop_candidate_internal(p,&tx,&tz);
        float px=valid?odg_fx_to_float(tx):odg_fx_to_float(p->x)+(float)p->face_x_q15/(float)ODG_Q15_ONE*1.90f;
        float pz=valid?odg_fx_to_float(tz):odg_fx_to_float(p->z)+(float)p->face_z_q15/(float)ODG_Q15_ONE*1.90f;
        float py=terrain_yf(px,pz)+0.08f;
        uint32_t col=valid?0xffe28affu:0xff6f68ffu;
        /* Preview the exact authoritative deployment socket. The ghost and the action
         * use the same C function, so the turret can no longer land somewhere other
         * than where the player was shown it would land. */
        box_y(&c,px,pz,0.42f,0.42f,py,0.14f,rgba_mix(col,0.58f));
        line_overlay(&c,(rv3){px-0.52f,py+0.03f,pz},(rv3){px+0.52f,py+0.03f,pz},col);
        line_overlay(&c,(rv3){px,py+0.03f,pz-0.52f},(rv3){px,py+0.03f,pz+0.52f},col);
      }
      if(!camera_preview){
        render_construction_placement_ghost(&c,p,selected);
        render_artifact_placement_ghost(&c,p,selected);
      }
    }
    render_particles(&c);
    render_weather_rain(&c);
    postprocess_frame();
    render_screen_labels();
}
