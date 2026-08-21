#include "game_internal.h"

#include <stdint.h>
#include <stdatomic.h>

typedef struct {
    uint32_t initialized;
    uint32_t sample_rate;
    uint64_t playback_time_us;
    float lowpass_bass;
    float lowpass_low_mid;
    float lowpass_mid;
    float noise_floor;
    float agc_peak;
    float envelope;
    float previous_bass;
    float energy;
    float bass;
    float low_mid;
    float mid;
    float high;
    float onset;
    float beat;
    float beat_strength;
    float activity;
} odg_music_state;

static odg_music_state g_music;
static atomic_flag g_music_lock = ATOMIC_FLAG_INIT;
static _Atomic uint32_t g_music_beat_cache_q16;

static void music_lock(void) { while (atomic_flag_test_and_set_explicit(&g_music_lock,memory_order_acquire)) { } }
static void music_unlock(void) { atomic_flag_clear_explicit(&g_music_lock,memory_order_release); }

static float f_abs(float v) { return v < 0.0f ? -v : v; }
static float f_max(float a,float b) { return a>b?a:b; }
static float f_min(float a,float b) { return a<b?a:b; }
static float f_clamp01(float v) { return v<0.0f?0.0f:(v>1.0f?1.0f:v); }
static int f_valid(float v) { return v==v && v>-4.0f && v<4.0f; }

static uint32_t q16(float v) {
    float c=f_clamp01(v);
    return (uint32_t)(c*65535.0f+0.5f);
}

static void music_reset_unlocked(void) {
    odg_memset(&g_music,0,sizeof(g_music));
    g_music.noise_floor=0.0025f;
    g_music.agc_peak=0.08f;
    atomic_store_explicit(&g_music_beat_cache_q16,0u,memory_order_release);
}

void odg_music_reset(void) {
    music_lock();
    music_reset_unlocked();
    music_unlock();
}

int32_t odg_music_submit_pcm_f32(const float *interleaved,uint32_t frame_count,
                                 uint32_t channels,uint32_t sample_rate,
                                 uint64_t playback_time_us) {
    uint32_t frame,channel;
    float sum=0.0f,bass_sum=0.0f,low_mid_sum=0.0f,mid_sum=0.0f,high_sum=0.0f;
    float bass_alpha,low_mid_alpha,mid_alpha;
    float raw_energy,raw_bass,raw_low_mid,raw_mid,raw_high;
    float signal,normalizer,onset;
    if (interleaved==NULL || frame_count==0u || frame_count>8192u ||
        channels==0u || channels>8u || sample_rate<8000u || sample_rate>192000u)
        return ODG_STATUS_INVALID_ARGUMENT;

    music_lock();
    if (!g_music.initialized || playback_time_us+UINT64_C(20000)<g_music.playback_time_us ||
        playback_time_us>g_music.playback_time_us+UINT64_C(750000)) {
        float floor=g_music.noise_floor;
        float peak=g_music.agc_peak;
        music_reset_unlocked();
        g_music.noise_floor=floor>0.0f?floor:0.0025f;
        g_music.agc_peak=peak>0.0f?peak:0.08f;
    }
    g_music.initialized=1u;
    g_music.sample_rate=sample_rate;
    bass_alpha=f_min(0.20f,f_max(0.004f,1131.0f/(float)sample_rate));
    low_mid_alpha=f_min(0.28f,f_max(0.008f,3200.0f/(float)sample_rate));
    mid_alpha=f_min(0.38f,f_max(0.012f,7600.0f/(float)sample_rate));

    for (frame=0u;frame<frame_count;++frame) {
        float mono=0.0f;
        float b,lm,m,h;
        for (channel=0u;channel<channels;++channel) {
            float sample=interleaved[(size_t)frame*(size_t)channels+(size_t)channel];
            if (!f_valid(sample)) sample=0.0f;
            if (sample>1.0f) sample=1.0f;
            if (sample<-1.0f) sample=-1.0f;
            mono+=sample;
        }
        mono/=(float)channels;
        g_music.lowpass_bass+=(mono-g_music.lowpass_bass)*bass_alpha;
        g_music.lowpass_low_mid+=(mono-g_music.lowpass_low_mid)*low_mid_alpha;
        g_music.lowpass_mid+=(mono-g_music.lowpass_mid)*mid_alpha;
        b=f_abs(g_music.lowpass_bass);
        lm=f_abs(g_music.lowpass_low_mid-g_music.lowpass_bass);
        m=f_abs(g_music.lowpass_mid-g_music.lowpass_low_mid);
        h=f_abs(mono-g_music.lowpass_mid);
        sum+=f_abs(mono);
        bass_sum+=b;
        low_mid_sum+=lm;
        mid_sum+=m;
        high_sum+=h;
    }

    raw_energy=sum/(float)frame_count;
    raw_bass=bass_sum/(float)frame_count;
    raw_low_mid=low_mid_sum/(float)frame_count;
    raw_mid=mid_sum/(float)frame_count;
    raw_high=high_sum/(float)frame_count;

    /* Slow adaptive floor + bounded peak normalization. This remains intentionally
     * conservative: quiet songs become visible without one transient causing minutes
     * of under-reaction. */
    if (raw_energy<g_music.noise_floor) g_music.noise_floor=g_music.noise_floor*0.985f+raw_energy*0.015f;
    else g_music.noise_floor=g_music.noise_floor*0.9995f+raw_energy*0.0005f;
    signal=f_max(0.0f,raw_energy-g_music.noise_floor*1.25f);
    if (signal>g_music.agc_peak) g_music.agc_peak=g_music.agc_peak*0.72f+signal*0.28f;
    else g_music.agc_peak=f_max(0.035f,g_music.agc_peak*0.9985f);
    normalizer=1.0f/f_max(0.035f,g_music.agc_peak);

    g_music.energy=f_clamp01(signal*normalizer);
    g_music.bass=f_clamp01(f_max(0.0f,raw_bass-g_music.noise_floor*0.35f)*normalizer*1.55f);
    g_music.low_mid=f_clamp01(raw_low_mid*normalizer*2.15f);
    g_music.mid=f_clamp01(raw_mid*normalizer*2.65f);
    g_music.high=f_clamp01(raw_high*normalizer*3.10f);
    onset=f_max(0.0f,(g_music.energy-g_music.envelope*1.08f)*3.8f) +
          f_max(0.0f,(g_music.bass-g_music.previous_bass)*2.2f);
    onset=f_clamp01(onset);
    g_music.onset=onset;
    g_music.envelope=f_max(g_music.energy,g_music.envelope*0.88f);
    g_music.previous_bass=g_music.bass;
    if (onset>0.12f) {
        g_music.beat=f_max(g_music.beat,onset);
        g_music.beat_strength=f_max(g_music.beat_strength,f_clamp01(onset*0.72f+g_music.bass*0.45f));
    }
    g_music.activity=f_clamp01(g_music.energy*0.54f+g_music.low_mid*0.18f+g_music.mid*0.18f+g_music.high*0.10f);
    g_music.playback_time_us=playback_time_us;
    atomic_store_explicit(&g_music_beat_cache_q16,q16(g_music.beat),memory_order_release);
    music_unlock();
    return ODG_STATUS_OK;
}

void odg_music_decay_visual_tick(void) {
    music_lock();
    g_music.beat*=0.915f;
    g_music.beat_strength*=0.928f;
    g_music.onset*=0.82f;
    if (g_music.beat<0.0002f) g_music.beat=0.0f;
    if (g_music.beat_strength<0.0002f) g_music.beat_strength=0.0f;
    if (g_music.onset<0.0002f) g_music.onset=0.0f;
    atomic_store_explicit(&g_music_beat_cache_q16,q16(g_music.beat),memory_order_release);
    music_unlock();
}

uint32_t odg_music_beat_q16_internal(void) { return atomic_load_explicit(&g_music_beat_cache_q16,memory_order_acquire); }

int32_t odg_copy_music_frame(odg_music_reactive_frame *out_frame,
                             uint64_t capacity,uint64_t *out_required) {
    if (out_required!=NULL) *out_required=(uint64_t)sizeof(odg_music_reactive_frame);
    if (out_frame==NULL || capacity<(uint64_t)sizeof(*out_frame)) return ODG_STATUS_BUFFER_TOO_SMALL;
    music_lock();
    odg_memset(out_frame,0,sizeof(*out_frame));
    out_frame->struct_size=(uint32_t)sizeof(*out_frame);
    out_frame->sample_rate=g_music.sample_rate;
    out_frame->playback_time_us=g_music.playback_time_us;
    out_frame->energy_q16=q16(g_music.energy);
    out_frame->bass_q16=q16(g_music.bass);
    out_frame->low_mid_q16=q16(g_music.low_mid);
    out_frame->mid_q16=q16(g_music.mid);
    out_frame->high_q16=q16(g_music.high);
    out_frame->onset_q16=q16(g_music.onset);
    out_frame->beat_q16=q16(g_music.beat);
    out_frame->beat_strength_q16=q16(g_music.beat_strength);
    out_frame->activity_q16=q16(g_music.activity);
    music_unlock();
    return ODG_STATUS_OK;
}
