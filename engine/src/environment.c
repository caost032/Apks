#include "game_internal.h"

#include <stdint.h>

/* The previous 27800 threshold was unreachable with the authoritative broad-noise
 * terrain (a million-cell audit bottomed out near 31200), leaving STEEP as dead data.
 * 32200 is about a 10.7 degree grade: still rare in this broad terrain, but meaningful
 * for planting, ground-fauna habitat and future navigation cost. */
#define ODG_SURFACE_STEEP_NORMAL_Y_Q15 32200

static uint64_t env_mix(uint64_t v){
    v^=v>>30u;v*=UINT64_C(0xbf58476d1ce4e5b9);v^=v>>27u;v*=UINT64_C(0x94d049bb133111eb);v^=v>>31u;return v;
}
static uint32_t env_hash_permille(int64_t x,int64_t z,uint64_t stream){
    uint64_t v=g_odg.seed^stream^(uint64_t)x*UINT64_C(0x9e3779b97f4a7c15)^(uint64_t)z*UINT64_C(0xd1b54a32d192ed03);
    return (uint32_t)(env_mix(v)%UINT64_C(1001));
}

int32_t odg_world_surface_sample64(int64_t world_cell_x,int64_t world_cell_z,
                                   odg_surface_sample *out_sample,uint64_t capacity,uint64_t *out_required){
    int32_t h,hl,hr,hd,hu;
    int32_t dx,dz;
    int64_t ny;
    uint32_t moisture,flags=0u,biome;
    if(out_required!=NULL)*out_required=(uint64_t)sizeof(odg_surface_sample);
    if(!g_odg.initialized)return ODG_STATUS_INVALID_STATE;
    if(out_sample==NULL||capacity<(uint64_t)sizeof(*out_sample))return ODG_STATUS_BUFFER_TOO_SMALL;
    if(odg_world_height_milli64(world_cell_x,world_cell_z,&h)!=ODG_STATUS_OK)return ODG_STATUS_INVALID_STATE;
    (void)odg_world_height_milli64(world_cell_x-1,world_cell_z,&hl);
    (void)odg_world_height_milli64(world_cell_x+1,world_cell_z,&hr);
    (void)odg_world_height_milli64(world_cell_x,world_cell_z-1,&hd);
    (void)odg_world_height_milli64(world_cell_x,world_cell_z+1,&hu);
    dx=hl-hr;dz=hd-hu;ny=2000;
    {
        uint64_t mag2=(uint64_t)((int64_t)dx*dx+(int64_t)dz*dz+ny*ny);
        uint32_t mag=odg_isqrt_u64(mag2);
        if(mag==0u)mag=1u;
        odg_memset(out_sample,0,sizeof(*out_sample));out_sample->struct_size=(uint32_t)sizeof(*out_sample);
        out_sample->normal_x_q15=(int32_t)(((int64_t)dx*ODG_Q15_ONE)/(int64_t)mag);
        out_sample->normal_y_q15=(int32_t)((ny*ODG_Q15_ONE)/(int64_t)mag);
        out_sample->normal_z_q15=(int32_t)(((int64_t)dz*ODG_Q15_ONE)/(int64_t)mag);
    }
    moisture=env_hash_permille(world_cell_x/8,world_cell_z/8,UINT64_C(0x4d4f495354555245));
    if(g_odg.weather_rain_permille>0u){uint32_t add=g_odg.weather_rain_permille/3u;moisture=moisture+add>1000u?1000u:moisture+add;}
    if(h>3400)biome=ODG_BIOME_HIGHLANDS;
    else if(h<480 && moisture>680u)biome=ODG_BIOME_WETLAND;
    else if(moisture>620u)biome=ODG_BIOME_FOREST;
    else if(moisture<300u)biome=ODG_BIOME_ROCKY;
    else biome=ODG_BIOME_PLAIN;
    /* Local basins below 220 mm become shallow surface water. Terrain remains present
     * underneath, so actors can never fall through a separate water mesh. */
    if(h<220){flags|=ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_WET;out_sample->water_depth_milli=(uint32_t)(220-h);}
    if(moisture>650u)flags|=ODG_SURFACE_FLAG_WET;
    if(out_sample->normal_y_q15<ODG_SURFACE_STEEP_NORMAL_Y_Q15)flags|=ODG_SURFACE_FLAG_STEEP;
    if(h>3000)flags|=ODG_SURFACE_FLAG_MOUNTAIN;
    out_sample->height_milli=h;out_sample->biome=biome;out_sample->moisture_permille=moisture;
    out_sample->rain_permille=g_odg.weather_rain_permille;out_sample->flags=flags;
    return ODG_STATUS_OK;
}

int odg_environment_surface_local(int32_t x,int32_t z,odg_surface_sample *out_sample){
    int64_t gx,gz;uint64_t required=0u;
    if(out_sample==NULL)return 0;
    odg_local_fx_to_global_cell_internal(x,z,&gx,&gz);
    return odg_world_surface_sample64(gx,gz,out_sample,sizeof(*out_sample),&required)==ODG_STATUS_OK;
}


int odg_environment_normal_local_q15(int32_t x,int32_t z,int32_t *out_x,int32_t *out_y,int32_t *out_z){
    const int32_t sample=ODG_FX_ONE/4;
    int32_t hl,hr,hd,hu,nx,ny,nz;
    uint64_t mag2;uint32_t mag;
    if(out_x==NULL||out_y==NULL||out_z==NULL)return 0;
    hl=odg_terrain_height_fx(x-sample,z);hr=odg_terrain_height_fx(x+sample,z);
    hd=odg_terrain_height_fx(x,z-sample);hu=odg_terrain_height_fx(x,z+sample);
    nx=hl-hr;nz=hd-hu;ny=sample*2;
    mag2=(uint64_t)((int64_t)nx*nx+(int64_t)ny*ny+(int64_t)nz*nz);
    mag=odg_isqrt_u64(mag2);if(mag==0u)mag=1u;
    *out_x=(int32_t)(((int64_t)nx*ODG_Q15_ONE)/(int64_t)mag);
    *out_y=(int32_t)(((int64_t)ny*ODG_Q15_ONE)/(int64_t)mag);
    *out_z=(int32_t)(((int64_t)nz*ODG_Q15_ONE)/(int64_t)mag);
    return 1;
}

void odg_environment_tick(void){
    /* Weather changes by long epochs and interpolates deterministically. No per-tick RNG
     * means save/load, replay and bot decisions remain reproducible. */
    const uint32_t epoch_ticks=60u*ODG_TICK_RATE;
    uint32_t epoch=(uint32_t)(g_odg.tick/(uint64_t)epoch_ticks);
    uint32_t phase=(uint32_t)(g_odg.tick%(uint64_t)epoch_ticks);
    uint32_t a=(uint32_t)(env_mix(g_odg.seed^UINT64_C(0x5745415448455231)^(uint64_t)epoch)%1001u);
    uint32_t b=(uint32_t)(env_mix(g_odg.seed^UINT64_C(0x5745415448455231)^(uint64_t)(epoch+1u))%1001u);
    uint32_t blended=(uint32_t)(((uint64_t)a*(epoch_ticks-phase)+(uint64_t)b*phase)/epoch_ticks);
    /* Mostly dry, with gradual showers rather than permanent drizzle. */
    g_odg.weather_rain_permille=blended>640u?(blended-640u)*1000u/360u:0u;
}

uint32_t odg_day_index(void){
    return g_odg.initialized?(uint32_t)(g_odg.tick/(uint64_t)ODG_DAY_LENGTH_TICKS):0u;
}
uint32_t odg_day_phase_permille(void){
    uint64_t shifted;
    if(!g_odg.initialized)return 0u;
    /* Worlds begin in established morning rather than forcing a first-minute night. */
    shifted=g_odg.tick+(uint64_t)ODG_DAY_LENGTH_TICKS/4u;
    return (uint32_t)(((shifted%(uint64_t)ODG_DAY_LENGTH_TICKS)*1000u)/(uint64_t)ODG_DAY_LENGTH_TICKS);
}
uint32_t odg_daylight_permille(void){
    uint32_t p=odg_day_phase_permille();
    /* 24-minute deterministic cycle: broad readable day, gradual dusk/dawn, and a real
     * dark interval. Moonlight never becomes zero so navigation remains possible before
     * torches, while local lights can still matter strongly. */
    if(p<80u)return 260u+(p*740u)/80u;
    if(p<470u)return 1000u;
    if(p<570u)return 1000u-((p-470u)*740u)/100u;
    if(p<920u)return 260u;
    return 260u+((p-920u)*740u)/80u;
}
uint32_t odg_is_night(void){
    uint32_t p=odg_day_phase_permille();
    return (p>=570u&&p<920u)?1u:0u;
}

uint32_t odg_weather_rain_permille(void){return g_odg.initialized?g_odg.weather_rain_permille:0u;}
