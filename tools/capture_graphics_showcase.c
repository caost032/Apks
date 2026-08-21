#include "odpar_game.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Reproducible renderer-only review capture. It uses the exact same C framebuffer as
 * Flutter/FFI and never substitutes web/CSS art for engine output. The showcase camera
 * is presentation-only; deterministic gameplay rules remain untouched. */
int main(int argc,char **argv){
    const char *path=argc>1?argv[1]:"build/graphics_showcase.ppm";
    uint32_t width=argc>2?(uint32_t)strtoul(argv[2],NULL,10):1280u;
    uint32_t height=argc>3?(uint32_t)strtoul(argv[3],NULL,10):720u;
    uint32_t theme=argc>4?(uint32_t)strtoul(argv[4],NULL,10):ODG_VISUAL_THEME_NEON_TIDES;
    uint32_t i;FILE *f;const uint8_t *pixels;
    if(width==0u||height==0u||width>ODG_MAX_RENDER_WIDTH||height>ODG_MAX_RENDER_HEIGHT)return 4;
    if(theme>=ODG_VISUAL_THEME_COUNT)return 5;
    if(odg_init(UINT64_C(0x4f4450415253484f),width,height)!=0)return 1;
    odg_set_visual_theme(theme);
    /* Build a real live trail and move away from the exact spawn composition. */
    for(i=0u;i<168u;++i){
        int32_t ix=i<84u?12500:23000;
        int32_t iz=i<84u?28900:22600;
        odg_set_input(ix,iz,0,0,0u);
        odg_step_ticks(1u);
    }
    odg_set_presentation_mode(ODG_PRESENTATION_SHOWCASE);
    pixels=(const uint8_t *)odg_render_frame();
    f=fopen(path,"wb");if(f==NULL)return 2;
    fprintf(f,"P6\n%u %u\n255\n",odg_render_width(),odg_render_height());
    for(i=0u;i<odg_render_width()*odg_render_height();++i)fwrite(pixels+i*4u,1u,3u,f);
    fclose(f);
    printf("showcase %ux%u theme=%u cells=%u trail=%u alive=%u\n",
           odg_render_width(),odg_render_height(),theme,odg_player_territory_cells(),
           odg_player_trail_cells(),odg_alive_count());
    return 0;
}
