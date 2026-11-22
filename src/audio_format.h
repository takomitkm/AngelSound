#ifndef ANGELSOUND_AUDIO_FORMAT_H
#define ANGELSOUND_AUDIO_FORMAT_H

#include <stdbool.h>
#include <stdint.h>

/* Validate a capture format for direct WAV storage: 8-bit unsigned PCM,
 * 16/32-bit little-endian PCM, or 32-bit float.  No conversion is performed. */
bool audio_format_supported(uint32_t format, int channels, int freq, int *bits,
                            bool *is_float);

#endif /* ANGELSOUND_AUDIO_FORMAT_H */