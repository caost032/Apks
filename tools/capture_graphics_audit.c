#include "game_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define Q15_DIAG 23170

static int write_ppm(const char *path){
    const uint8_t *p=(const uint8_t *)odg_render_frame();
    FILE *f=fopen(path,"wb");uint32_t i,total;
    if(f==NULL||p==NULL)return 0;
    fprintf(f,"P6\n%u %u\n255\n",odg_render_width(),odg_render_height());
    total=odg_render_width()*odg_render_height();
    for(i=0u;i<total;++i)fwrite(p+i*4u,1u,3u,f);
    fclose(f);return 1;
}

static void clear_scene(int keep_player){
    uint32_t i;
    for(i=1u;i<ODG_MAX_ACTORS;++i)g_odg.actors[i].active=0u;
    for(i=0u;i<g_odg.resource_count;++i)g_odg_resources[i].active=0u;
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i)g_odg.fauna[i].active=0u;
    for(i=0u;i<ODG_FAUNA_MAX_NESTS;++i)g_odg.fauna_nests[i].active=0u;
    g_odg.fauna_count=0u;g_odg.fauna_nest_count=0u;
    for(i=0u;i<g_odg.artifact_count;++i)g_odg_artifacts[i].active=0u;
    for(i=0u;i<g_odg.pickup_count;++i)g_odg_pickups[i].active=0u;
    for(i=0u;i<g_odg.turret_count;++i)g_odg_turrets[i].active=0u;
    g_odg.resource_count=0u;g_odg.artifact_count=0u;g_odg.pickup_count=0u;g_odg.turret_count=0u;
    odg_construction_reset_runtime_internal();
    g_odg.obstacle_count=0u;g_odg.presentation_mode=ODG_PRESENTATION_GAMEPLAY;
    g_odg.remote_view_active=0u;g_odg.avatar_preview_active=0u;g_odg.camera_preview_active=0u;
    g_odg.camera_mode=ODG_CAMERA_MODE_MEDIUM;
    g_odg.actors[ODG_PLAYER_ID].active=keep_player?1u:0u;
    g_odg.actors[ODG_PLAYER_ID].hp=keep_player?g_odg.actors[ODG_PLAYER_ID].max_hp:0u;
    g_odg.actors[ODG_PLAYER_ID].face_x_q15=0;g_odg.actors[ODG_PLAYER_ID].face_z_q15=ODG_Q15_ONE;
    odg_entities_spatial_mark_dirty();
}

static void focus_camera(int32_t x,int32_t z,int32_t dir_x_q15,int32_t dir_z_q15,
                         int32_t distance_fx,int32_t pitch_q15,int32_t height_fx){
    g_odg.camera_anchor_x=x;g_odg.camera_anchor_z=z;
    g_odg.camera_dir_x_q15=dir_x_q15;g_odg.camera_dir_z_q15=dir_z_q15;
    g_odg.control_basis_x_q15=dir_x_q15;g_odg.control_basis_z_q15=dir_z_q15;
    g_odg.camera_distance_fx=distance_fx;g_odg.camera_target_distance_fx=distance_fx;
    g_odg.camera_pitch_q15=pitch_q15;
    g_odg.camera_height_fx=odg_terrain_height_fx(x,z)+height_fx;
}

static int capture_angle(const char *path,int32_t x,int32_t z,int32_t dir_x_q15,int32_t dir_z_q15,
                         int32_t distance_fx,int32_t pitch_q15,int32_t height_fx){
    focus_camera(x,z,dir_x_q15,dir_z_q15,distance_fx,pitch_q15,height_fx);
    return write_ppm(path);
}

static void make_turret(int32_t x,int32_t z,uint32_t tier,uint32_t ammo,uint32_t max_ammo){
    odg_turret *t;uint32_t id=g_odg.turret_count;
    if(!odg_entities_reserve_turrets(id+1u))return;
    ++g_odg.turret_count;t=&g_odg_turrets[id];odg_memset(t,0,sizeof(*t));
    t->active=1u;t->id=id;t->instance_id=UINT64_C(9001)+id;t->material_tier=tier;t->owner=ODG_OWNER_FROM_ID(0u);
    t->x=x;t->z=z;t->local_resident=1u;odg_local_fx_to_global_fx_internal(x,z,&t->global_fx_x,&t->global_fx_z);
    t->ammo=ammo;t->max_ammo=max_ammo;t->carried_by=ODG_TURRET_NONE;t->head_x_q15=0;t->head_z_q15=-ODG_Q15_ONE;
    t->target_global_cell_x=INT64_MIN;t->target_global_cell_z=INT64_MIN;t->target_actor_id=UINT32_MAX;t->mode=ODG_TURRET_MODE_DEFENSE;
    odg_entities_spatial_mark_dirty();
}

static void make_pickup(uint32_t type,uint32_t tier,int32_t x,int32_t z,uint32_t quantity){
    odg_item_stack s;odg_memset(&s,0,sizeof(s));s.type_id=type;s.quantity=quantity;s.material_tier=tier;
    (void)odg_spawn_world_pickup(&s,x,z,UINT32_C(9999));
}

static void make_artifact(uint32_t item,uint32_t tier,int32_t x,int32_t z){
    odg_artifact *a;uint32_t id=g_odg.artifact_count;
    if(!odg_entities_reserve_artifacts(id+1u))return;
    ++g_odg.artifact_count;a=&g_odg_artifacts[id];odg_memset(a,0,sizeof(*a));
    a->active=1u;a->id=id;a->instance_id=UINT64_C(10000)+id;a->item_type=item;a->owner_actor_id=0u;a->material_tier=tier;
    a->x=x;a->z=z;a->local_resident=1u;odg_local_fx_to_global_fx_internal(x,z,&a->global_fx_x,&a->global_fx_z);
    odg_entities_spatial_mark_dirty();
}

static void make_resource(uint32_t kind,int32_t x,int32_t z,uint64_t sid){
    odg_resource_node *r;uint32_t id=g_odg.resource_count;
    if(!odg_entities_reserve_resources(id+1u))return;
    ++g_odg.resource_count;r=&g_odg_resources[id];odg_memset(r,0,sizeof(*r));
    r->active=1u;r->id=id;r->stable_id=sid;r->kind=kind;r->state=ODG_RESOURCE_STATE_AVAILABLE;r->x=x;r->z=z;r->local_resident=1u;
    odg_local_fx_to_global_fx_internal(x,z,&r->global_fx_x,&r->global_fx_z);
    odg_entities_spatial_mark_dirty();
}

static odg_construction_block *make_construction(uint32_t shape,uint32_t tier,int32_t x,int32_t z){
    odg_construction_block *b;uint32_t id=g_odg_construction_count;
    if(!odg_entities_reserve_construction(id+1u))return NULL;
    ++g_odg_construction_count;b=&g_odg_construction_blocks[id];odg_memset(b,0,sizeof(*b));
    b->active=1u;b->id=id;b->instance_id=UINT64_C(0x434f4e5300000000)+(uint64_t)id;
    b->owner_actor_id=ODG_PLAYER_ID;b->material_tier=tier;b->shape=shape;b->x=x;b->z=z;b->local_resident=1u;
    b->max_health=odg_construction_max_health_internal(tier,shape);b->health=b->max_health;
    odg_local_fx_to_global_fx_internal(x,z,&b->global_fx_x,&b->global_fx_z);
    odg_entities_spatial_mark_dirty();return b;
}

static int find_dry_construction_stage(int32_t origin_x,int32_t origin_z,int32_t *out_x,int32_t *out_z){
    int32_t dz,dx;
    if(out_x==NULL||out_z==NULL)return 0;
    for(dz=-36;dz<=36;dz+=3){
        for(dx=-36;dx<=36;dx+=3){
            int32_t cx=origin_x+dx*ODG_FX_ONE,cz=origin_z+dz*ODG_FX_ONE;
            int32_t ox,oz;int valid=1;int32_t min_h=INT32_MAX,max_h=INT32_MIN;
            for(oz=-1;oz<=1&&valid;++oz){
                for(ox=-4;ox<=4;ox+=2){
                    odg_surface_sample s;int32_t sx=cx+ox*ODG_FX_ONE,sz=cz+oz*ODG_FX_ONE;
                    if(!odg_environment_surface_local(sx,sz,&s)||(s.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))!=0u){valid=0;break;}
                    if(s.height_milli<min_h)min_h=s.height_milli;
                    if(s.height_milli>max_h)max_h=s.height_milli;
                }
            }
            if(valid&&max_h-min_h<=450){*out_x=cx;*out_z=cz;return 1;}
        }
    }
    return 0;
}

static odg_resource_node *make_flora(uint32_t stage,uint32_t variant,uint32_t fruit_count,int32_t x,int32_t z){
    uint32_t id=g_odg.resource_count;odg_resource_node *r;
    if(!odg_resource_spawn_flora(ODG_FLORA_SPECIES_APPLE_TREE,stage,variant,x,z,UINT32_MAX))return NULL;
    if(id>=g_odg.resource_count)return NULL;
    r=&g_odg_resources[id];
    if(fruit_count>r->fruit_capacity)r->fruit_capacity=fruit_count;
    r->fruit_count=fruit_count;
    odg_entities_spatial_mark_dirty();return r;
}

static odg_fauna_entity *make_fauna(uint32_t species_id,uint32_t variant,uint32_t sex,int32_t x,int32_t z,int32_t y_offset_fx){
    uint32_t i;const odg_fauna_species_definition *species=odg_fauna_species_internal(species_id);odg_fauna_entity *e=NULL;
    if(species==NULL)return NULL;
    for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i)if(!g_odg.fauna[i].active){e=&g_odg.fauna[i];break;}
    if(e==NULL)return NULL;
    odg_memset(e,0,sizeof(*e));
    e->active=1u;e->id=i;e->stable_id=UINT64_C(0x4641554e415f0000)+(uint64_t)i;e->species_id=species_id;e->family=species->family;
    e->variant=species->variant_count!=0u?variant%species->variant_count:0u;e->state=1u;e->owner_actor_id=UINT32_MAX;
    e->max_hp=species->max_health;e->hp=e->max_hp;e->satiety_permille=1000u;e->hydration_permille=1000u;
    e->life_stage=ODG_FAUNA_STAGE_ADULT;e->sex=sex;e->nest_id=UINT32_MAX;e->x=x;e->z=z;e->local_resident=1u;e->y_offset_fx=y_offset_fx;
    e->face_x_q15=0;e->face_z_q15=ODG_Q15_ONE;odg_local_fx_to_global_fx_internal(x,z,&e->global_fx_x,&e->global_fx_z);
    ++g_odg.fauna_count;return e;
}

static odg_fauna_nest *make_nest(uint32_t species_id,uint32_t substrate,uint32_t eggs,int32_t x,int32_t z,uint64_t host){
    uint32_t i;odg_fauna_nest *n=NULL;
    for(i=0u;i<ODG_FAUNA_MAX_NESTS;++i)if(!g_odg.fauna_nests[i].active){n=&g_odg.fauna_nests[i];break;}
    if(n==NULL)return NULL;
    odg_memset(n,0,sizeof(*n));n->active=1u;n->id=i;n->stable_id=UINT64_C(0x4e4553545f000000)+(uint64_t)i;
    n->species_id=species_id;n->substrate=substrate;n->egg_count=eggs;n->hatch_ticks=ODG_TICK_RATE*30u;n->parent_a=UINT32_MAX;n->parent_b=UINT32_MAX;
    n->host_resource_stable_id=host;
    if(substrate==ODG_NEST_SUBSTRATE_TREE&&host!=0u){
        uint32_t ri;int found=0;
        for(ri=0u;ri<g_odg.resource_count;++ri){
            odg_resource_node *resource=&g_odg_resources[ri];
            if(resource->active&&resource->stable_id==host&&odg_fauna_tree_nest_position_internal(resource,&x,&z)){found=1;break;}
        }
        if(found==0){n->active=0u;return NULL;}
    }
    n->x=x;n->z=z;n->local_resident=1u;odg_local_fx_to_global_fx_internal(x,z,&n->global_fx_x,&n->global_fx_z);
    ++g_odg.fauna_nest_count;return n;
}

static int capture_resource_scale(int32_t px,int32_t pz){
    clear_scene(1);
    g_odg.actors[0].x=px;g_odg.actors[0].z=pz;
    odg_local_fx_to_global_fx_internal(px,pz,&g_odg.actors[0].global_fx_x,&g_odg.actors[0].global_fx_z);
    (void)make_flora(ODG_FLORA_STAGE_MATURE,1u,5u,px-(19*ODG_FX_ONE)/10,pz+(29*ODG_FX_ONE)/10);
    (void)make_flora(ODG_FLORA_STAGE_OLD,2u,7u,px+(18*ODG_FX_ONE)/10,pz+(36*ODG_FX_ONE)/10);
    (void)make_flora(ODG_FLORA_STAGE_YOUNG,3u,0u,px+(2*ODG_FX_ONE)/10,pz+(58*ODG_FX_ONE)/10);
    make_resource(ODG_RESOURCE_STONE,px-(26*ODG_FX_ONE)/10,pz+(10*ODG_FX_ONE)/10,UINT64_C(0x00000131));
    make_resource(ODG_RESOURCE_IRON,px+(27*ODG_FX_ONE)/10,pz+(12*ODG_FX_ONE)/10,UINT64_C(0x000001d7));
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/01_scale_ecology_front.ppm",px,pz+(25*ODG_FX_ONE)/10,0,ODG_Q15_ONE,7*ODG_FX_ONE,7600,2350))return 0;
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/02_scale_ecology_oblique.ppm",px,pz+(27*ODG_FX_ONE)/10,Q15_DIAG,Q15_DIAG,8*ODG_FX_ONE,9200,2750))return 0;
    return 1;
}

static int capture_turrets(int32_t px,int32_t pz){
    int32_t tz=pz+(31*ODG_FX_ONE)/10;
    clear_scene(0);
    make_turret(px-(17*ODG_FX_ONE)/10,tz,ODG_MATERIAL_WOOD,32u,48u);
    make_turret(px,tz,ODG_MATERIAL_STONE,32u,48u);
    make_turret(px+(17*ODG_FX_ONE)/10,tz,ODG_MATERIAL_IRON,32u,48u);
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/03_turrets_wood_stone_iron_front.ppm",px,tz,0,ODG_Q15_ONE,6*ODG_FX_ONE,6500,2050))return 0;
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/04_turrets_wood_stone_iron_left.ppm",px,tz,Q15_DIAG,Q15_DIAG,7*ODG_FX_ONE,7000,2200))return 0;
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/05_turrets_wood_stone_iron_right.ppm",px,tz,-Q15_DIAG,Q15_DIAG,7*ODG_FX_ONE,7000,2200))return 0;
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/06_turrets_wood_stone_iron_high.ppm",px,tz,0,ODG_Q15_ONE,7*ODG_FX_ONE,11200,3200))return 0;
    return 1;
}

static int capture_stations(int32_t px,int32_t pz){
    int32_t sz=pz+(30*ODG_FX_ONE)/10;
    clear_scene(0);
    make_artifact(ODG_ITEM_WORKBENCH,ODG_MATERIAL_WOOD,px-(17*ODG_FX_ONE)/10,sz);
    make_artifact(ODG_ITEM_SMITHY,ODG_MATERIAL_STONE,px,sz);
    make_artifact(ODG_ITEM_CHEST,ODG_MATERIAL_IRON,px+(17*ODG_FX_ONE)/10,sz);
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/07_stations_workbench_smithy_chest_front.ppm",px,sz,0,ODG_Q15_ONE,5*ODG_FX_ONE,6500,1850))return 0;
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/08_stations_workbench_smithy_chest_oblique.ppm",px,sz,Q15_DIAG,Q15_DIAG,6*ODG_FX_ONE,8000,2250))return 0;
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/09_stations_workbench_smithy_chest_high.ppm",px,sz,-Q15_DIAG,Q15_DIAG,6*ODG_FX_ONE,11000,3000))return 0;
    return 1;
}

static int capture_chips(int32_t px,int32_t pz){
    int32_t cz=pz+(29*ODG_FX_ONE)/10;
    clear_scene(0);
    make_pickup(ODG_ITEM_AMMO,ODG_MATERIAL_IRON,px-(24*ODG_FX_ONE)/10,cz,12u);
    make_pickup(ODG_ITEM_REPROGRAM_CHIP,ODG_MATERIAL_WOOD,px-(12*ODG_FX_ONE)/10,cz,1u);
    make_pickup(ODG_ITEM_REPROGRAM_CHIP,ODG_MATERIAL_STONE,px,cz,1u);
    make_pickup(ODG_ITEM_REPROGRAM_CHIP,ODG_MATERIAL_IRON,px+(12*ODG_FX_ONE)/10,cz,1u);
    make_pickup(ODG_ITEM_ASCENSION_CHIP,ODG_MATERIAL_IRON,px+(24*ODG_FX_ONE)/10,cz,1u);
    odg_entities_spatial_mark_dirty();
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/10_chips_and_ammo_front.ppm",px,cz,0,ODG_Q15_ONE,5*ODG_FX_ONE,6200,1750))return 0;
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/11_chips_and_ammo_reverse.ppm",px,cz,0,-ODG_Q15_ONE,5*ODG_FX_ONE,6200,1750))return 0;
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/12_chips_and_ammo_high_oblique.ppm",px,cz,Q15_DIAG,Q15_DIAG,6*ODG_FX_ONE,10500,2750))return 0;

    /* One-card detail passes guarantee that every tier can be judged face-on. The normal
     * multi-card scene intentionally keeps asynchronous world rotation, so one card may
     * occasionally be edge-on in a family shot. */
    clear_scene(0);g_odg.tick=0u;
    make_pickup(ODG_ITEM_REPROGRAM_CHIP,ODG_MATERIAL_WOOD,px,cz,1u);
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/15_chip_reprogram_wood_detail.ppm",px,cz,0,ODG_Q15_ONE,3*ODG_FX_ONE,6200,1500))return 0;
    clear_scene(0);g_odg.tick=0u;
    make_pickup(ODG_ITEM_REPROGRAM_CHIP,ODG_MATERIAL_STONE,px,cz,1u);
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/16_chip_reprogram_stone_detail.ppm",px,cz,0,ODG_Q15_ONE,3*ODG_FX_ONE,6200,1500))return 0;
    clear_scene(0);g_odg.tick=0u;
    make_pickup(ODG_ITEM_REPROGRAM_CHIP,ODG_MATERIAL_IRON,px,cz,1u);
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/17_chip_reprogram_iron_detail.ppm",px,cz,0,ODG_Q15_ONE,3*ODG_FX_ONE,6200,1500))return 0;
    clear_scene(0);g_odg.tick=0u;
    make_pickup(ODG_ITEM_ASCENSION_CHIP,ODG_MATERIAL_IRON,px,cz,1u);
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/18_chip_ascension_iron_detail.ppm",px,cz,0,ODG_Q15_ONE,3*ODG_FX_ONE,6200,1500))return 0;
    return 1;
}

static int capture_mining(int32_t px,int32_t pz){
    int32_t rz=pz+(27*ODG_FX_ONE)/10;
    clear_scene(0);
    make_resource(ODG_RESOURCE_STONE,px-(10*ODG_FX_ONE)/10,rz,UINT64_C(701));
    make_resource(ODG_RESOURCE_IRON,px+(10*ODG_FX_ONE)/10,rz,UINT64_C(702));
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/13_ore_stone_iron_front.ppm",px,rz,0,ODG_Q15_ONE,4*ODG_FX_ONE,6800,1650))return 0;
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/14_ore_stone_iron_oblique.ppm",px,rz,-Q15_DIAG,Q15_DIAG,5*ODG_FX_ONE,9000,2250))return 0;
    return 1;
}

static int capture_ecosystem(int32_t px,int32_t pz){
    int32_t z=pz+(34*ODG_FX_ONE)/10;odg_resource_node *host;uint64_t host_sid=0u;
    clear_scene(0);
    (void)make_flora(ODG_FLORA_STAGE_SEEDLING,0u,0u,px-4*ODG_FX_ONE,z);
    (void)make_flora(ODG_FLORA_STAGE_SAPLING,1u,0u,px-2*ODG_FX_ONE,z);
    (void)make_flora(ODG_FLORA_STAGE_YOUNG,2u,0u,px,z);
    (void)make_flora(ODG_FLORA_STAGE_MATURE,3u,6u,px+2*ODG_FX_ONE,z);
    (void)make_flora(ODG_FLORA_STAGE_OLD,4u,8u,px+4*ODG_FX_ONE,z);
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/19_flora_lifecycle_front.ppm",px,z,0,ODG_Q15_ONE,10*ODG_FX_ONE,7200,2600))return 0;
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/20_flora_lifecycle_oblique.ppm",px,z,Q15_DIAG,Q15_DIAG,11*ODG_FX_ONE,9000,3200))return 0;

    clear_scene(0);
    (void)make_fauna(ODG_FAUNA_SPECIES_FOREST_DEER,1u,ODG_FAUNA_SEX_MALE,px-(27*ODG_FX_ONE)/10,z,0);
    (void)make_fauna(ODG_FAUNA_SPECIES_MEADOW_RABBIT,0u,ODG_FAUNA_SEX_FEMALE,px-(9*ODG_FX_ONE)/10,z,0);
    (void)make_fauna(ODG_FAUNA_SPECIES_FIELD_FOWL,3u,ODG_FAUNA_SEX_MALE,px+(9*ODG_FX_ONE)/10,z,0);
    (void)make_fauna(ODG_FAUNA_SPECIES_ORCHARD_BIRD,2u,ODG_FAUNA_SEX_FEMALE,px+(27*ODG_FX_ONE)/10,z,(7*ODG_FX_ONE)/10);
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/21_fauna_four_species_front.ppm",px,z,0,ODG_Q15_ONE,7*ODG_FX_ONE,6000,1900))return 0;
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/22_fauna_four_species_oblique.ppm",px,z,-Q15_DIAG,Q15_DIAG,8*ODG_FX_ONE,7600,2350))return 0;

    clear_scene(0);
    host=make_flora(ODG_FLORA_STAGE_MATURE,1u,6u,px-(14*ODG_FX_ONE)/10,z);if(host!=NULL)host_sid=host->stable_id;
    (void)make_nest(ODG_FAUNA_SPECIES_ORCHARD_BIRD,ODG_NEST_SUBSTRATE_TREE,2u,px-(14*ODG_FX_ONE)/10,z,host_sid);
    (void)make_nest(ODG_FAUNA_SPECIES_FIELD_FOWL,ODG_NEST_SUBSTRATE_GROUND,3u,px+(14*ODG_FX_ONE)/10,z,0u);
    (void)make_fauna(ODG_FAUNA_SPECIES_ORCHARD_BIRD,1u,ODG_FAUNA_SEX_FEMALE,px-(21*ODG_FX_ONE)/10,z+(3*ODG_FX_ONE)/10,(9*ODG_FX_ONE)/10);
    (void)make_fauna(ODG_FAUNA_SPECIES_FIELD_FOWL,2u,ODG_FAUNA_SEX_FEMALE,px+(21*ODG_FX_ONE)/10,z+(3*ODG_FX_ONE)/10,0);
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/23_nesting_tree_ground_front.ppm",px,z,0,ODG_Q15_ONE,7*ODG_FX_ONE,6500,2100))return 0;
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/24_nesting_tree_ground_high.ppm",px,z,Q15_DIAG,Q15_DIAG,8*ODG_FX_ONE,11200,3400))return 0;
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/25_tree_nest_detail.ppm",px-(14*ODG_FX_ONE)/10,z,-Q15_DIAG,Q15_DIAG,3*ODG_FX_ONE,10800,3000))return 0;
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/26_ground_nest_detail.ppm",px+(14*ODG_FX_ONE)/10,z,Q15_DIAG,Q15_DIAG,3*ODG_FX_ONE,7000,1450))return 0;
    return 1;
}

static int capture_construction(int32_t px,int32_t pz){
    int32_t stage_x=px,stage_z=pz,z;
    clear_scene(0);
    if(!find_dry_construction_stage(px,pz,&stage_x,&stage_z))return 0;
    px=stage_x;z=stage_z;
    /* Readable family lineup plus two valid layered assemblies. The renderer consumes the
     * exact same shape IDs that gameplay/save/FFI use; no review-only model exists. */
    (void)make_construction(ODG_CONSTRUCTION_SHAPE_FLOOR,ODG_MATERIAL_WOOD,px-3*ODG_FX_ONE,z);
    (void)make_construction(ODG_CONSTRUCTION_SHAPE_WALL,ODG_MATERIAL_STONE,px-ODG_FX_ONE,z);
    (void)make_construction(ODG_CONSTRUCTION_SHAPE_DOORWAY,ODG_MATERIAL_WOOD,px+ODG_FX_ONE,z);
    (void)make_construction(ODG_CONSTRUCTION_SHAPE_ROOF,ODG_MATERIAL_WOOD,px+ODG_FX_ONE,z);
    (void)make_construction(ODG_CONSTRUCTION_SHAPE_WALL,ODG_MATERIAL_IRON,px+3*ODG_FX_ONE,z);
    (void)make_construction(ODG_CONSTRUCTION_SHAPE_ROOF,ODG_MATERIAL_IRON,px+3*ODG_FX_ONE,z);
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/27_construction_shapes_front.ppm",px,z,0,ODG_Q15_ONE,8*ODG_FX_ONE,6200,2150))return 0;
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/28_construction_shapes_oblique.ppm",px,z,Q15_DIAG,Q15_DIAG,9*ODG_FX_ONE,8200,2700))return 0;
    if(!capture_angle("artifacts/graphics_inspection_2026_08_20/29_construction_shapes_high.ppm",px,z,-Q15_DIAG,Q15_DIAG,9*ODG_FX_ONE,11800,3600))return 0;
    clear_scene(0);
    {
        odg_construction_block *damaged;
        (void)make_construction(ODG_CONSTRUCTION_SHAPE_WALL,ODG_MATERIAL_STONE,px-(13*ODG_FX_ONE)/10,z);
        damaged=make_construction(ODG_CONSTRUCTION_SHAPE_WALL,ODG_MATERIAL_STONE,px+(13*ODG_FX_ONE)/10,z);
        if(damaged==NULL)return 0;
        damaged->health=damaged->max_health/5u;
        if(!capture_angle("artifacts/graphics_inspection_2026_08_20/30_construction_damage_detail.ppm",px,z,Q15_DIAG,Q15_DIAG,5*ODG_FX_ONE,7600,1900))return 0;
    }
    return 1;
}

int main(void){
    int32_t px,pz;
    (void)system("mkdir -p artifacts/graphics_inspection_2026_08_20");
    if(odg_init(UINT64_C(0x5445525249544f52),1280u,720u)!=0)return 2;
    odg_set_visual_theme(ODG_VISUAL_THEME_NEON_TIDES);
    px=g_odg.actors[0].x;pz=g_odg.actors[0].z;
    if(!capture_resource_scale(px,pz))return 3;
    if(!capture_turrets(px,pz))return 4;
    if(!capture_stations(px,pz))return 5;
    if(!capture_chips(px,pz))return 6;
    if(!capture_mining(px,pz))return 7;
    if(!capture_ecosystem(px,pz))return 8;
    if(!capture_construction(px,pz))return 9;
    printf("graphics inspection captures: 30 views, native C renderer\n");
    return 0;
}
