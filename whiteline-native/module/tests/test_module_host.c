#include "odpar_module.h"

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); ++failures; } } while (0)

static uint64_t hash_frame(const OdparFrame *f) {
    const uint8_t *p=(const uint8_t *)f->pixels;
    uint64_t h=UINT64_C(1469598103934665603);
    uint32_t i, n=f->stride_bytes*f->height;
    for(i=0;i<n;i+=97u){ h^=p[i]; h*=UINT64_C(1099511628211); }
    return h;
}

static void tap(const OdparModuleApi *api, void *g, int id, float x, float y) {
    OdparEvent e;
    memset(&e,0,sizeof(e)); e.struct_size=sizeof(e); e.pointer_id=id; e.x01=x; e.y01=y;
    e.type=ODPAR_EVENT_POINTER_DOWN; CHECK(api->event(g,&e)==0);
    e.type=ODPAR_EVENT_POINTER_UP; CHECK(api->event(g,&e)==0);
}

int main(int argc,char **argv){
    void *so; OdparModuleGetApiFn get_api; const OdparModuleApi *api; void *game;
    OdparModuleCreateInfo ci; OdparFrame frame; OdparEvent e; uint64_t menu_hash, run_hash; unsigned i;
    if(argc!=2){fprintf(stderr,"usage: %s module.so\n",argv[0]);return 2;}
    so=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); CHECK(so!=NULL); if(!so){fprintf(stderr,"%s\n",dlerror());return 1;}
    *(void **)(&get_api)=dlsym(so,"odpar_module_get_api"); CHECK(get_api!=NULL);
    api=get_api ? get_api(ODPAR_MODULE_ABI):NULL; CHECK(api!=NULL);
    CHECK(get_api(ODPAR_MODULE_ABI+1u)==NULL);
    CHECK(api && strcmp(api->module_id,"odpar.whiteline")==0);
    memset(&ci,0,sizeof(ci)); ci.struct_size=sizeof(ci); ci.view_width=1280; ci.view_height=720; ci.seed=0x574c4431u; ci.density=2.0f;
    game=api->create(&ci); CHECK(game!=NULL);
    memset(&frame,0,sizeof(frame)); frame.struct_size=sizeof(frame); CHECK(api->render(game,&frame)==0); CHECK(frame.pixels!=NULL); CHECK(frame.width==640 && frame.height==360); menu_hash=hash_frame(&frame);
    /* select ENDLESS and PLAY */
    tap(api,game,1,0.40f,0.32f); tap(api,game,2,0.50f,0.82f);
    /* hold GO + steer right for 2 seconds */
    memset(&e,0,sizeof(e)); e.struct_size=sizeof(e); e.pointer_id=10; e.type=ODPAR_EVENT_POINTER_DOWN; e.x01=.91f; e.y01=.84f; CHECK(api->event(game,&e)==0);
    e.pointer_id=11; e.x01=.25f; e.y01=.84f; CHECK(api->event(game,&e)==0);
    for(i=0;i<120;i++) CHECK(api->advance(game,1.0/60.0)==2u);
    memset(&frame,0,sizeof(frame)); frame.struct_size=sizeof(frame); CHECK(api->render(game,&frame)==0); run_hash=hash_frame(&frame); CHECK(run_hash!=menu_hash); CHECK(frame.tick==240u);
    /* Android lifecycle pause/resume must not become a permanent gameplay pause. */
    memset(&e,0,sizeof(e)); e.struct_size=sizeof(e); e.type=ODPAR_EVENT_PAUSE;
    CHECK(api->event(game,&e)==0);
    CHECK(api->advance(game,1.0/60.0)==0u);
    e.type=ODPAR_EVENT_RESUME;
    CHECK(api->event(game,&e)==0);
    CHECK(api->advance(game,1.0/60.0)==2u);

    /* Release controls, toggle camera and AUTO, then verify continued frames. */
    e.type=ODPAR_EVENT_POINTER_UP; e.pointer_id=10; e.x01=.91f; e.y01=.84f; CHECK(api->event(game,&e)==0);
    e.pointer_id=11; e.x01=.25f; e.y01=.84f; CHECK(api->event(game,&e)==0);
    tap(api,game,3,.17f,.24f); tap(api,game,4,.30f,.24f);
    for(i=0;i<30;i++) CHECK(api->advance(game,1.0/60.0)==2u);
    memset(&frame,0,sizeof(frame)); frame.struct_size=sizeof(frame); CHECK(api->render(game,&frame)==0); CHECK(frame.tick==302u);
    /* Back -> pause, back -> resume, back from menu eventually exits. */
    memset(&e,0,sizeof(e)); e.struct_size=sizeof(e); e.type=ODPAR_EVENT_BACK; CHECK(api->event(game,&e)==0);
    CHECK(api->advance(game,1.0/60.0)==0u);
    CHECK(api->event(game,&e)==0); CHECK(api->advance(game,1.0/60.0)==2u);
    CHECK(api->wants_exit(game)==0);
    api->destroy(game);

    /* Every shipped WhiteLine mode must cross the same tiny ODPAR contract.
     * This catches mode-specific initialization/render regressions without
     * rebuilding or involving an Android application layer. */
    for(i=0u;i<4u;++i){
        float mode_y=.32f+.105f*(float)i;
        memset(&ci,0,sizeof(ci));
        ci.struct_size=sizeof(ci); ci.view_width=1280; ci.view_height=720;
        ci.seed=0x574c4431u+i*101u; ci.density=2.0f;
        game=api->create(&ci); CHECK(game!=NULL);
        if(!game) continue;
        tap(api,game,20+(int)i*2,0.40f,mode_y);
        tap(api,game,21+(int)i*2,0.50f,0.82f);
        for(unsigned j=0u;j<30u;++j) CHECK(api->advance(game,1.0/60.0)==2u);
        memset(&frame,0,sizeof(frame)); frame.struct_size=sizeof(frame);
        CHECK(api->render(game,&frame)==0);
        CHECK(frame.pixels!=NULL); CHECK(frame.tick==60u);
        CHECK(api->wants_exit(game)==0);
        api->destroy(game);
    }

    CHECK(dlclose(so)==0);
    if(failures){fprintf(stderr,"%d failure(s)\n",failures);return 1;}
    puts("WhiteLine native module host test: PASS");
    return 0;
}
