#if defined(__ANDROID__)
#include <jni.h>
#include <stdint.h>
#include <string.h>

#include "odpar_game.h"

#define ODG_ANDROID_PCM_FLOAT 4
#define ODG_ANDROID_PCM_16BIT 2
#define ODG_ANDROID_PCM_CHUNK 1024u

static float pcm_scratch[ODG_ANDROID_PCM_CHUNK * 2u];

JNIEXPORT jint JNICALL
Java_com_odpar_territorial_1domain_MainActivity_nativeSubmitPcm(
    JNIEnv *env, jobject self, jbyteArray payload, jint frame_count,
    jint channels, jint sample_rate, jint encoding, jlong playback_time_us) {
    jbyte *bytes;
    jsize length;
    uint32_t offset = 0u;
    uint32_t frames;
    int32_t status = ODG_STATUS_OK;
    (void)self;
    if (payload == NULL || frame_count <= 0 || channels <= 0 || channels > 2 ||
        sample_rate < 8000 || sample_rate > 192000) return ODG_STATUS_INVALID_ARGUMENT;
    length = (*env)->GetArrayLength(env, payload);
    bytes = (*env)->GetByteArrayElements(env, payload, NULL);
    if (bytes == NULL) return ODG_STATUS_INVALID_STATE;
    while (offset < (uint32_t)frame_count) {
        uint32_t i, samples;
        frames = (uint32_t)frame_count - offset;
        if (frames > ODG_ANDROID_PCM_CHUNK) frames = ODG_ANDROID_PCM_CHUNK;
        samples = frames * (uint32_t)channels;
        if (encoding == ODG_ANDROID_PCM_16BIT) {
            const uint32_t byte_offset = offset * (uint32_t)channels * 2u;
            if ((uint64_t)byte_offset + (uint64_t)samples * 2u > (uint64_t)length) {
                status = ODG_STATUS_INVALID_ARGUMENT; break;
            }
            for (i = 0u; i < samples; ++i) {
                const uint32_t p = byte_offset + i * 2u;
                const uint16_t raw = (uint16_t)(uint8_t)bytes[p] |
                                     (uint16_t)((uint16_t)(uint8_t)bytes[p + 1u] << 8u);
                const int16_t signed_sample = (int16_t)raw;
                pcm_scratch[i] = (float)signed_sample / 32768.0f;
            }
        } else if (encoding == ODG_ANDROID_PCM_FLOAT) {
            const uint32_t byte_offset = offset * (uint32_t)channels * 4u;
            if ((uint64_t)byte_offset + (uint64_t)samples * 4u > (uint64_t)length) {
                status = ODG_STATUS_INVALID_ARGUMENT; break;
            }
            for (i = 0u; i < samples; ++i) {
                float v;
                memcpy(&v, (const uint8_t *)bytes + byte_offset + i * 4u, sizeof(v));
                pcm_scratch[i] = v;
            }
        } else {
            status = ODG_STATUS_UNSUPPORTED; break;
        }
        status = odg_music_submit_pcm_f32(pcm_scratch, frames, (uint32_t)channels,
                                          (uint32_t)sample_rate,
                                          (uint64_t)playback_time_us +
                                          ((uint64_t)offset * UINT64_C(1000000)) / (uint64_t)sample_rate);
        if (status != ODG_STATUS_OK) break;
        offset += frames;
    }
    (*env)->ReleaseByteArrayElements(env, payload, bytes, JNI_ABORT);
    return status;
}

JNIEXPORT void JNICALL
Java_com_odpar_territorial_1domain_MainActivity_nativeResetMusic(JNIEnv *env, jobject self) {
    (void)env; (void)self; odg_music_reset();
}
#else
/* Keep strict non-Android CMake mirror builds valid without exporting code. */
typedef int odg_android_bridge_nonempty_translation_unit;
#endif
