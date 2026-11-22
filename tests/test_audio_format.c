#include <stdio.h>

#include <SDL3/SDL_audio.h>

#include "audio_format.h"

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

int main(void)
{
    int bits = 0;
    bool is_float = false;

    CHECK(audio_format_supported(SDL_AUDIO_U8, 1, 44100, &bits, &is_float));
    CHECK(bits == 8 && !is_float);
    CHECK(audio_format_supported(SDL_AUDIO_U8, 2, 48000, &bits, &is_float));
    CHECK(bits == 8 && !is_float);

    CHECK(audio_format_supported(SDL_AUDIO_S16LE, 2, 48000, &bits, &is_float));
    CHECK(bits == 16 && !is_float);
    CHECK(audio_format_supported(SDL_AUDIO_S32LE, 1, 96000, &bits, &is_float));
    CHECK(bits == 32 && !is_float);
    CHECK(audio_format_supported(SDL_AUDIO_F32LE, 2, 48000, &bits, &is_float));
    CHECK(bits == 32 && is_float);

    CHECK(!audio_format_supported(SDL_AUDIO_S8, 1, 44100, &bits, &is_float));
    CHECK(!audio_format_supported(SDL_AUDIO_S16BE, 1, 44100, &bits, &is_float));
    CHECK(!audio_format_supported(SDL_AUDIO_F32BE, 2, 48000, &bits, &is_float));
    /* unsigned 16-bit is not representable in WAV: signed=0, size=16 (0x0010) */
    CHECK(!audio_format_supported(0x0010u, 1, 48000, &bits, &is_float));

    CHECK(!audio_format_supported(SDL_AUDIO_S16LE, 0, 48000, &bits, &is_float));
    CHECK(!audio_format_supported(SDL_AUDIO_S16LE, 9, 48000, &bits, &is_float));
    CHECK(!audio_format_supported(SDL_AUDIO_S16LE, 1, 0, &bits, &is_float));

    CHECK(audio_format_supported(SDL_AUDIO_S16LE, 1, 44100, NULL, NULL));

    if (failures == 0) {
        printf("test_audio_format: PASS\n");
        return 0;
    }
    fprintf(stderr, "test_audio_format: %d failure(s)\n", failures);
    return 1;
}