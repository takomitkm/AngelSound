#include "audio_format.h"

#include <SDL3/SDL_audio.h>

bool audio_format_supported(uint32_t format, int channels, int freq, int *bits,
                            bool *is_float)
{
    uint32_t fmt = format;
    int sample_bits = (int)(fmt & SDL_AUDIO_MASK_BITSIZE);
    bool flt = (fmt & SDL_AUDIO_MASK_FLOAT) != 0;
    bool big = (fmt & SDL_AUDIO_MASK_BIG_ENDIAN) != 0;
    bool signed_samples = (fmt & SDL_AUDIO_MASK_SIGNED) != 0;

    bool ok = channels >= 1 && channels <= 8 && freq > 0 && !big;
    if (ok && flt) {
        ok = sample_bits == 32;
    } else if (ok && sample_bits == 8) {
        ok = !signed_samples; /* WAV stores 8-bit PCM unsigned */
    } else if (ok) {
        ok = (sample_bits == 16 || sample_bits == 32) && signed_samples;
    }
    if (bits != NULL) {
        *bits = sample_bits;
    }
    if (is_float != NULL) {
        *is_float = flt;
    }
    return ok;
}