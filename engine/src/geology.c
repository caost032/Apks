#include "game_internal.h"

#include <stdint.h>

/* Geology is pure worldgen: no mutable RNG cursor, no visited-chunk history. The same
 * (seed, global x/z, depth) always resolves to the same stratum/cave/vein. Surface
 * resources may expose this volume, but never invent an ore body independently. */

static uint64_t geo_mix64(uint64_t x){
    x^=x>>30u;x*=UINT64_C(0xbf58476d1ce4e5b9);
    x^=x>>27u;x*=UINT64_C(0x94d049bb133111eb);
    x^=x>>31u;return x;
}
static uint64_t geo_zigzag64(int64_t v){
    uint64_t u=(uint64_t)v;return (u<<1u)^(uint64_t)(-(int64_t)(u>>63u));
}
static uint32_t geo_smooth_q16(uint32_t t){
    uint64_t tt=((uint64_t)t*t)>>16u;
    uint64_t k=UINT64_C(196608)-2u*(uint64_t)t;
    uint64_t v=(tt*k)>>16u;return v>65535u?65535u:(uint32_t)v;
}
static uint32_t geo_lattice_u16(int64_t x,int64_t y,int64_t z,uint64_t stream){
    uint64_t h=g_odg.seed^stream;
    h^=geo_mix64(geo_zigzag64(x)+UINT64_C(0x9e3779b97f4a7c15));
    h^=geo_mix64(geo_zigzag64(y)+UINT64_C(0x94d049bb133111eb));
    h^=geo_mix64(geo_zigzag64(z)+UINT64_C(0xd1b54a32d192ed03));
    return (uint32_t)(geo_mix64(h)&UINT64_C(0xffff));
}
static uint32_t geo_lerp_q16(uint32_t a,uint32_t b,uint32_t t){
    int64_t d=(int64_t)b-(int64_t)a;
    int64_t v=(int64_t)a+((d*(int64_t)t)>>16u);
    if(v<0)return 0u;
    if(v>65535)return 65535u;
    return (uint32_t)v;
}
static uint32_t geo_noise3_q16(int64_t x,int64_t z,uint32_t depth_milli,
                               int64_t horizontal_scale,uint32_t vertical_scale_milli,
                               uint64_t stream){
    int64_t gx=odg_floor_div_i64_internal(x,horizontal_scale),gz=odg_floor_div_i64_internal(z,horizontal_scale);
    int64_t gy=odg_floor_div_i64_internal((int64_t)depth_milli,(int64_t)vertical_scale_milli);
    int64_t ox=x-gx*horizontal_scale,oz=z-gz*horizontal_scale;
    int64_t oy=(int64_t)depth_milli-gy*(int64_t)vertical_scale_milli;
    uint32_t tx=geo_smooth_q16((uint32_t)((ox*65535)/horizontal_scale));
    uint32_t tz=geo_smooth_q16((uint32_t)((oz*65535)/horizontal_scale));
    uint32_t ty=geo_smooth_q16((uint32_t)((oy*65535)/(int64_t)vertical_scale_milli));
    uint32_t c000=geo_lattice_u16(gx,gy,gz,stream),c100=geo_lattice_u16(gx+1,gy,gz,stream);
    uint32_t c010=geo_lattice_u16(gx,gy+1,gz,stream),c110=geo_lattice_u16(gx+1,gy+1,gz,stream);
    uint32_t c001=geo_lattice_u16(gx,gy,gz+1,stream),c101=geo_lattice_u16(gx+1,gy,gz+1,stream);
    uint32_t c011=geo_lattice_u16(gx,gy+1,gz+1,stream),c111=geo_lattice_u16(gx+1,gy+1,gz+1,stream);
    uint32_t x00=geo_lerp_q16(c000,c100,tx),x10=geo_lerp_q16(c010,c110,tx);
    uint32_t x01=geo_lerp_q16(c001,c101,tx),x11=geo_lerp_q16(c011,c111,tx);
    uint32_t y0=geo_lerp_q16(x00,x10,ty),y1=geo_lerp_q16(x01,x11,ty);
    return geo_lerp_q16(y0,y1,tz);
}

uint32_t odg_world_cave_openness_permille64(int64_t world_cell_x,int64_t world_cell_z,uint32_t depth_milli){
    uint32_t n,gate,dist,open,gate_factor;
    if(!g_odg.initialized||depth_milli<1800u||depth_milli>36000u)return 0u;
    /* Caves are continuous low-frequency volumes, not independent holes per cell. A
     * narrow band around a smooth 3-D isosurface creates connected chambers/tunnels;
     * the second field prevents one endless sheet from hollowing the whole world. */
    n=geo_noise3_q16(world_cell_x,world_cell_z,depth_milli,18,6200u,UINT64_C(0x434156455f4d4149));
    gate=geo_noise3_q16(world_cell_x,world_cell_z,depth_milli,9,3900u,UINT64_C(0x434156455f474154));
    dist=n>32768u?n-32768u:32768u-n;
    if(dist>=7200u||gate<24500u)return 0u;
    open=(7200u-dist)*1000u/7200u;
    gate_factor=gate>48000u?1000u:(gate-24500u)*1000u/(48000u-24500u);
    open=(uint32_t)(((uint64_t)open*gate_factor)/1000u);
    return open>1000u?1000u:open;
}

uint32_t odg_world_geology_material64(int64_t world_cell_x,int64_t world_cell_z,uint32_t depth_milli){
    uint32_t coal,iron;
    if(!g_odg.initialized)return ODG_GEOLOGY_MATERIAL_AIR;
    if(depth_milli<700u)return ODG_GEOLOGY_MATERIAL_TOPSOIL;
    if(depth_milli<2200u)return ODG_GEOLOGY_MATERIAL_SUBSOIL;
    if(odg_world_cave_openness_permille64(world_cell_x,world_cell_z,depth_milli)>=520u)
        return ODG_GEOLOGY_MATERIAL_AIR;
    coal=geo_noise3_q16(world_cell_x,world_cell_z,depth_milli,11,4200u,UINT64_C(0x434f414c5f564549));
    iron=geo_noise3_q16(world_cell_x,world_cell_z,depth_milli,9,4700u,UINT64_C(0x49524f4e5f564549));
    if(depth_milli>=6500u&&depth_milli<=34000u&&iron>=55200u)return ODG_GEOLOGY_MATERIAL_IRON_ORE;
    if(depth_milli>=2600u&&depth_milli<=19000u&&coal>=53500u)return ODG_GEOLOGY_MATERIAL_COAL_ORE;
    return ODG_GEOLOGY_MATERIAL_STONE;
}

uint32_t odg_world_geology_ore_resource64(int64_t world_cell_x,int64_t world_cell_z,uint32_t depth_milli){
    uint32_t material=odg_world_geology_material64(world_cell_x,world_cell_z,depth_milli);
    if(material==ODG_GEOLOGY_MATERIAL_COAL_ORE)return ODG_RESOURCE_COAL;
    if(material==ODG_GEOLOGY_MATERIAL_IRON_ORE)return ODG_RESOURCE_IRON;
    return 0u;
}

uint32_t odg_world_cave_entrance64(int64_t world_cell_x,int64_t world_cell_z){
    odg_surface_sample s;uint64_t req=0u;uint32_t a,b,cluster;
    if(!g_odg.initialized)return 0u;
    if(odg_world_surface_sample64(world_cell_x,world_cell_z,&s,sizeof(s),&req)!=ODG_STATUS_OK||
       (s.flags&ODG_SURFACE_FLAG_WATER)!=0u)return 0u;
    a=odg_world_cave_openness_permille64(world_cell_x,world_cell_z,2200u);
    b=odg_world_cave_openness_permille64(world_cell_x,world_cell_z,3600u);
    /* A broad 2-D clustering term makes mouths occur in small geological districts, not
     * salt-and-pepper holes. Rock/highland terrain lowers the threshold but is not the
     * only possible host, so forest cave systems still exist. */
    cluster=geo_noise3_q16(world_cell_x,world_cell_z,2600u,24,12000u,UINT64_C(0x434156455f4d4f55));
    if(s.biome==ODG_BIOME_ROCKY||s.biome==ODG_BIOME_HIGHLANDS)
        return (a>=470u&&b>=360u&&cluster>=30000u)?1u:0u;
    return (a>=560u&&b>=430u&&cluster>=36000u)?1u:0u;
}

int odg_geology_surface_exposure_internal(int64_t world_cell_x,int64_t world_cell_z,uint32_t resource_kind){
    static const uint32_t coal_depths[]={3200u,4800u,6500u,8200u};
    static const uint32_t iron_depths[]={7000u,9200u,11800u,14600u};
    const uint32_t *depths;uint32_t count,i;
    if(resource_kind==ODG_RESOURCE_COAL){depths=coal_depths;count=4u;}
    else if(resource_kind==ODG_RESOURCE_IRON){depths=iron_depths;count=4u;}
    else return 0;
    if(odg_world_cave_entrance64(world_cell_x,world_cell_z)==0u)return 0;
    for(i=0u;i<count;++i)
        if(odg_world_geology_ore_resource64(world_cell_x,world_cell_z,depths[i])==resource_kind)return 1;
    return 0;
}
