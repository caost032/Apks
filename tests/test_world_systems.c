#include "game_internal.h"

#include <stdint.h>
#include <stdio.h>

static int failures=0;
#define CHECK(expr) do { if(!(expr)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expr);++failures;} } while(0)

static int find_local_surface(int want_water,uint32_t min_depth,int require_neutral,
                              int32_t *out_x,int32_t *out_z){
    int32_t x,z;
    for(z=-60;z<=60;++z)for(x=-60;x<=60;++x){
        int32_t fx=x*ODG_FX_ONE,fz=z*ODG_FX_ONE;odg_surface_sample s;
        if(!odg_environment_surface_local(fx,fz,&s))continue;
        if(want_water){
            if((s.flags&ODG_SURFACE_FLAG_WATER)==0u||s.water_depth_milli<min_depth)continue;
        }else if((s.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))!=0u)continue;
        if(require_neutral&&odg_territory_actor_controls_position(ODG_PLAYER_ID,fx,fz))continue;
        *out_x=fx;*out_z=fz;return 1;
    }
    return 0;
}


static int find_owned_dry_surface(uint32_t actor_id,int32_t *out_x,int32_t *out_z){
    int32_t x,z;
    for(z=-60;z<=60;++z)for(x=-60;x<=60;++x){
        int32_t fx=x*ODG_FX_ONE,fz=z*ODG_FX_ONE;odg_surface_sample surface;
        if(!odg_environment_surface_local(fx,fz,&surface))continue;
        if((surface.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))!=0u)continue;
        if(!odg_territory_actor_controls_position(actor_id,fx,fz))continue;
        *out_x=fx;*out_z=fz;return 1;
    }
    return 0;
}

static int find_placeable_actor_position(uint32_t actor_id,int32_t *out_x,int32_t *out_z){
    int32_t x,z;odg_actor *actor=&g_odg.actors[actor_id];
    for(z=-48;z<=48;++z)for(x=-48;x<=48;++x){
        int32_t fx=x*ODG_FX_ONE,fz=z*ODG_FX_ONE,cx,cz;odg_surface_sample surface;
        if(!odg_environment_surface_local(fx,fz,&surface)||(surface.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))!=0u)continue;
        if(!odg_territory_actor_controls_position(actor_id,fx,fz))continue;
        actor->x=fx;actor->z=fz;actor->vertical_offset_fx=0;actor->vertical_velocity_fx=0;actor->local_resident=1u;
        odg_local_fx_to_global_fx_internal(fx,fz,&actor->global_fx_x,&actor->global_fx_z);
        if(odg_construction_placement_candidate_internal(actor,&cx,&cz,NULL,NULL)){*out_x=fx;*out_z=fz;return 1;}
    }
    return 0;
}

static uint32_t first_fauna(uint32_t species){
    uint32_t i;for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i)
        if(g_odg.fauna[i].active&&g_odg.fauna[i].species_id==species)return i;
    return UINT32_MAX;
}

static uint32_t fauna_count(uint32_t species){
    uint32_t i,n=0u;for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i)
        if(g_odg.fauna[i].active&&g_odg.fauna[i].species_id==species)++n;
    return n;
}

static int find_coastal_turret_site(int64_t *out_gx,int64_t *out_gz,uint32_t *out_land_cells){
    int64_t base_gx=odg_global_center_cell_x_internal(),base_gz=odg_global_center_cell_z_internal();
    int32_t oz,ox;
    for(oz=-60;oz<=60;++oz)for(ox=-60;ox<=60;++ox){
        int64_t gx=base_gx+(int64_t)ox,gz=base_gz+(int64_t)oz;
        odg_surface_sample center;uint64_t required=0u;uint32_t land=0u;int32_t dz,dx;int32_t lx,lz;
        if(odg_world_surface_sample64(gx,gz,&center,sizeof(center),&required)!=ODG_STATUS_OK)continue;
        if((center.flags&ODG_SURFACE_FLAG_WATER)!=0u)continue;
        if(!odg_global_cell_center_to_local_fx_internal(gx,gz,&lx,&lz))continue;
        for(dz=-ODG_TURRET_CAPTURE_RADIUS;dz<=ODG_TURRET_CAPTURE_RADIUS;++dz)
            for(dx=-ODG_TURRET_CAPTURE_RADIUS;dx<=ODG_TURRET_CAPTURE_RADIUS;++dx){
                odg_surface_sample sample;required=0u;
                if(odg_world_surface_sample64(gx+(int64_t)dx,gz+(int64_t)dz,&sample,sizeof(sample),&required)!=ODG_STATUS_OK)continue;
                if((sample.flags&ODG_SURFACE_FLAG_WATER)==0u)++land;
            }
        /* Need a real coast and a strict land majority that is <=12 votes. Under the old
         * buggy denominator of all 25 cells, that exact majority could never commission. */
        if(land>0u&&land<25u&&(land/2u+1u)<=12u){*out_gx=gx;*out_gz=gz;*out_land_cells=land;return 1;}
    }
    return 0;
}

static void place_actor(odg_actor *actor,int32_t x,int32_t z){
    actor->x=x;actor->z=z;actor->vertical_offset_fx=0;actor->vertical_velocity_fx=0;
    actor->local_resident=1u;odg_local_fx_to_global_fx_internal(x,z,&actor->global_fx_x,&actor->global_fx_z);
}

static void remove_procedural_resources_for_chunk(int64_t cx,int64_t cz){
    uint32_t read_i,write_i=0u;
    for(read_i=0u;read_i<g_odg.resource_count;++read_i){
        const odg_resource_node *r=&g_odg_resources[read_i];
        if(r->procedural!=0u&&r->chunk_x==cx&&r->chunk_z==cz)continue;
        if(write_i!=read_i)g_odg_resources[write_i]=g_odg_resources[read_i];
        g_odg_resources[write_i].id=write_i;++write_i;
    }
    for(read_i=write_i;read_i<g_odg.resource_count;++read_i)odg_memset(&g_odg_resources[read_i],0,sizeof(g_odg_resources[read_i]));
    g_odg.resource_count=write_i;odg_entities_spatial_mark_dirty();
}

static void test_worldgen4_resource_canonicality(void){
    typedef struct {uint32_t ordinal,kind,species;uint64_t stable;int64_t x,z;} saved_resource;
    saved_resource saved[64];uint32_t saved_count=0u,i,first=UINT32_MAX,target_ordinal=UINT32_MAX;
    int64_t pgx,pgz,cx,cz;odg_actor bot_backup;
    CHECK(odg_init(UINT64_C(1),320u,180u)==ODG_STATUS_OK);
    CHECK(odg_worldgen_version()==ODG_WORLDGEN_VERSION_RESOURCE_ID_NAMESPACES);
    odg_resources_stream_refresh();
    odg_global_fx_to_global_cell_internal(g_odg.actors[ODG_PLAYER_ID].global_fx_x,g_odg.actors[ODG_PLAYER_ID].global_fx_z,&pgx,&pgz);
    cx=odg_floor_div_i64_internal(pgx,(int64_t)ODG_CHUNK_SIZE_CELLS);
    cz=odg_floor_div_i64_internal(pgz,(int64_t)ODG_CHUNK_SIZE_CELLS);
    for(i=0u;i<g_odg.resource_count&&saved_count<64u;++i){
        const odg_resource_node *r=&g_odg_resources[i];saved_resource *s;
        if(!r->active||r->procedural==0u||r->chunk_x!=cx||r->chunk_z!=cz)continue;
        s=&saved[saved_count++];s->ordinal=r->chunk_ordinal;s->kind=r->kind;s->species=r->species_id;
        s->stable=r->stable_id;s->x=r->global_fx_x;s->z=r->global_fx_z;
        if(first==UINT32_MAX||r->chunk_ordinal<first)first=r->chunk_ordinal;
    }
    CHECK(saved_count>=2u&&first!=UINT32_MAX);
    if(saved_count<2u||first==UINT32_MAX)return;
    CHECK(odg_chunk_mark_resource_depleted(cx,cz,first)!=0);
    remove_procedural_resources_for_chunk(cx,cz);odg_resources_stream_refresh();
    for(i=0u;i<saved_count;++i){
        uint32_t j;int found=0;
        if(saved[i].ordinal==first)continue;
        for(j=0u;j<g_odg.resource_count;++j){
            const odg_resource_node *r=&g_odg_resources[j];
            if(!r->active||r->procedural==0u||r->chunk_x!=cx||r->chunk_z!=cz||r->chunk_ordinal!=saved[i].ordinal)continue;
            CHECK(r->kind==saved[i].kind&&r->stable_id==saved[i].stable&&r->species_id==saved[i].species);
            CHECK(r->global_fx_x==saved[i].x&&r->global_fx_z==saved[i].z);found=1;target_ordinal=saved[i].ordinal;break;
        }
        CHECK(found!=0);
    }
    CHECK(target_ordinal!=UINT32_MAX);
    if(target_ordinal!=UINT32_MAX){
        saved_resource *target=NULL;uint32_t j;
        for(i=0u;i<saved_count;++i)if(saved[i].ordinal==target_ordinal){target=&saved[i];break;}
        CHECK(target!=NULL);
        if(target!=NULL){
            bot_backup=g_odg.actors[1u];remove_procedural_resources_for_chunk(cx,cz);
            g_odg.actors[1u].active=1u;g_odg.actors[1u].hp=g_odg.actors[1u].max_hp!=0u?g_odg.actors[1u].max_hp:100u;
            g_odg.actors[1u].global_fx_x=target->x;g_odg.actors[1u].global_fx_z=target->z;
            CHECK(odg_global_fx_to_local_internal(target->x,target->z,&g_odg.actors[1u].x,&g_odg.actors[1u].z)!=0);
            g_odg.actors[1u].local_resident=1u;odg_resources_stream_refresh();
            for(j=0u;j<g_odg.resource_count;++j){
                const odg_resource_node *r=&g_odg_resources[j];
                CHECK(!(r->procedural!=0u&&r->chunk_x==cx&&r->chunk_z==cz&&r->chunk_ordinal==target_ordinal));
            }
            g_odg.actors[1u].global_fx_x=target->x+5*(int64_t)ODG_FX_ONE;g_odg.actors[1u].global_fx_z=target->z;
            CHECK(odg_global_fx_to_local_internal(g_odg.actors[1u].global_fx_x,g_odg.actors[1u].global_fx_z,&g_odg.actors[1u].x,&g_odg.actors[1u].z)!=0);
            odg_resources_stream_refresh();
            for(j=0u;j<g_odg.resource_count;++j){
                const odg_resource_node *r=&g_odg_resources[j];
                if(r->procedural!=0u&&r->chunk_x==cx&&r->chunk_z==cz&&r->chunk_ordinal==target_ordinal){
                    CHECK(r->global_fx_x==target->x&&r->global_fx_z==target->z&&r->stable_id==target->stable&&r->species_id==target->species);
                    break;
                }
            }
            CHECK(j<g_odg.resource_count);g_odg.actors[1u]=bot_backup;
        }
    }
}

static int find_offwindow_turret_candidate(odg_actor *actor,int32_t *out_x,int32_t *out_z){
    static const int32_t faces[][2]={{ODG_Q15_ONE,0},{-ODG_Q15_ONE,0},{0,ODG_Q15_ONE},{0,-ODG_Q15_ONE}};
    int32_t px,pz;uint32_t f;uint8_t own;
    if(actor==NULL||out_x==NULL||out_z==NULL)return 0;
    own=ODG_OWNER_FROM_ID(actor->id);
    /* Deliberately leave the resident 128x128 territory cache. The candidate must use the
     * global chunk owner, not clamp this position back onto the local edge. */
    for(px=70;px<=92;px+=2)for(pz=-12;pz<=12;pz+=2){
        int64_t cgx,cgz;int32_t dz,dx;
        place_actor(actor,px*ODG_FX_ONE,pz*ODG_FX_ONE);
        odg_local_fx_to_global_cell_internal(actor->x,actor->z,&cgx,&cgz);
        for(dz=-6;dz<=6;++dz)for(dx=-6;dx<=6;++dx)odg_chunk_set_owner_at_global_cell(cgx+dx,cgz+dz,own);
        for(f=0u;f<(uint32_t)(sizeof(faces)/sizeof(faces[0]));++f){
            actor->face_x_q15=faces[f][0];actor->face_z_q15=faces[f][1];
            if(odg_turret_drop_candidate_internal(actor,out_x,out_z))return 1;
        }
    }
    return 0;
}

static void test_dynamic_placement_exclusion(void){
    odg_actor *p,*bot;odg_actor bot_backup;int32_t x1=0,z1=0,x2=0,z2=0;uint32_t f;
    static const int32_t faces[][2]={{ODG_Q15_ONE,0},{-ODG_Q15_ONE,0},{0,ODG_Q15_ONE},{0,-ODG_Q15_ONE}};
    CHECK(odg_init(UINT64_C(0x44594e414d4943),320u,180u)==ODG_STATUS_OK);
    p=&g_odg.actors[ODG_PLAYER_ID];bot=&g_odg.actors[1u];bot_backup=*bot;

    /* Artifact placement may search an alternate fan position, but it must never return
     * the body-occupied point it just advertised. */
    for(f=0u;f<(uint32_t)(sizeof(faces)/sizeof(faces[0]));++f){
        p->face_x_q15=faces[f][0];p->face_z_q15=faces[f][1];
        if(odg_artifact_placement_candidate_for_item_internal(p,ODG_ITEM_WORKBENCH,&x1,&z1))break;
    }
    CHECK(f<(uint32_t)(sizeof(faces)/sizeof(faces[0])));
    if(f<(uint32_t)(sizeof(faces)/sizeof(faces[0]))){
        place_actor(bot,x1,z1);
        if(odg_artifact_placement_candidate_for_item_internal(p,ODG_ITEM_WORKBENCH,&x2,&z2)){
            CHECK(x2!=x1||z2!=z1);
            CHECK(odg_dynamic_position_clear_internal(x2,z2,4*ODG_FX_ONE/5,p->id,UINT32_MAX)!=0);
        }
    }
    *bot=bot_backup;

    /* Turret territory authority is global Open Domain. The fixture is intentionally
     * outside the resident local mask, then repeats the body-exclusion check. */
    CHECK(find_offwindow_turret_candidate(p,&x1,&z1)!=0);
    if(find_offwindow_turret_candidate(p,&x1,&z1)){
        place_actor(bot,x1,z1);
        if(odg_turret_drop_candidate_internal(p,&x2,&z2)){
            CHECK(x2!=x1||z2!=z1);
            CHECK(odg_dynamic_position_clear_internal(x2,z2,ODG_TURRET_COLLISION_RADIUS_FX,p->id,UINT32_MAX)!=0);
        }
    }
    *bot=bot_backup;odg_entities_spatial_mark_dirty();
}

static int prepare_raft_site(uint32_t actor_id,int32_t *out_x,int32_t *out_z){
    static const int32_t faces[][2]={{ODG_Q15_ONE,0},{-ODG_Q15_ONE,0},{0,ODG_Q15_ONE},{0,-ODG_Q15_ONE}};
    odg_actor *actor=&g_odg.actors[actor_id];int32_t x,z;uint32_t f;
    for(z=-60;z<=60;++z)for(x=-60;x<=60;++x){
        int32_t fx=x*ODG_FX_ONE,fz=z*ODG_FX_ONE,cx,cz;odg_surface_sample surface;int64_t gx,gz;int32_t ox,oz;
        if(!odg_environment_surface_local(fx,fz,&surface)||(surface.flags&ODG_SURFACE_FLAG_WATER)==0u||surface.water_depth_milli<800u)continue;
        place_actor(actor,fx,fz);odg_local_fx_to_global_cell_internal(fx,fz,&gx,&gz);
        for(oz=-5;oz<=5;++oz)for(ox=-5;ox<=5;++ox)odg_chunk_set_owner_at_global_cell(gx+ox,gz+oz,ODG_OWNER_FROM_ID(actor_id));
        for(f=0u;f<(uint32_t)(sizeof(faces)/sizeof(faces[0]));++f){
            actor->face_x_q15=faces[f][0];actor->face_z_q15=faces[f][1];
            if(odg_artifact_placement_candidate_for_item_internal(actor,ODG_ITEM_RAFT,&cx,&cz)){*out_x=cx;*out_z=cz;return 1;}
        }
    }
    return 0;
}

int main(void){
    const uint64_t seed=UINT64_C(0x45434f5359533231);
    uint32_t i;
    test_worldgen4_resource_canonicality();
    test_dynamic_placement_exclusion();
    CHECK(odg_init(seed,320u,180u)==ODG_STATUS_OK);
    CHECK(odg_api_version()==ODG_API_VERSION);
    CHECK(odg_save_schema_supported(ODG_SAVE_SCHEMA_VERSION)!=0);
    CHECK(odg_worldgen_version()==ODG_WORLDGEN_VERSION_CURRENT);
    CHECK(odg_content_registry_validate()!=0);

    /* Time is gameplay authority: new worlds begin in broad daylight, night has a real
     * low-light interval, and the same phase is consumed by hostile ecology + renderer. */
    g_odg.tick=0u;
    CHECK(odg_day_phase_permille()==250u);
    CHECK(odg_is_night()==0u);
    CHECK(odg_daylight_permille()>=900u);
    g_odg.tick=(uint64_t)ODG_DAY_LENGTH_TICKS*45u/100u; /* raw .45 + morning offset = .70 */
    CHECK(odg_day_phase_permille()>=695u&&odg_day_phase_permille()<=705u);
    CHECK(odg_is_night()!=0u);
    CHECK(odg_daylight_permille()<=350u);

    /* Geology is a deterministic 3-D volume. Surface soil never turns into ore; caves and
     * both current ore families must exist below ground, and any materialized mineral node
     * must correspond to a real cave-mouth seam rather than a surface dice roll. */
    {
        int64_t cx=odg_global_center_cell_x_internal(),cz=odg_global_center_cell_z_internal();
        uint32_t caves=0u,coal_cells=0u,iron_cells=0u,entrances=0u,coal_nodes=0u,iron_nodes=0u;
        int32_t z,x;
        for(z=-64;z<=64;z+=2)for(x=-64;x<=64;x+=2){
            int64_t gx=cx+x,gz=cz+z;uint32_t m;
            CHECK(odg_world_geology_material64(gx,gz,500u)==ODG_GEOLOGY_MATERIAL_TOPSOIL);
            if(odg_world_cave_openness_permille64(gx,gz,5000u)>=520u)++caves;
            m=odg_world_geology_material64(gx,gz,6000u);if(m==ODG_GEOLOGY_MATERIAL_COAL_ORE)++coal_cells;
            m=odg_world_geology_material64(gx,gz,12000u);if(m==ODG_GEOLOGY_MATERIAL_IRON_ORE)++iron_cells;
            if(odg_world_cave_entrance64(gx,gz)!=0u)++entrances;
        }
        CHECK(caves>0u&&coal_cells>0u&&iron_cells>0u&&entrances>0u);
        {
            odg_chunk_descriptor descriptor;uint64_t required=0u;
            int64_t chunk_x=odg_floor_div_i64_internal(cx,(int64_t)ODG_CHUNK_SIZE_CELLS);
            int64_t chunk_z=odg_floor_div_i64_internal(cz,(int64_t)ODG_CHUNK_SIZE_CELLS);
            CHECK(odg_chunk_descriptor_get(chunk_x,chunk_z,&descriptor,sizeof(descriptor),&required)==ODG_STATUS_OK);
            CHECK(descriptor.reserved_u32[0]==0u&&descriptor.reserved_u32[1]==0u&&descriptor.reserved_u32[2]==0u);
            CHECK(odg_chunk_coal_candidate_count_internal(chunk_x,chunk_z)>0u);
        }
        for(i=0u;i<g_odg.resource_count;++i){
            const odg_resource_node *r=&g_odg_resources[i];int64_t gx,gz;
            if(!r->active||(r->kind!=ODG_RESOURCE_COAL&&r->kind!=ODG_RESOURCE_IRON))continue;
            odg_global_fx_to_global_cell_internal(r->global_fx_x,r->global_fx_z,&gx,&gz);
            CHECK(odg_geology_surface_exposure_internal(gx,gz,r->kind)!=0);
            if(r->kind==ODG_RESOURCE_COAL)++coal_nodes;else ++iron_nodes;
        }
        CHECK(coal_nodes>0u&&iron_nodes>0u);
    }

    /* v2 bathymetry is not a flat blue plane: find genuinely deep water, prove terrain
     * exists underneath it, then exercise oxygen depletion, drowning damage and recovery. */
    {
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];int32_t wx=0,wz=0,dx=0,dz=0;uint32_t hp;
        odg_surface_sample water,dry;
        CHECK(find_local_surface(1,900u,0,&wx,&wz)!=0);
        CHECK(odg_environment_surface_local(wx,wz,&water)!=0);
        CHECK((water.flags&ODG_SURFACE_FLAG_WATER)!=0u&&water.water_depth_milli>=900u);
        CHECK(water.height_milli<220); /* water surface is above a real non-flat bottom */
        CHECK(find_local_surface(0,0u,0,&dx,&dz)!=0);
        CHECK(odg_environment_surface_local(dx,dz,&dry)!=0&&(dry.flags&ODG_SURFACE_FLAG_WATER)==0u);
        /* Full simulation gives an air-breathing actor buoyancy instead of pinning it
         * to the lake bed. Standing still in deep water rises toward the swim draft. */
        place_actor(p,wx,wz);p->hp=p->max_hp;
        g_odg_persistent_runtime.actors[ODG_PLAYER_ID].oxygen_permille=1000u;
        odg_set_input(0,0,0,ODG_Q15_ONE,0u);
        odg_step_ticks(90u);
        CHECK(odg_actor_is_swimming_internal(p)!=0);
        CHECK(p->vertical_offset_fx>0);
        CHECK(p->hp==p->max_hp);
        /* Drowning remains a real failure mode if the actor is actually held below the
         * surface (e.g. future cave/obstacle effects); survival and locomotion are separate. */
        p->vertical_offset_fx=0;p->vertical_velocity_fx=0;
        g_odg_persistent_runtime.actors[ODG_PLAYER_ID].oxygen_permille=3u;
        g_odg_persistent_runtime.actors[ODG_PLAYER_ID].oxygen_loss_accum=3u;
        for(i=0u;i<12u;++i)odg_survival_tick();
        CHECK(odg_player_oxygen_permille()==0u);
        hp=p->hp;for(i=0u;i<180u;++i)odg_survival_tick();
        CHECK(p->hp+8u==hp);
        place_actor(p,dx,dz);for(i=0u;i<500u;++i)odg_survival_tick();
        CHECK(odg_player_oxygen_permille()==1000u);
    }

    /* Aquatic fauna own the water medium instead of being terrestrial fauna painted blue. */
    {
        uint32_t fish=0u;
        for(i=0u;i<ODG_FAUNA_MAX_ENTRIES;++i){
            odg_fauna_entity *e=&g_odg.fauna[i];odg_surface_sample s;
            if(!e->active||e->species_id!=ODG_FAUNA_SPECIES_RIVER_FISH)continue;
            ++fish;CHECK(odg_environment_surface_local(e->x,e->z,&s)!=0);
            CHECK((s.flags&ODG_SURFACE_FLAG_WATER)!=0u);
            CHECK(s.water_depth_milli>=odg_fauna_species_internal(e->species_id)->body_radius_milli+120u);
            CHECK(e->y_offset_fx>0);
            g_odg_persistent_runtime.fauna[i].oxygen_permille=1u;odg_survival_tick();
            CHECK(g_odg_persistent_runtime.fauna[i].oxygen_permille==1000u);
        }
        CHECK(fish>0u);
    }

    /* The first monster is conditional ecology, not ambient decoration. It spawns only at
     * night for a player outside their own territory, acquires from a fair approach range,
     * cannot attack that player after they regain territorial safety, and retreats by day. */
    {
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];
        int32_t home_x=0,home_z=0,neutral_x=0,neutral_z=0;
        uint32_t monster,hp,before;
        for(i=1u;i<ODG_MAX_ACTORS;++i)g_odg.actors[i].active=0u;
        CHECK(find_owned_dry_surface(ODG_PLAYER_ID,&home_x,&home_z)!=0);
        CHECK(find_local_surface(0,0u,1,&neutral_x,&neutral_z)!=0);
        place_actor(p,neutral_x,neutral_z);CHECK(!odg_territory_actor_controls_position(ODG_PLAYER_ID,p->x,p->z));
        before=fauna_count(ODG_FAUNA_SPECIES_NIGHT_STALKER);
        g_odg.tick=(uint64_t)ODG_DAY_LENGTH_TICKS*45u/100u;
        CHECK((g_odg.tick%(4u*ODG_TICK_RATE))==0u&&odg_is_night()!=0u);
        odg_fauna_tick();
        CHECK(fauna_count(ODG_FAUNA_SPECIES_NIGHT_STALKER)>before);
        monster=first_fauna(ODG_FAUNA_SPECIES_NIGHT_STALKER);CHECK(monster!=UINT32_MAX);
        if(monster!=UINT32_MAX){
            odg_fauna_entity *m=&g_odg.fauna[monster];
            CHECK(g_odg_persistent_runtime.fauna_target_actor[monster]==ODG_PLAYER_ID);
            /* Force contact while outside: one balanced strike, then cooldown prevents a frame-rate blender. */
            m->x=p->x+ODG_FX_ONE/2;m->z=p->z;m->local_resident=1u;
            odg_local_fx_to_global_fx_internal(m->x,m->z,&m->global_fx_x,&m->global_fx_z);
            g_odg_persistent_runtime.fauna_target_actor[monster]=ODG_PLAYER_ID;
            g_odg_persistent_runtime.fauna_attack_cooldown[monster]=0u;hp=p->hp;
            odg_fauna_tick();CHECK(p->hp+9u==hp);hp=p->hp;odg_fauna_tick();CHECK(p->hp==hp);
            /* Home territory is a hard eligibility boundary for this species. */
            place_actor(p,home_x,home_z);
            CHECK(odg_territory_actor_controls_position(ODG_PLAYER_ID,p->x,p->z));
            m->x=p->x+ODG_FX_ONE/2;m->z=p->z;m->local_resident=1u;
            odg_local_fx_to_global_fx_internal(m->x,m->z,&m->global_fx_x,&m->global_fx_z);
            g_odg_persistent_runtime.fauna_target_actor[monster]=ODG_PLAYER_ID;
            g_odg_persistent_runtime.fauna_attack_cooldown[monster]=0u;hp=p->hp;
            odg_fauna_tick();CHECK(p->hp==hp);CHECK(g_odg_persistent_runtime.fauna_target_actor[monster]==UINT32_MAX);
            /* Daylight causes a distant nocturnal stalker to leave the active ecology. */
            m->x=p->x+20*ODG_FX_ONE;m->z=p->z;m->local_resident=1u;
            odg_local_fx_to_global_fx_internal(m->x,m->z,&m->global_fx_x,&m->global_fx_z);
            g_odg.tick=0u;odg_fauna_tick();CHECK(m->active==0u);
        }
    }

    /* Amphibious predators prove that water ecology is not a binary fish/land split.
     * Crocodiles breathe air, can occupy water or shoreline, nest on ground and have
     * explicit combat/loot data rather than inheriting generic mammal behavior. */
    {
        const odg_fauna_species_definition *c=odg_fauna_species_internal(ODG_FAUNA_SPECIES_MARSH_CROCODILE);
        const odg_fauna_nesting_definition *n=odg_fauna_nesting_internal(ODG_FAUNA_SPECIES_MARSH_CROCODILE);
        odg_loot_table_definition loot;int found_loot=0;uint32_t li;
        for(li=0u;li<odg_loot_table_count();++li){uint64_t req=0u;if(odg_loot_table_get(li,&loot,sizeof(loot),&req)==ODG_STATUS_OK&&loot.loot_table_id==ODG_LOOT_MARSH_CROCODILE){found_loot=1;break;}}
        CHECK(c!=NULL&&c->family==ODG_FAUNA_FAMILY_REPTILE);
        CHECK((c->behavior_flags&ODG_FAUNA_BEHAVIOR_AMPHIBIOUS)!=0u);
        CHECK((c->behavior_flags&ODG_FAUNA_BEHAVIOR_AQUATIC)==0u);
        CHECK((c->behavior_flags&ODG_FAUNA_BEHAVIOR_HOSTILE_ACTORS)!=0u);
        CHECK((c->behavior_flags&ODG_FAUNA_BEHAVIOR_PREDATOR)!=0u);
        CHECK(n!=NULL&&(n->substrate_mask&ODG_NEST_SUBSTRATE_GROUND)!=0u);
        CHECK(found_loot!=0&&loot.entry_count>=2u);
        /* Natural predation consumes prey into predator nutrition instead of cloning the
         * player's hunting loot, and a prey floor keeps the local fish population viable. */
        {
            uint32_t croc=first_fauna(ODG_FAUNA_SPECIES_MARSH_CROCODILE);
            uint32_t fish=first_fauna(ODG_FAUNA_SPECIES_RIVER_FISH);int32_t wx=0,wz=0;uint32_t sat;
            CHECK(croc!=UINT32_MAX&&fish!=UINT32_MAX&&fauna_count(ODG_FAUNA_SPECIES_RIVER_FISH)>3u);
            CHECK(find_local_surface(1,800u,0,&wx,&wz)!=0);
            if(croc!=UINT32_MAX&&fish!=UINT32_MAX){
                g_odg.fauna[croc].x=wx;g_odg.fauna[croc].z=wz;g_odg.fauna[croc].local_resident=1u;
                odg_local_fx_to_global_fx_internal(wx,wz,&g_odg.fauna[croc].global_fx_x,&g_odg.fauna[croc].global_fx_z);
                g_odg.fauna[fish].x=wx;g_odg.fauna[fish].z=wz;g_odg.fauna[fish].local_resident=1u;
                odg_local_fx_to_global_fx_internal(wx,wz,&g_odg.fauna[fish].global_fx_x,&g_odg.fauna[fish].global_fx_z);
                g_odg.fauna[croc].satiety_permille=100u;sat=g_odg.fauna[croc].satiety_permille;
                odg_fauna_tick();CHECK(!g_odg.fauna[fish].active);CHECK(g_odg.fauna[croc].satiety_permille>sat);
                CHECK(fauna_count(ODG_FAUNA_SPECIES_RIVER_FISH)>=3u);
            }
        }
    }

    /* Worldgen no longer gifts ammunition. Manufactured or intentionally dropped ammo
     * may still exist later, but the initial ambient pickup population contains none. */
    for(i=0u;i<g_odg.pickup_count;++i){
        const odg_world_pickup *pickup=&g_odg_pickups[i];
        CHECK(!pickup->active||pickup->stack.type_id!=ODG_ITEM_AMMO);
        if(pickup->active&&pickup->stack.type_id==ODG_ITEM_REPROGRAM_CHIP){
            int64_t gx,gz;odg_surface_sample surface;uint64_t required=0u;
            odg_global_fx_to_global_cell_internal(pickup->global_fx_x,pickup->global_fx_z,&gx,&gz);
            CHECK(odg_world_cell_safe_ground_internal(gx,gz)!=0);
            CHECK(odg_world_surface_sample64(gx,gz,&surface,sizeof(surface),&required)==ODG_STATUS_OK);
            CHECK((surface.flags&(ODG_SURFACE_FLAG_WATER|ODG_SURFACE_FLAG_STEEP))==0u);
        }
    }
    {
        odg_recipe_definition ammo_recipe;uint64_t required=0u;
        CHECK(odg_recipe_get(ODG_RECIPE_AMMO_X12,&ammo_recipe,sizeof(ammo_recipe),&required)==ODG_STATUS_OK);
        CHECK(ammo_recipe.station_item_type==ODG_STATION_SMITHY);
        CHECK(ammo_recipe.output_item_type==ODG_ITEM_AMMO&&ammo_recipe.ingredients[0].item_type==ODG_ITEM_IRON);
    }

    /* Turret item/chip operations are strict inventory transactions. Invalid placement
     * must not allocate hidden inactive turret slots; consuming the final ascension chip
     * must use the removed stack's tier rather than reading a slot that was just cleared. */
    {
        odg_actor *p;odg_item_stack turret_item;uint32_t before,slot=0u,try_i;int64_t pgx,pgz;int32_t dz,dx;
        CHECK(odg_init(UINT64_C(0x545552525f54584e),320u,180u)==ODG_STATUS_OK);
        p=&g_odg.actors[ODG_PLAYER_ID];
        odg_memset(&p->inventory,0,sizeof(p->inventory));p->inventory.slot_count=ODG_INVENTORY_BASE_SLOTS;
        odg_memset(&turret_item,0,sizeof(turret_item));turret_item.type_id=ODG_ITEM_TURRET;turret_item.quantity=1u;turret_item.material_tier=ODG_MATERIAL_WOOD;
        CHECK(odg_inventory_add(&p->inventory,&turret_item)!=0);
        CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_TURRET,ODG_MATERIAL_WOOD,&slot)!=0);p->inventory.selected_slot=slot;
        odg_global_fx_to_global_cell_internal(p->global_fx_x,p->global_fx_z,&pgx,&pgz);
        for(dz=-8;dz<=8;++dz)for(dx=-8;dx<=8;++dx)
            odg_chunk_set_owner_at_global_cell(pgx+(int64_t)dx,pgz+(int64_t)dz,ODG_OWNER_FROM_ID(1u));
        before=g_odg.turret_count;
        for(try_i=0u;try_i<8u;++try_i)CHECK(odg_turret_place_selected(ODG_PLAYER_ID)==0);
        CHECK(g_odg.turret_count==before);
        CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_TURRET,ODG_MATERIAL_WOOD)==1u);
    }
    {
        odg_actor *p;odg_turret *t;odg_item_stack chip;odg_interaction_hint hint;uint64_t required=0u;uint32_t ti,slot=0u;
        CHECK(odg_init(UINT64_C(0x415343454e445458),320u,180u)==ODG_STATUS_OK);
        p=&g_odg.actors[ODG_PLAYER_ID];ti=g_odg.turret_count;CHECK(odg_entities_reserve_turrets(ti+1u)!=0);
        if(g_odg_turret_capacity>ti){
            t=&g_odg_turrets[ti];odg_memset(t,0,sizeof(*t));g_odg.turret_count=ti+1u;
            t->active=1u;t->id=ti;t->instance_id=odg_next_instance_id();t->owner=ODG_OWNER_FROM_ID(ODG_PLAYER_ID);
            t->carried_by=ODG_TURRET_NONE;t->x=p->x;t->z=p->z;t->local_resident=1u;
            odg_local_fx_to_global_fx_internal(t->x,t->z,&t->global_fx_x,&t->global_fx_z);odg_apply_turret_tier(t,ODG_MATERIAL_WOOD,0);
            odg_memset(&p->inventory,0,sizeof(p->inventory));p->inventory.slot_count=ODG_INVENTORY_BASE_SLOTS;
            odg_memset(&chip,0,sizeof(chip));chip.type_id=ODG_ITEM_ASCENSION_CHIP;chip.quantity=1u;chip.material_tier=ODG_MATERIAL_STONE;
            CHECK(odg_inventory_add(&p->inventory,&chip)!=0);CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_ASCENSION_CHIP,ODG_MATERIAL_STONE,&slot)!=0);p->inventory.selected_slot=slot;
            odg_rebuild_interaction_hint();CHECK(odg_copy_interaction_hint(&hint,sizeof(hint),&required)==ODG_STATUS_OK);CHECK(hint.action==ODG_INTERACTION_UPGRADE&&hint.valid!=0u);
            odg_set_input(0,0,0,0,ODG_BUTTON_INTERACT);odg_step_ticks(1u);odg_set_input(0,0,0,0,0u);odg_step_ticks(1u);
            CHECK(t->material_tier==ODG_MATERIAL_STONE);CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_ASCENSION_CHIP,ODG_MATERIAL_STONE)==0u);
            odg_memset(&chip,0,sizeof(chip));chip.type_id=ODG_ITEM_ASCENSION_CHIP;chip.quantity=1u;chip.material_tier=ODG_MATERIAL_IRON;
            CHECK(odg_inventory_add(&p->inventory,&chip)!=0);CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_ASCENSION_CHIP,ODG_MATERIAL_IRON,&slot)!=0);p->inventory.selected_slot=slot;
            odg_rebuild_interaction_hint();CHECK(odg_copy_interaction_hint(&hint,sizeof(hint),&required)==ODG_STATUS_OK);CHECK(hint.action==ODG_INTERACTION_UPGRADE&&hint.valid!=0u);
            odg_set_input(0,0,0,0,ODG_BUTTON_INTERACT);odg_step_ticks(1u);odg_set_input(0,0,0,0,0u);odg_step_ticks(1u);
            CHECK(t->material_tier==ODG_MATERIAL_IRON);CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_ASCENSION_CHIP,ODG_MATERIAL_IRON)==0u);
        }else CHECK(0);
    }

    /* Conquering and physically collecting a natural turret transfers it out of the
     * high-bit procedural namespace. The portable/manual object receives one sequential
     * identity, the worldgen slot is marked removed, SAVE remains canonical, and that
     * exact stateful turret can be deployed again. */
    {
        odg_actor *p;odg_turret *t=NULL;odg_interaction_hint hint;odg_item_stack *carried_stack=NULL;
        uint32_t ti=UINT32_MAX,scan_i,slot=0u,tick;uint64_t required=0u,procedural_id=0u,manual_id=0u,save_required=0u;int64_t tgx,tgz;
        CHECK(odg_init(UINT64_C(0x50524f435f504943),320u,180u)==ODG_STATUS_OK);p=&g_odg.actors[ODG_PLAYER_ID];
        odg_inventory_init(&p->inventory);odg_turrets_stream_refresh();
        for(scan_i=0u;scan_i<g_odg.turret_count;++scan_i){
            if(g_odg_turrets[scan_i].active&&g_odg_turrets[scan_i].procedural!=0u&&g_odg_turrets[scan_i].local_resident!=0u){ti=scan_i;break;}
        }
        CHECK(ti!=UINT32_MAX);
        if(ti!=UINT32_MAX){
            t=&g_odg_turrets[ti];procedural_id=t->instance_id;CHECK((procedural_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)!=0u);
            t->owner=ODG_OWNER_FROM_ID(ODG_PLAYER_ID);odg_local_fx_to_global_cell_internal(t->x,t->z,&tgx,&tgz);
            odg_chunk_set_owner_at_global_cell(tgx,tgz,ODG_OWNER_FROM_ID(ODG_PLAYER_ID));
            p->x=t->x-2*ODG_FX_ONE;p->z=t->z;p->local_resident=1u;odg_local_fx_to_global_fx_internal(p->x,p->z,&p->global_fx_x,&p->global_fx_z);
            odg_entities_spatial_mark_dirty();odg_rebuild_interaction_hint();
            CHECK(odg_copy_interaction_hint(&hint,sizeof(hint),&required)==ODG_STATUS_OK);
            CHECK(hint.action==ODG_INTERACTION_PICKUP_ARTIFACT&&hint.target_kind==ODG_INTERACTION_TARGET_TURRET&&hint.target_id==ti&&hint.valid!=0u);
            odg_set_input(0,0,0,0,ODG_BUTTON_INTERACT);for(tick=0u;tick<ODG_INTERACT_HOLD_TICKS+2u;++tick)odg_step_ticks(1u);
            odg_set_input(0,0,0,0,0u);odg_step_ticks(1u);t=&g_odg_turrets[ti];
            CHECK(!t->active&&t->procedural==0u&&t->instance_id!=0u&&(t->instance_id&ODG_INSTANCE_ID_PROCEDURAL_BIT)==0u);
            CHECK(t->local_resident==0u&&t->x==0&&t->z==0&&t->global_fx_x==0&&t->global_fx_z==0);
            manual_id=t->instance_id;CHECK(manual_id!=procedural_id);
            CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_TURRET,t->material_tier,&slot)!=0);
            if(slot<odg_inventory_capacity(&p->inventory)){carried_stack=&p->inventory.slots[slot];CHECK(carried_stack->instance_id==manual_id&&carried_stack->payload_id==(uint64_t)ti+UINT64_C(1));}
            CHECK(odg_save_identity_validate_internal()!=0);CHECK(odg_save_write(NULL,0u,&save_required)==ODG_STATUS_BUFFER_TOO_SMALL&&save_required==odg_save_blob_size());
            if(slot<odg_inventory_capacity(&p->inventory)){p->inventory.selected_slot=slot;CHECK(odg_turret_place_selected(ODG_PLAYER_ID)!=0);t=&g_odg_turrets[ti];CHECK(t->active&&t->procedural==0u&&t->instance_id==manual_id);}
        }
    }

    /* Carried turret payloads reserve their dormant slot exactly like artifacts. A fresh
     * turret may reuse only truly free/procedural-sleep slots, never the state backing a
     * carried turret. State survives re-placement and duplicate active handles fail closed. */
    {
        odg_actor *p;odg_turret *carried;odg_item_stack original,fresh,duplicate;uint32_t carried_id,slot=0u;
        uint64_t instance;int64_t pgx,pgz;int32_t dx,dz,cx=0,cz=0;uint32_t ammo=7u,shots=23u;
        CHECK(odg_init(UINT64_C(0x545552525f504159),320u,180u)==ODG_STATUS_OK);p=&g_odg.actors[ODG_PLAYER_ID];
        odg_global_fx_to_global_cell_internal(p->global_fx_x,p->global_fx_z,&pgx,&pgz);
        for(dz=-12;dz<=12;++dz)for(dx=-12;dx<=12;++dx)
            odg_chunk_set_owner_at_global_cell(pgx+(int64_t)dx,pgz+(int64_t)dz,ODG_OWNER_FROM_ID(ODG_PLAYER_ID));
        CHECK(odg_entities_reserve_turrets(g_odg.turret_count+1u)!=0);carried_id=g_odg.turret_count++;
        carried=&g_odg_turrets[carried_id];odg_memset(carried,0,sizeof(*carried));carried->id=carried_id;
        carried->instance_id=odg_next_instance_id();instance=carried->instance_id;carried->owner=ODG_OWNER_FROM_ID(ODG_PLAYER_ID);
        carried->procedural=0u;carried->carried_by=ODG_TURRET_NONE;carried->mode=ODG_TURRET_MODE_HARVEST;
        odg_apply_turret_tier(carried,ODG_MATERIAL_STONE,0);carried->ammo=ammo;carried->shots_fired=shots;carried->active=0u;
        odg_inventory_init(&p->inventory);odg_memset(&original,0,sizeof(original));original.type_id=ODG_ITEM_TURRET;original.quantity=1u;
        original.material_tier=ODG_MATERIAL_STONE;original.flags=ODG_ITEM_FLAG_ARTIFACT;original.instance_id=instance;original.payload_id=(uint64_t)carried_id+UINT64_C(1);
        CHECK(odg_item_stack_normalize_internal(&original)!=0);CHECK(odg_inventory_add(&p->inventory,&original)!=0);
        odg_memset(&fresh,0,sizeof(fresh));fresh.type_id=ODG_ITEM_TURRET;fresh.quantity=1u;fresh.material_tier=ODG_MATERIAL_WOOD;
        CHECK(odg_item_stack_normalize_internal(&fresh)!=0);CHECK(odg_inventory_add(&p->inventory,&fresh)!=0);
        CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_TURRET,ODG_MATERIAL_WOOD,&slot)!=0);p->inventory.selected_slot=slot;
        CHECK(odg_turret_place_selected(ODG_PLAYER_ID)!=0);
        carried=&g_odg_turrets[carried_id];CHECK(!carried->active&&carried->procedural==0u&&carried->instance_id==instance);
        CHECK(carried->material_tier==ODG_MATERIAL_STONE&&carried->ammo==ammo&&carried->shots_fired==shots&&carried->mode==ODG_TURRET_MODE_HARVEST);
        /* Remove the fresh fixture turret from the local collision field; the assertion
         * above already proved it did not reuse the reserved carried slot. */
        for(uint32_t fixture_i=0u;fixture_i<g_odg.turret_count;++fixture_i){
            if(fixture_i!=carried_id&&g_odg_turrets[fixture_i].active&&g_odg_turrets[fixture_i].procedural==0u&&
               g_odg_turrets[fixture_i].material_tier==ODG_MATERIAL_WOOD){
                odg_memset(&g_odg_turrets[fixture_i],0,sizeof(g_odg_turrets[fixture_i]));
                g_odg_turrets[fixture_i].id=fixture_i;break;
            }
        }
        odg_entities_spatial_mark_dirty();
        CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_TURRET,ODG_MATERIAL_STONE,&slot)!=0);p->inventory.selected_slot=slot;
        {
            odg_item_stack saved_handle=p->inventory.slots[slot];uint64_t invalid_required=0u;
            odg_memset(&p->inventory.slots[slot],0,sizeof(p->inventory.slots[slot]));
            CHECK(odg_save_identity_validate_internal()==0);
            CHECK(odg_save_write(NULL,0u,&invalid_required)==ODG_STATUS_INVALID_STATE);
            p->inventory.slots[slot]=saved_handle;
            CHECK(odg_save_identity_validate_internal()!=0);
        }
        CHECK(odg_turret_place_selected(ODG_PLAYER_ID)!=0);carried=&g_odg_turrets[carried_id];
        CHECK(carried->active&&carried->instance_id==instance&&carried->ammo==ammo&&carried->shots_fired==shots&&carried->mode==ODG_TURRET_MODE_HARVEST);
        /* Keep the payload target active but move its collider away, so a valid drop
         * candidate definitely exists and the duplicate must fail for identity reasons. */
        carried->x=p->x+30*ODG_FX_ONE;carried->z=p->z;odg_local_fx_to_global_fx_internal(carried->x,carried->z,&carried->global_fx_x,&carried->global_fx_z);
        odg_entities_spatial_mark_dirty();
        CHECK(odg_save_identity_validate_internal()!=0);
        duplicate=original;CHECK(odg_inventory_add(&p->inventory,&duplicate)!=0);
        CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_TURRET,ODG_MATERIAL_STONE,&slot)!=0);p->inventory.selected_slot=slot;
        CHECK(odg_turret_drop_candidate_internal(p,&cx,&cz)!=0);CHECK(odg_turret_place_selected(ODG_PLAYER_ID)==0);
        CHECK(p->inventory.slots[slot].quantity==1u&&g_odg_turrets[carried_id].active&&g_odg_turrets[carried_id].instance_id==instance);
        CHECK(odg_save_identity_validate_internal()==0);
        {uint64_t invalid_required=0u;CHECK(odg_save_write(NULL,0u,&invalid_required)==ODG_STATUS_INVALID_STATE);}
    }

    /* Artifact placement is transactional and payload identity is exclusive. A picked-up
     * chest keeps its inactive slot reserved; placing a fresh torch must not overwrite
     * that state. Re-placing the chest reactivates the same instance, while a duplicate
     * payload that points at the now-active artifact is rejected without consumption. */
    {
        odg_actor *p;odg_artifact *chest;odg_item_stack fresh,duplicate;odg_interaction_hint hold;
        uint32_t chest_id,slot=0u,before_count,after_fresh;uint64_t chest_instance;int64_t pgx,pgz;int32_t dx,dz;
        CHECK(odg_init(UINT64_C(0x415254495f54584e),320u,180u)==ODG_STATUS_OK);
        p=&g_odg.actors[ODG_PLAYER_ID];
        odg_global_fx_to_global_cell_internal(p->global_fx_x,p->global_fx_z,&pgx,&pgz);
        for(dz=-12;dz<=12;++dz)for(dx=-12;dx<=12;++dx)
            odg_chunk_set_owner_at_global_cell(pgx+(int64_t)dx,pgz+(int64_t)dz,ODG_OWNER_FROM_ID(ODG_PLAYER_ID));
        CHECK(odg_entities_reserve_artifacts(g_odg.artifact_count+1u)!=0);
        chest_id=g_odg.artifact_count++;chest=&g_odg_artifacts[chest_id];odg_memset(chest,0,sizeof(*chest));
        chest->active=1u;chest->id=chest_id;chest->instance_id=odg_next_instance_id();chest_instance=chest->instance_id;
        chest->item_type=ODG_ITEM_CHEST;chest->owner_actor_id=ODG_PLAYER_ID;chest->material_tier=ODG_MATERIAL_WOOD;
        chest->capability_bits=ODG_ARTIFACT_CAP_OPEN_UI|ODG_ARTIFACT_CAP_MOVE|ODG_ARTIFACT_CAP_STORE;
        chest->state=ODG_ARTIFACT_STATE_PROTECTED;chest->x=p->x;chest->z=p->z;chest->local_resident=1u;
        odg_local_fx_to_global_fx_internal(chest->x,chest->z,&chest->global_fx_x,&chest->global_fx_z);odg_entities_spatial_mark_dirty();
        odg_memset(&p->inventory,0,sizeof(p->inventory));p->inventory.slot_count=ODG_INVENTORY_BASE_SLOTS;
        odg_memset(&hold,0,sizeof(hold));hold.action=ODG_INTERACTION_OPEN_ARTIFACT;hold.target_id=chest_id;hold.valid=1u;
        CHECK(odg_artifact_execute_hold(ODG_PLAYER_ID,&hold)!=0);CHECK(chest->active==0u&&chest->instance_id==chest_instance);
        CHECK(chest->local_resident==0u&&chest->x==0&&chest->z==0&&chest->global_fx_x==0&&chest->global_fx_z==0);
        CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_CHEST,ODG_MATERIAL_WOOD,&slot)!=0);
        CHECK(p->inventory.slots[slot].payload_id==(uint64_t)chest_id+UINT64_C(1));
        {
            odg_item_stack saved_handle=p->inventory.slots[slot];uint64_t invalid_required=0u;
            odg_memset(&p->inventory.slots[slot],0,sizeof(p->inventory.slots[slot]));
            CHECK(odg_save_identity_validate_internal()==0);
            CHECK(odg_save_write(NULL,0u,&invalid_required)==ODG_STATUS_INVALID_STATE);
            p->inventory.slots[slot]=saved_handle;
            CHECK(odg_save_identity_validate_internal()!=0);
        }
        {uint64_t required=0u;uint32_t caps=chest->capability_bits,material=chest->material_tier;
         CHECK(odg_save_write(NULL,0u,&required)==ODG_STATUS_BUFFER_TOO_SMALL);
         chest->x=ODG_FX_ONE;CHECK(odg_save_write(NULL,0u,&required)==ODG_STATUS_INVALID_STATE);chest->x=0;
         chest->capability_bits^=ODG_ARTIFACT_CAP_LIGHT;CHECK(odg_save_write(NULL,0u,&required)==ODG_STATUS_INVALID_STATE);chest->capability_bits=caps;
         chest->material_tier=ODG_MATERIAL_IRON;CHECK(odg_save_write(NULL,0u,&required)==ODG_STATUS_INVALID_STATE);chest->material_tier=material;
         chest->aux_u32=1u;CHECK(odg_save_write(NULL,0u,&required)==ODG_STATUS_INVALID_STATE);chest->aux_u32=0u;
         CHECK(odg_save_write(NULL,0u,&required)==ODG_STATUS_BUFFER_TOO_SMALL);}

        odg_memset(&fresh,0,sizeof(fresh));fresh.type_id=ODG_ITEM_TORCH;fresh.quantity=1u;fresh.material_tier=ODG_MATERIAL_WOOD;
        CHECK(odg_item_stack_normalize_internal(&fresh)!=0);CHECK(odg_inventory_add(&p->inventory,&fresh)!=0);
        CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_TORCH,ODG_MATERIAL_WOOD,&slot)!=0);p->inventory.selected_slot=slot;
        before_count=g_odg.artifact_count;CHECK(odg_artifact_place_selected(ODG_PLAYER_ID)!=0);after_fresh=g_odg.artifact_count;
        CHECK(after_fresh==before_count+1u);CHECK(!chest->active&&chest->instance_id==chest_instance&&(chest->state&ODG_ARTIFACT_STATE_PROTECTED)!=0u);

        CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_CHEST,ODG_MATERIAL_WOOD,&slot)!=0);p->inventory.selected_slot=slot;
        CHECK(odg_artifact_place_selected(ODG_PLAYER_ID)!=0);CHECK(g_odg.artifact_count==after_fresh);
        CHECK(chest->active&&chest->instance_id==chest_instance&&(chest->state&ODG_ARTIFACT_STATE_PROTECTED)!=0u);
        odg_memset(&duplicate,0,sizeof(duplicate));duplicate.type_id=ODG_ITEM_CHEST;duplicate.quantity=1u;duplicate.material_tier=ODG_MATERIAL_WOOD;
        duplicate.flags=ODG_ITEM_FLAG_ARTIFACT;duplicate.instance_id=chest_instance;duplicate.payload_id=(uint64_t)chest_id+UINT64_C(1);
        CHECK(odg_item_stack_normalize_internal(&duplicate)!=0);CHECK(odg_inventory_add(&p->inventory,&duplicate)!=0);
        CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_CHEST,ODG_MATERIAL_WOOD,&slot)!=0);p->inventory.selected_slot=slot;
        before_count=g_odg.artifact_count;CHECK(odg_save_identity_validate_internal()==0);
        CHECK(odg_artifact_place_selected(ODG_PLAYER_ID)==0);CHECK(g_odg.artifact_count==before_count);
        CHECK(p->inventory.slots[slot].quantity==1u&&chest->active&&chest->instance_id==chest_instance&&(chest->state&ODG_ARTIFACT_STATE_PROTECTED)!=0u);
        {uint64_t invalid_required=0u;CHECK(odg_save_write(NULL,0u,&invalid_required)==ODG_STATUS_INVALID_STATE);}
    }

    /* Construction cycle: place a material-preserving block on owned land. An enemy
     * cannot dismantle it while the builder controls the cell. Neutralizing/conquering
     * that cell changes the authorization, and dismantling recovers the same module. */
    {
        odg_actor *builder=&g_odg.actors[ODG_PLAYER_ID];odg_actor *raider=&g_odg.actors[1u];
        odg_item_stack block;odg_interaction_hint hint;uint32_t slot=UINT32_MAX,block_id=UINT32_MAX;
        int32_t bx=0,bz=0;int64_t gx,gz;uint32_t before_artifacts,before_construction;
        CHECK(find_placeable_actor_position(ODG_PLAYER_ID,&bx,&bz)!=0);place_actor(builder,bx,bz);
        odg_memset(&builder->inventory,0,sizeof(builder->inventory));builder->inventory.slot_count=ODG_INVENTORY_BASE_SLOTS;
        odg_memset(&block,0,sizeof(block));block.type_id=ODG_ITEM_BUILDING_BLOCK;block.quantity=1u;block.material_tier=ODG_MATERIAL_STONE;
        CHECK(odg_inventory_add(&builder->inventory,&block)!=0);
        CHECK(odg_inventory_find_type(&builder->inventory,ODG_ITEM_BUILDING_BLOCK,ODG_MATERIAL_STONE,&slot)!=0);
        builder->inventory.selected_slot=slot;before_artifacts=g_odg.artifact_count;before_construction=g_odg_construction_count;
        CHECK(odg_construction_place_selected_internal(ODG_PLAYER_ID)!=0);
        CHECK(g_odg.artifact_count==before_artifacts&&g_odg_construction_count==before_construction+1u);
        block_id=g_odg_construction_count-1u;
        {
            odg_construction_block *placed=&g_odg_construction_blocks[block_id];
            CHECK(placed->active!=0u&&placed->material_tier==ODG_MATERIAL_STONE&&placed->shape==ODG_CONSTRUCTION_SHAPE_WALL);
            raider->active=1u;raider->hp=raider->max_hp;odg_memset(&raider->inventory,0,sizeof(raider->inventory));raider->inventory.slot_count=ODG_INVENTORY_BASE_SLOTS;
            place_actor(raider,placed->x,placed->z);
            odg_memset(&hint,0,sizeof(hint));CHECK(odg_construction_build_hint_internal(raider,NULL,&hint)!=0);
            CHECK(hint.target_id==block_id&&hint.valid==0u&&hint.threshold_ticks>(uint32_t)ODG_INTERACT_HOLD_TICKS);
            odg_local_fx_to_global_cell_internal(placed->x,placed->z,&gx,&gz);
            odg_chunk_set_owner_at_global_cell(gx,gz,ODG_OWNER_NONE);
            odg_memset(&hint,0,sizeof(hint));CHECK(odg_construction_build_hint_internal(raider,NULL,&hint)!=0);
            CHECK(hint.target_id==block_id&&hint.valid!=0u&&hint.requires_hold!=0u);
            CHECK(odg_construction_execute_hold_internal(1u,&hint)!=0);
            CHECK(g_odg_construction_count==before_construction);
            CHECK(odg_inventory_total(&raider->inventory,ODG_ITEM_BUILDING_BLOCK,ODG_MATERIAL_STONE)==1u);
            CHECK(raider->inventory.slots[0].payload_id==0u);
        }
    }



    /* Water vehicle cycle: a raft is crafted, can only be placed in navigable owned
     * water, carries one actor physically above the water (oxygen recovers), respects
     * water/obstacle collision while moving, then dismounts and returns as the same item. */
    {
        odg_actor *p=&g_odg.actors[ODG_PLAYER_ID];odg_item_stack raft_stack;odg_interaction_hint hint;
        odg_recipe_definition raft_recipe;uint64_t required=0u;uint32_t slot=UINT32_MAX,raft_id=UINT32_MAX;
        int32_t target_x=0,target_z=0,old_x=0,old_z=0;uint32_t before_oxygen,moved=0u;
        static const int32_t moves[][2]={{ODG_FX_ONE/8,0},{-ODG_FX_ONE/8,0},{0,ODG_FX_ONE/8},{0,-ODG_FX_ONE/8}};
        CHECK(prepare_raft_site(ODG_PLAYER_ID,&target_x,&target_z)!=0);
        odg_memset(&p->inventory,0,sizeof(p->inventory));p->inventory.slot_count=ODG_INVENTORY_BASE_SLOTS;
        odg_memset(&raft_stack,0,sizeof(raft_stack));raft_stack.type_id=ODG_ITEM_RAFT;raft_stack.quantity=1u;raft_stack.material_tier=ODG_MATERIAL_WOOD;
        CHECK(odg_inventory_add(&p->inventory,&raft_stack)!=0);CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_RAFT,ODG_MATERIAL_WOOD,&slot)!=0);
        p->inventory.selected_slot=slot;CHECK(odg_artifact_place_selected(ODG_PLAYER_ID)!=0);
        for(i=0u;i<g_odg.artifact_count;++i)if(g_odg_artifacts[i].active&&g_odg_artifacts[i].item_type==ODG_ITEM_RAFT){raft_id=i;break;}
        CHECK(raft_id!=UINT32_MAX);
        if(raft_id!=UINT32_MAX){
            odg_artifact *raft=&g_odg_artifacts[raft_id];odg_surface_sample water;odg_actor bot_backup=g_odg.actors[1u];
            CHECK((raft->capability_bits&ODG_ARTIFACT_CAP_VEHICLE)!=0u);
            CHECK(odg_environment_surface_local(raft->x,raft->z,&water)!=0&&(water.flags&ODG_SURFACE_FLAG_WATER)!=0u&&
                  odg_artifact_surface_allows_item_internal(ODG_ITEM_RAFT,raft->x,raft->z)!=0);
            place_actor(p,raft->x,raft->z);odg_memset(&hint,0,sizeof(hint));CHECK(odg_artifact_build_hint(p,NULL,&hint)!=0);
            CHECK(hint.action==ODG_INTERACTION_USE_VEHICLE&&hint.target_id==raft_id&&hint.valid!=0u);
            CHECK(odg_artifact_execute_tap(ODG_PLAYER_ID,&hint)!=0);CHECK(odg_artifact_actor_vehicle_internal(ODG_PLAYER_ID)==raft_id);
            /* Dynamic bodies are physical obstacles to the vehicle, not just to its
             * placement. Find a direction that is otherwise legal, occupy its target
             * with a bot, and prove the exact move becomes illegal until the body leaves. */
            {
                uint32_t mi;int found_move=0;
                for(mi=0u;mi<(uint32_t)(sizeof(moves)/sizeof(moves[0]));++mi){
                    if(!odg_artifact_vehicle_can_move_actor_internal(p,moves[mi][0],moves[mi][1]))continue;
                    found_move=1;place_actor(&g_odg.actors[1u],raft->x+moves[mi][0],raft->z+moves[mi][1]);
                    CHECK(odg_artifact_vehicle_can_move_actor_internal(p,moves[mi][0],moves[mi][1])==0);
                    g_odg.actors[1u]=bot_backup;odg_entities_spatial_mark_dirty();
                    CHECK(odg_artifact_vehicle_can_move_actor_internal(p,moves[mi][0],moves[mi][1])!=0);break;
                }
                CHECK(found_move!=0);
            }
            /* Record one valid dismount, remount, then occupy that exact point. The
             * second dismount may choose a different safe candidate or refuse, but it
             * must never place the rider inside the blocking body. */
            {
                int32_t first_dx=0,first_dz=0;int second_result;
                CHECK(odg_artifact_vehicle_toggle_internal(ODG_PLAYER_ID,raft_id)!=0);
                CHECK(odg_artifact_actor_vehicle_internal(ODG_PLAYER_ID)==UINT32_MAX);
                first_dx=p->x;first_dz=p->z;place_actor(p,raft->x,raft->z);
                CHECK(odg_artifact_vehicle_toggle_internal(ODG_PLAYER_ID,raft_id)!=0);
                CHECK(odg_artifact_actor_vehicle_internal(ODG_PLAYER_ID)==raft_id);
                place_actor(&g_odg.actors[1u],first_dx,first_dz);
                second_result=odg_artifact_vehicle_toggle_internal(ODG_PLAYER_ID,raft_id);
                if(second_result){CHECK(p->x!=first_dx||p->z!=first_dz);}
                else CHECK(odg_artifact_actor_vehicle_internal(ODG_PLAYER_ID)==raft_id);
                g_odg.actors[1u]=bot_backup;odg_entities_spatial_mark_dirty();
                if(odg_artifact_actor_vehicle_internal(ODG_PLAYER_ID)==UINT32_MAX){
                    place_actor(p,raft->x,raft->z);CHECK(odg_artifact_vehicle_toggle_internal(ODG_PLAYER_ID,raft_id)!=0);
                }
                CHECK(odg_artifact_actor_vehicle_internal(ODG_PLAYER_ID)==raft_id);
            }
            /* A mounted vehicle is a persisted cross-reference, not merely two nearby
             * coordinates. Save requires exactly one rider and exact actor<->vehicle
             * position/state agreement; moving only the actor must fail closed. */
            {
                odg_actor mounted=*p;uint64_t save_required=0u;
                CHECK(odg_artifact_cross_reference_validate_internal()!=0);
                CHECK(odg_save_write(NULL,0u,&save_required)==ODG_STATUS_BUFFER_TOO_SMALL);
                place_actor(p,mounted.x+ODG_FX_ONE,mounted.z);
                CHECK(odg_artifact_cross_reference_validate_internal()==0);
                CHECK(odg_save_write(NULL,0u,&save_required)==ODG_STATUS_INVALID_STATE);
                *p=mounted;odg_entities_spatial_mark_dirty();CHECK(odg_artifact_cross_reference_validate_internal()!=0);
            }
            CHECK(odg_actor_is_swimming_internal(p)==0);g_odg_persistent_runtime.actors[ODG_PLAYER_ID].oxygen_permille=500u;
            before_oxygen=odg_player_oxygen_permille();odg_survival_tick();CHECK(odg_player_oxygen_permille()>before_oxygen);
            old_x=raft->x;old_z=raft->z;
            for(i=0u;i<(uint32_t)(sizeof(moves)/sizeof(moves[0]));++i){
                if(odg_artifact_vehicle_move_actor_internal(p,moves[i][0],moves[i][1])){moved=1u;break;}
            }
            CHECK(moved!=0u);CHECK(raft->x!=old_x||raft->z!=old_z);CHECK(p->x==raft->x&&p->z==raft->z);
            CHECK(odg_environment_surface_local(raft->x,raft->z,&water)!=0&&(water.flags&ODG_SURFACE_FLAG_WATER)!=0u);
            hint.action=ODG_INTERACTION_USE_VEHICLE;hint.target_id=raft_id;hint.valid=1u;CHECK(odg_artifact_execute_tap(ODG_PLAYER_ID,&hint)!=0);
            CHECK(odg_artifact_actor_vehicle_internal(ODG_PLAYER_ID)==UINT32_MAX&&raft->aux_u32==0u);
            CHECK(odg_artifact_execute_hold(ODG_PLAYER_ID,&hint)!=0);CHECK(!raft->active);
            CHECK(odg_inventory_total(&p->inventory,ODG_ITEM_RAFT,ODG_MATERIAL_WOOD)==1u);
        }
        CHECK(odg_recipe_get(ODG_RECIPE_RAFT,&raft_recipe,sizeof(raft_recipe),&required)==ODG_STATUS_OK);
        CHECK(raft_recipe.station_item_type==ODG_STATION_WORKBENCH&&raft_recipe.output_item_type==ODG_ITEM_RAFT&&
              raft_recipe.ingredients[0].item_type==ODG_ITEM_WOOD&&raft_recipe.ingredients[0].quantity==16u);
    }

    /* Neutral coastal turrets use a strict majority of surrounding ground, not a fake
     * 25-cell denominator that counts ocean. This fixture deliberately chooses a coast
     * where the minimum winning land vote is <=12: old behavior cannot capture it, while
     * the canonical surface-aware rule can. Once commissioned, paint alone never flips it. */
    {
        int64_t center_gx=0,center_gz=0;uint32_t land=0u,needed=0u,assigned=0u;
        int32_t lx=0,lz=0,dz,dx;uint32_t ti=g_odg.turret_count;odg_turret *t=NULL;
        CHECK(find_coastal_turret_site(&center_gx,&center_gz,&land)!=0);
        CHECK(land>0u&&land<25u);needed=land/2u+1u;CHECK(needed<=12u);
        CHECK(odg_entities_reserve_turrets(ti+1u)!=0);
        if(g_odg_turret_capacity>ti&&odg_global_cell_center_to_local_fx_internal(center_gx,center_gz,&lx,&lz)){
            t=&g_odg_turrets[ti];odg_memset(t,0,sizeof(*t));g_odg.turret_count=ti+1u;
            t->active=1u;t->id=ti;t->instance_id=odg_next_instance_id();t->owner=ODG_TURRET_NEUTRAL;
            t->carried_by=ODG_TURRET_NONE;t->x=lx;t->z=lz;t->local_resident=1u;
            odg_local_fx_to_global_fx_internal(lx,lz,&t->global_fx_x,&t->global_fx_z);
            odg_apply_turret_tier(t,ODG_MATERIAL_WOOD,0);
            for(dz=-ODG_TURRET_CAPTURE_RADIUS;dz<=ODG_TURRET_CAPTURE_RADIUS;++dz)
                for(dx=-ODG_TURRET_CAPTURE_RADIUS;dx<=ODG_TURRET_CAPTURE_RADIUS;++dx){
                    int64_t gx=center_gx+(int64_t)dx,gz=center_gz+(int64_t)dz;
                    odg_surface_sample sample;uint64_t required=0u;
                    odg_chunk_set_owner_at_global_cell(gx,gz,ODG_OWNER_NONE);
                    if(assigned>=needed)continue;
                    if(odg_world_surface_sample64(gx,gz,&sample,sizeof(sample),&required)!=ODG_STATUS_OK)continue;
                    if((sample.flags&ODG_SURFACE_FLAG_WATER)!=0u)continue;
                    odg_chunk_set_owner_at_global_cell(gx,gz,ODG_OWNER_FROM_ID(ODG_PLAYER_ID));++assigned;
                }
            CHECK(assigned==needed);
            g_odg.actors[ODG_PLAYER_ID].active=1u;g_odg.actors[ODG_PLAYER_ID].hp=g_odg.actors[ODG_PLAYER_ID].max_hp;
            odg_update_turret_ownership_internal();CHECK(t->owner==ODG_OWNER_FROM_ID(ODG_PLAYER_ID));
            for(dz=-ODG_TURRET_CAPTURE_RADIUS;dz<=ODG_TURRET_CAPTURE_RADIUS;++dz)
                for(dx=-ODG_TURRET_CAPTURE_RADIUS;dx<=ODG_TURRET_CAPTURE_RADIUS;++dx)
                    odg_chunk_set_owner_at_global_cell(center_gx+(int64_t)dx,center_gz+(int64_t)dz,ODG_OWNER_NONE);
            odg_update_turret_ownership_internal();CHECK(t->owner==ODG_OWNER_FROM_ID(ODG_PLAYER_ID));
        }else CHECK(0);
    }

    /* Procedural turret descriptors are only an intent. The resolved physical cell must
     * always be dry, non-steep ground; ocean-only chunks may legitimately resolve to no
     * turret instead of materializing an impossible collider. Scan well beyond the active
     * window so this guards pure deterministic worldgen, not just streamed entities. */
    {
        int64_t center_gx=odg_global_center_cell_x_internal(),center_gz=odg_global_center_cell_z_internal();
        int64_t center_cx=odg_floor_div_i64_internal(center_gx,(int64_t)ODG_CHUNK_SIZE_CELLS);
        int64_t center_cz=odg_floor_div_i64_internal(center_gz,(int64_t)ODG_CHUNK_SIZE_CELLS);
        int32_t oz,ox;uint32_t resolved=0u;
        for(oz=-20;oz<=20;++oz)for(ox=-20;ox<=20;++ox){
            int64_t gx,gz;
            if(!odg_chunk_procedural_turret_cell(center_cx+(int64_t)ox,center_cz+(int64_t)oz,&gx,&gz))continue;
            CHECK(odg_world_cell_safe_ground_internal(gx,gz)!=0);++resolved;
        }
        CHECK(resolved>100u);
    }

    /* WORLDGEN3 is a real, versioned behavior delta rather than a renamed constant. Find
     * a chunk whose frozen v2 hash lands on invalid ground but whose interior has a valid
     * fallback, then prove v2 keeps that cell while v3 resolves the same chunk safely. */
    {
        uint32_t saved_worldgen=g_odg_persistent_runtime.worldgen_version;int found=0;int32_t oz,ox;
        int64_t center_gx=odg_global_center_cell_x_internal(),center_gz=odg_global_center_cell_z_internal();
        int64_t center_cx=odg_floor_div_i64_internal(center_gx,(int64_t)ODG_CHUNK_SIZE_CELLS);
        int64_t center_cz=odg_floor_div_i64_internal(center_gz,(int64_t)ODG_CHUNK_SIZE_CELLS);
        int64_t v2_gx=0,v2_gz=0,v3_gx=0,v3_gz=0;
        for(oz=-40;oz<=40&&!found;++oz)for(ox=-40;ox<=40&&!found;++ox){
            int64_t old_gx,old_gz,new_gx,new_gz,cx=center_cx+(int64_t)ox,cz=center_cz+(int64_t)oz;
            g_odg_persistent_runtime.worldgen_version=ODG_WORLDGEN_VERSION_BATHYMETRY;
            if(!odg_chunk_procedural_turret_cell(cx,cz,&old_gx,&old_gz) ||
               odg_world_cell_safe_ground_internal(old_gx,old_gz)!=0)continue;
            g_odg_persistent_runtime.worldgen_version=ODG_WORLDGEN_VERSION_CURRENT;
            if(!odg_chunk_procedural_turret_cell(cx,cz,&new_gx,&new_gz) ||
               odg_world_cell_safe_ground_internal(new_gx,new_gz)==0)continue;
            v2_gx=old_gx;v2_gz=old_gz;v3_gx=new_gx;v3_gz=new_gz;found=1;
        }
        CHECK(found!=0);
        if(found){CHECK(v3_gx!=v2_gx||v3_gz!=v2_gz);}
        g_odg_persistent_runtime.worldgen_version=saved_worldgen;
    }

    /* Sleeping natural infrastructure must never pop through a dynamic body. Force one
     * resident procedural turret dormant, occupy its exact cell with a bot, and prove
     * streaming defers. Once the bot moves away it materializes; permanent dismantling
     * then releases both the entity and its pre-stream reservation. */
    {
        uint32_t ti,target=UINT32_MAX,scan_i;odg_actor bot_backup=g_odg.actors[1u];odg_turret snapshot;
        int64_t source_cx=0,source_cz=0;int32_t tx=0,tz=0;
        odg_turrets_stream_refresh();
        for(ti=0u;ti<g_odg.turret_count;++ti){
            if(g_odg_turrets[ti].active&&g_odg_turrets[ti].procedural!=0u&&g_odg_turrets[ti].local_resident!=0u){target=ti;break;}
        }
        CHECK(target!=UINT32_MAX);
        if(target!=UINT32_MAX){
            odg_actor *bot=&g_odg.actors[1u];uint32_t tombstone_id=target;uint64_t save_required=0u;
            /* Give the natural turret non-default persisted state so the test proves the
             * chunk override, rather than a stale dynamic record, is what wakes it. */
            g_odg_turrets[target].owner=ODG_OWNER_FROM_ID(ODG_PLAYER_ID);
            odg_apply_turret_tier(&g_odg_turrets[target],ODG_MATERIAL_IRON,0);
            g_odg_turrets[target].ammo=7u;g_odg_turrets[target].shots_fired=19u;
            snapshot=g_odg_turrets[target];source_cx=snapshot.source_chunk_x;source_cz=snapshot.source_chunk_z;tx=snapshot.x;tz=snapshot.z;
            odg_turret_persist_procedural(&g_odg_turrets[target]);
            odg_memset(&g_odg_turrets[target],0,sizeof(g_odg_turrets[target]));g_odg_turrets[target].id=tombstone_id;
            CHECK(g_odg_turrets[target].instance_id==0u&&g_odg_turrets[target].procedural==0u&&g_odg_turrets[target].local_resident==0u);
            CHECK(odg_save_write(NULL,0u,&save_required)==ODG_STATUS_BUFFER_TOO_SMALL);
            bot->active=1u;bot->hp=bot->max_hp!=0u?bot->max_hp:100u;bot->local_resident=1u;bot->x=tx;bot->z=tz;
            bot->global_fx_x=snapshot.global_fx_x;bot->global_fx_z=snapshot.global_fx_z;
            odg_turrets_stream_refresh();
            for(scan_i=0u;scan_i<g_odg.turret_count;++scan_i)CHECK(!(g_odg_turrets[scan_i].active&&g_odg_turrets[scan_i].procedural!=0u&&g_odg_turrets[scan_i].source_chunk_x==source_cx&&g_odg_turrets[scan_i].source_chunk_z==source_cz));
            bot->x=tx+4*ODG_FX_ONE;bot->z=tz;odg_local_fx_to_global_fx_internal(bot->x,bot->z,&bot->global_fx_x,&bot->global_fx_z);
            odg_turrets_stream_refresh();target=UINT32_MAX;
            for(scan_i=0u;scan_i<g_odg.turret_count;++scan_i)if(g_odg_turrets[scan_i].active&&g_odg_turrets[scan_i].procedural!=0u&&g_odg_turrets[scan_i].source_chunk_x==source_cx&&g_odg_turrets[scan_i].source_chunk_z==source_cz){target=scan_i;break;}
            CHECK(target!=UINT32_MAX);
            if(target!=UINT32_MAX){
                uint32_t removed_id=target;odg_turret *restored=&g_odg_turrets[target];
                CHECK(restored->owner==snapshot.owner&&restored->material_tier==snapshot.material_tier&&
                      restored->ammo==snapshot.ammo&&restored->max_ammo==snapshot.max_ammo&&
                      restored->fire_period==snapshot.fire_period&&restored->aim_required==snapshot.aim_required&&
                      restored->range_fx==snapshot.range_fx&&restored->shots_fired==snapshot.shots_fired);
                odg_chunk_mark_procedural_turret_removed(source_cx,source_cz);
                odg_memset(restored,0,sizeof(*restored));restored->id=removed_id;
                odg_turrets_stream_refresh();
                for(scan_i=0u;scan_i<g_odg.turret_count;++scan_i)CHECK(!(g_odg_turrets[scan_i].active&&g_odg_turrets[scan_i].procedural!=0u&&g_odg_turrets[scan_i].source_chunk_x==source_cx&&g_odg_turrets[scan_i].source_chunk_z==source_cz));
                CHECK(odg_chunk_procedural_turret_reserves_local_circle_internal(tx,tz,0)==0);
            }
            g_odg.actors[1u]=bot_backup;
        }
    }

    /* Procedural resource depletion has a 64-bit persistent mask per chunk. Guard both
     * sides of that contract: current descriptor densities must never allocate ordinal 64+,
     * and a real harvested streamed resource must stay gone after unload/reload. */
    {
        int64_t center_gx=odg_global_center_cell_x_internal(),center_gz=odg_global_center_cell_z_internal();
        int64_t center_cx=odg_floor_div_i64_internal(center_gx,(int64_t)ODG_CHUNK_SIZE_CELLS);
        int64_t center_cz=odg_floor_div_i64_internal(center_gz,(int64_t)ODG_CHUNK_SIZE_CELLS);
        int32_t oz,ox;uint32_t max_ordinals=0u;
        for(oz=-20;oz<=20;++oz)for(ox=-20;ox<=20;++ox){
            odg_chunk_descriptor d;uint64_t required=0u;uint32_t total;
            int64_t cx=center_cx+(int64_t)ox,cz=center_cz+(int64_t)oz;
            CHECK(odg_chunk_descriptor_get(cx,cz,&d,sizeof(d),&required)==ODG_STATUS_OK);
            total=d.tree_count+d.stone_count+odg_chunk_coal_candidate_count_internal(cx,cz)+d.iron_count;
            CHECK(total<=64u);if(total>max_ordinals)max_ordinals=total;
        }
        CHECK(max_ordinals>0u&&max_ordinals<64u);
    }
    {
        uint32_t ri,target=UINT32_MAX,ticks=0u,result=0u;odg_actor player_backup=g_odg.actors[ODG_PLAYER_ID];
        uint8_t old_owner=ODG_OWNER_NONE;int64_t gx=0,gz=0;int64_t chunk_x=0,chunk_z=0;uint32_t ordinal=0u;
        int64_t resource_global_x=0,resource_global_z=0;int64_t actor_global_x[ODG_MAX_ACTORS],actor_global_z[ODG_MAX_ACTORS];odg_item_stack *slot;
        odg_resources_stream_refresh();
        for(ri=0u;ri<g_odg.resource_count;++ri){
            const odg_resource_node *r=&g_odg_resources[ri];uint32_t ti;int blocked=0;
            if(!r->active||!r->local_resident||r->procedural==0u||r->kind!=ODG_RESOURCE_TREE)continue;
            for(ti=0u;ti<g_odg.turret_count;++ti){
                const odg_turret *t=&g_odg_turrets[ti];
                if(t->active&&t->mode==ODG_TURRET_MODE_HARVEST&&t->source_chunk_x==r->chunk_x&&t->source_chunk_z==r->chunk_z){blocked=1;break;}
            }
            if(!blocked){target=ri;break;}
        }
        CHECK(target!=UINT32_MAX);
        if(target!=UINT32_MAX){
            odg_resource_node *r=&g_odg_resources[target];const odg_item_definition *axe=odg_item_definition_internal(ODG_ITEM_AXE);
            chunk_x=r->chunk_x;chunk_z=r->chunk_z;ordinal=r->chunk_ordinal;resource_global_x=r->global_fx_x;resource_global_z=r->global_fx_z;
            odg_global_fx_to_global_cell_internal(resource_global_x,resource_global_z,&gx,&gz);old_owner=odg_chunk_owner_at_global_cell(gx,gz);
            place_actor(&g_odg.actors[ODG_PLAYER_ID],r->x,r->z);g_odg.actors[ODG_PLAYER_ID].active=1u;g_odg.actors[ODG_PLAYER_ID].hp=g_odg.actors[ODG_PLAYER_ID].max_hp;
            odg_chunk_set_owner_at_global_cell(gx,gz,ODG_OWNER_FROM_ID(ODG_PLAYER_ID));
            slot=&g_odg.actors[ODG_PLAYER_ID].inventory.slots[0];odg_memset(slot,0,sizeof(*slot));slot->type_id=ODG_ITEM_AXE;slot->quantity=1u;slot->material_tier=ODG_MATERIAL_IRON;
            CHECK(axe!=NULL);
            if(axe!=NULL)slot->flags=axe->flags;
            CHECK(odg_item_stack_normalize_internal(slot)!=0);
            g_odg.actors[ODG_PLAYER_ID].inventory.selected_slot=0u;
            while(ticks++<600u&&result!=2u)result=(uint32_t)odg_resource_hold_tick(ODG_PLAYER_ID,target);
            CHECK(result==2u);CHECK(odg_chunk_resource_depleted(chunk_x,chunk_z,ordinal)!=0);
            /* Unload every actor far from the harvested chunk, then come back. The same
             * deterministic ordinal must not materialize again. */
            for(ri=0u;ri<ODG_MAX_ACTORS;++ri){
                actor_global_x[ri]=g_odg.actors[ri].global_fx_x;actor_global_z[ri]=g_odg.actors[ri].global_fx_z;
                if(!g_odg.actors[ri].active)continue;
                g_odg.actors[ri].global_fx_x=resource_global_x+(int64_t)ODG_CHUNK_SIZE_CELLS*80*(int64_t)ODG_FX_ONE;
                g_odg.actors[ri].global_fx_z=resource_global_z+(int64_t)ODG_CHUNK_SIZE_CELLS*80*(int64_t)ODG_FX_ONE;
            }
            odg_resources_stream_refresh();
            for(ri=0u;ri<g_odg.resource_count;++ri)CHECK(!(g_odg_resources[ri].procedural!=0u&&g_odg_resources[ri].chunk_x==chunk_x&&g_odg_resources[ri].chunk_z==chunk_z&&g_odg_resources[ri].chunk_ordinal==ordinal));
            g_odg.actors[ODG_PLAYER_ID].global_fx_x=resource_global_x;g_odg.actors[ODG_PLAYER_ID].global_fx_z=resource_global_z;
            odg_resources_stream_refresh();
            for(ri=0u;ri<g_odg.resource_count;++ri)CHECK(!(g_odg_resources[ri].procedural!=0u&&g_odg_resources[ri].chunk_x==chunk_x&&g_odg_resources[ri].chunk_z==chunk_z&&g_odg_resources[ri].chunk_ordinal==ordinal));
            for(ri=0u;ri<ODG_MAX_ACTORS;++ri){g_odg.actors[ri].global_fx_x=actor_global_x[ri];g_odg.actors[ri].global_fx_z=actor_global_z[ri];}
            g_odg.actors[ODG_PLAYER_ID]=player_backup;odg_chunk_set_owner_at_global_cell(gx,gz,old_owner);
        }
    }

    /* Stackable deployables are deliberately stateless inventory items. Recovering a
     * torch must collapse its world instance back into a plain stack (payload=0,
     * instance=0), free the artifact slot, remain saveable, and be placeable again.
     * Keeping a per-instance handle here would conflict with max_stack>1 normalization. */
    {
        odg_actor *p;odg_item_stack torch;odg_interaction_hint hold;uint32_t slot=UINT32_MAX,torch_id=UINT32_MAX,scan_i;
        uint64_t required=0u;int64_t pgx,pgz;int32_t dx,dz;
        CHECK(odg_init(UINT64_C(0x535441434b4c4954),320u,180u)==ODG_STATUS_OK);
        p=&g_odg.actors[ODG_PLAYER_ID];odg_global_fx_to_global_cell_internal(p->global_fx_x,p->global_fx_z,&pgx,&pgz);
        for(dz=-12;dz<=12;++dz)for(dx=-12;dx<=12;++dx)
            odg_chunk_set_owner_at_global_cell(pgx+(int64_t)dx,pgz+(int64_t)dz,ODG_OWNER_FROM_ID(ODG_PLAYER_ID));
        odg_inventory_init(&p->inventory);odg_memset(&torch,0,sizeof(torch));torch.type_id=ODG_ITEM_TORCH;torch.quantity=1u;
        torch.material_tier=ODG_MATERIAL_WOOD;CHECK(odg_item_stack_normalize_internal(&torch)!=0);
        CHECK(odg_inventory_add(&p->inventory,&torch)!=0);CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_TORCH,ODG_MATERIAL_WOOD,&slot)!=0);
        p->inventory.selected_slot=slot;CHECK(odg_artifact_place_selected(ODG_PLAYER_ID)!=0);
        for(scan_i=0u;scan_i<g_odg.artifact_count;++scan_i)if(g_odg_artifacts[scan_i].active&&g_odg_artifacts[scan_i].owner_actor_id==ODG_PLAYER_ID&&
           g_odg_artifacts[scan_i].item_type==ODG_ITEM_TORCH){torch_id=scan_i;break;}
        CHECK(torch_id!=UINT32_MAX);
        if(torch_id!=UINT32_MAX){
            odg_memset(&hold,0,sizeof(hold));hold.action=ODG_INTERACTION_OPEN_ARTIFACT;hold.target_id=torch_id;hold.valid=1u;
            CHECK(odg_artifact_execute_hold(ODG_PLAYER_ID,&hold)!=0);
            CHECK(!g_odg_artifacts[torch_id].active&&g_odg_artifacts[torch_id].instance_id==0u);
            CHECK(odg_inventory_find_type(&p->inventory,ODG_ITEM_TORCH,ODG_MATERIAL_WOOD,&slot)!=0);
            CHECK(p->inventory.slots[slot].payload_id==0u&&p->inventory.slots[slot].instance_id==0u);
            CHECK(odg_save_identity_validate_internal()!=0);
            CHECK(odg_save_write(NULL,0u,&required)==ODG_STATUS_BUFFER_TOO_SMALL&&required==odg_save_blob_size());
            p->inventory.selected_slot=slot;CHECK(odg_artifact_place_selected(ODG_PLAYER_ID)!=0);
            CHECK(g_odg_artifacts[torch_id].active&&g_odg_artifacts[torch_id].item_type==ODG_ITEM_TORCH);
        }
    }


        CHECK(odg_artifact_item_deployable_internal(ODG_ITEM_TORCH)!=0);
    CHECK((odg_item_definition_internal(ODG_ITEM_TORCH)->capability_bits&ODG_ITEM_CAP_PLACE)!=0u);

    if(failures!=0){fprintf(stderr,"world systems: %d failure(s)\n",failures);return 2;}
    printf("WORLD SYSTEMS OK geology=caves+coal+iron water=swim+oxygen+fish+croc+raft+body-safe night=stalker torch=ready construction=territory+body-safe turret=coastal-majority+worldgen-v3-safe+stream-defer+global-domain+body-safe+payload-safe+transactional resources=worldgen-v4-canonical+depletion-persistent+stream-defer artifacts=payload-safe+transactional save=identity-guarded ammo=smithy api=%u save=%u\n",
           odg_api_version(),odg_save_schema_version());
    return 0;
}
