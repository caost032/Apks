#include "game_internal.h"

/* Shared compact 5x7 glyph atlas. The renderer consumes these rows for world-space
 * labels, item cards and turret telemetry. Keeping glyph data here prevents each
 * feature from inventing its own text renderer. Bits 4..0 are left-to-right. */
int odg_glyph5x7_internal(char ch,uint8_t rows[7]) {
    static const uint8_t digits[10][7]={
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},
        {31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}};
    static const uint8_t letters[26][7]={
        {14,17,17,31,17,17,17}, /* A */ {30,17,17,30,17,17,30}, /* B */
        {14,17,16,16,16,17,14}, /* C */ {30,17,17,17,17,17,30}, /* D */
        {31,16,16,30,16,16,31}, /* E */ {31,16,16,30,16,16,16}, /* F */
        {14,17,16,23,17,17,15}, /* G */ {17,17,17,31,17,17,17}, /* H */
        {14,4,4,4,4,4,14},      /* I */ {7,2,2,2,18,18,12},      /* J */
        {17,18,20,24,20,18,17}, /* K */ {16,16,16,16,16,16,31}, /* L */
        {17,27,21,21,17,17,17}, /* M */ {17,25,21,19,17,17,17}, /* N */
        {14,17,17,17,17,17,14}, /* O */ {30,17,17,30,16,16,16}, /* P */
        {14,17,17,17,21,18,13}, /* Q */ {30,17,17,30,20,18,17}, /* R */
        {15,16,16,14,1,1,30},   /* S */ {31,4,4,4,4,4,4},       /* T */
        {17,17,17,17,17,17,14}, /* U */ {17,17,17,17,17,10,4}, /* V */
        {17,17,17,21,21,21,10}, /* W */ {17,17,10,4,10,17,17}, /* X */
        {17,17,10,4,4,4,4},     /* Y */ {31,1,2,4,8,16,31}      /* Z */
    };
    static const uint8_t slash[7]={1,1,2,4,8,16,16};
    static const uint8_t dash[7]={0,0,0,31,0,0,0};
    static const uint8_t dot[7]={0,0,0,0,0,12,12};
    static const uint8_t colon[7]={0,12,12,0,12,12,0};
    static const uint8_t plus[7]={0,4,4,31,4,4,0};
    const uint8_t *src=NULL;uint32_t r;
    if(ch>='a'&&ch<='z') ch=(char)(ch-'a'+'A');
    if(ch>='0'&&ch<='9') src=digits[(uint32_t)(ch-'0')];
    else if(ch>='A'&&ch<='Z') src=letters[(uint32_t)(ch-'A')];
    else if(ch=='/') src=slash; else if(ch=='-') src=dash; else if(ch=='.') src=dot;
    else if(ch==':') src=colon; else if(ch=='+') src=plus;
    else if(ch==' ') { for(r=0u;r<7u;++r) rows[r]=0u; return 1; }
    if(src==NULL) return 0;
    for(r=0u;r<7u;++r) rows[r]=src[r];
    return 1;
}
