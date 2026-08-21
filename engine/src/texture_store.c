#include "game_internal.h"

#include <stdint.h>

/* Presentation-only texture storage. A full mip pyramid is generated at upload time
 * so distant cube faces do not shimmer. 256^2 + ... + 1^2 = 87381 texels/face. */
#define ODG_AVATAR_MIP_LEVELS 9u
#define ODG_AVATAR_MIP_TEXELS 87381u
#define ODG_AVATAR_MIP_BYTES (ODG_AVATAR_MIP_TEXELS*4u)
static uint8_t g_player_mips[ODG_AVATAR_FACE_COUNT][ODG_AVATAR_MIP_BYTES];
static uint8_t g_player_face_present[ODG_AVATAR_FACE_COUNT];

static uint32_t mip_dim(uint32_t level){uint32_t d=ODG_AVATAR_TEXTURE_SIZE>>level;return d==0u?1u:d;}
static uint32_t mip_texel_offset(uint32_t level){uint32_t off=0u,i;for(i=0u;i<level&&i<ODG_AVATAR_MIP_LEVELS;++i){uint32_t d=mip_dim(i);off+=d*d;}return off;}
static uint32_t pack_rgba(const uint8_t *p){return ((uint32_t)p[0]<<24u)|((uint32_t)p[1]<<16u)|((uint32_t)p[2]<<8u)|(uint32_t)p[3];}

static void build_face_mips(uint32_t face){
    uint32_t level;
    for(level=1u;level<ODG_AVATAR_MIP_LEVELS;++level){
        uint32_t dst_d=mip_dim(level),src_d=mip_dim(level-1u),dst_off=mip_texel_offset(level),src_off=mip_texel_offset(level-1u),y,x,c;
        for(y=0u;y<dst_d;++y)for(x=0u;x<dst_d;++x)for(c=0u;c<4u;++c){
            uint32_t sx=x*2u,sy=y*2u,sum=0u;
            sum+=g_player_mips[face][((src_off+sy*src_d+sx)*4u)+c];
            sum+=g_player_mips[face][((src_off+sy*src_d+(sx+1u<src_d?sx+1u:sx))*4u)+c];
            sum+=g_player_mips[face][((src_off+(sy+1u<src_d?sy+1u:sy)*src_d+sx)*4u)+c];
            sum+=g_player_mips[face][((src_off+(sy+1u<src_d?sy+1u:sy)*src_d+(sx+1u<src_d?sx+1u:sx))*4u)+c];
            g_player_mips[face][((dst_off+y*dst_d+x)*4u)+c]=(uint8_t)((sum+2u)/4u);
        }
    }
}

int32_t odg_avatar_texture_upload(uint32_t face,const uint8_t *rgba,uint32_t width,uint32_t height,uint32_t stride){
    uint32_t y;const uint32_t row_bytes=ODG_AVATAR_TEXTURE_SIZE*4u;
    if(face>=ODG_AVATAR_FACE_COUNT||rgba==NULL)return ODG_STATUS_INVALID_ARGUMENT;
    if(width!=ODG_AVATAR_TEXTURE_SIZE||height!=ODG_AVATAR_TEXTURE_SIZE||stride<row_bytes)return ODG_STATUS_INVALID_ARGUMENT;
    for(y=0u;y<ODG_AVATAR_TEXTURE_SIZE;++y)odg_memcpy(&g_player_mips[face][y*row_bytes],rgba+(size_t)y*stride,row_bytes);
    build_face_mips(face);g_player_face_present[face]=1u;return ODG_STATUS_OK;
}
int32_t odg_avatar_texture_clear(uint32_t face){if(face>=ODG_AVATAR_FACE_COUNT)return ODG_STATUS_INVALID_ARGUMENT;g_player_face_present[face]=0u;return ODG_STATUS_OK;}
uint32_t odg_avatar_texture_present(uint32_t face){return face<ODG_AVATAR_FACE_COUNT&&g_player_face_present[face]!=0u?1u:0u;}

static uint32_t hash32(uint32_t v){v^=v>>16u;v*=UINT32_C(0x7feb352d);v^=v>>15u;v*=UINT32_C(0x846ca68b);v^=v>>16u;return v;}
static uint32_t bot_procedural_skin(uint32_t actor_id,uint32_t face,uint32_t u_q16,uint32_t v_q16){
    uint32_t seed_lo=(uint32_t)g_odg.seed,h=hash32(seed_lo^actor_id*UINT32_C(0x9e3779b1)^face*UINT32_C(0x85ebca6b));
    static const uint32_t base[9]={0xa55f62ffu,0xb48152ffu,0x7566a5ffu,0x539676ffu,0x9d5d86ffu,0xa28f4dffu,0x607c9bffu,0xa76752ffu,0x739455ffu};
    uint32_t c=base[(actor_id-1u)%9u],u=u_q16>>12u,v=v_q16>>12u,stripe=(u+v+(h&7u))%7u,edge=(u<2u||v<2u||u>13u||v>13u)?1u:0u;
    uint32_t r=(c>>24u)&255u,g=(c>>16u)&255u,b=(c>>8u)&255u;
    if(stripe==0u){r=(r*122u)/100u;g=(g*122u)/100u;b=(b*122u)/100u;}if(edge){r=(r*68u)/100u;g=(g*68u)/100u;b=(b*68u)/100u;}
    if(face==ODG_AVATAR_FACE_FRONT&&v>=5u&&v<=7u&&(u==5u||u==10u)){r=225u;g=235u;b=238u;}if(r>255u)r=255u;if(g>255u)g=255u;if(b>255u)b=255u;
    return (r<<24u)|(g<<16u)|(b<<8u)|255u;
}

static uint32_t sample_player_bilinear(uint32_t face,uint32_t u_q16,uint32_t v_q16,uint32_t level){
    uint32_t d,off,x0,y0,x1,y1,fx,fy,c,out[4]={0u,0u,0u,0u};
    if(level>=ODG_AVATAR_MIP_LEVELS) level=ODG_AVATAR_MIP_LEVELS-1u;
    d=mip_dim(level);off=mip_texel_offset(level);
    if(u_q16>65535u) u_q16=65535u;
    if(v_q16>65535u) v_q16=65535u;
    if(d==1u)return pack_rgba(&g_player_mips[face][off*4u]);
    {uint64_t ux=(uint64_t)u_q16*(uint64_t)(d-1u),vy=(uint64_t)v_q16*(uint64_t)(d-1u);x0=(uint32_t)(ux>>16u);y0=(uint32_t)(vy>>16u);fx=(uint32_t)(ux&65535u);fy=(uint32_t)(vy&65535u);}
    x1=x0+1u<d?x0+1u:x0;y1=y0+1u<d?y0+1u:y0;
    for(c=0u;c<4u;++c){
        uint32_t p00=g_player_mips[face][((off+y0*d+x0)*4u)+c],p10=g_player_mips[face][((off+y0*d+x1)*4u)+c];
        uint32_t p01=g_player_mips[face][((off+y1*d+x0)*4u)+c],p11=g_player_mips[face][((off+y1*d+x1)*4u)+c];
        uint32_t top=(uint32_t)(((uint64_t)p00*(65536u-fx)+(uint64_t)p10*fx)>>16u),bot=(uint32_t)(((uint64_t)p01*(65536u-fx)+(uint64_t)p11*fx)>>16u);
        out[c]=(uint32_t)(((uint64_t)top*(65536u-fy)+(uint64_t)bot*fy)>>16u);
    }
    return (out[0]<<24u)|(out[1]<<16u)|(out[2]<<8u)|out[3];
}

uint32_t odg_avatar_texture_sample_lod_internal(uint32_t actor_id,uint32_t face,uint32_t u_q16,uint32_t v_q16,uint32_t lod){
    if(face>=ODG_AVATAR_FACE_COUNT)return 0xffffffffu;
    if(actor_id==ODG_PLAYER_ID&&g_player_face_present[face]!=0u)return sample_player_bilinear(face,u_q16,v_q16,lod);
    if(actor_id!=ODG_PLAYER_ID) return bot_procedural_skin(actor_id,face,u_q16,v_q16);
    return 0u;
}
