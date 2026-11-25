#ifndef ANGELSOUND_WAV_H
#define ANGELSOUND_WAV_H

/* Minimal PCM WAV reader/writer.  Kept free of SDL so the unit tests link
 * against plain C.  Recording writes whatever the capture device reports
 * (8/16/32-bit PCM or 32-bit float); reading also accepts those formats. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define WAV_HEADER_SIZE 44

typedef struct {
    FILE *fp;
    uint32_t data_bytes;
    /* Set when a write was refused because it would push the RIFF 32-bit
     * data length past UINT32_MAX - 36.  The file stays valid (truncated);
     * the caller decides whether that is an error or a hard stop. */
    bool limit_reached;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    bool is_float;
} WavWriter;

typedef struct {
    uint8_t *data;
    uint32_t data_bytes;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    bool is_float;
} WavAudio;

void wav_format_header(uint8_t header[WAV_HEADER_SIZE], uint32_t data_bytes,
                       uint16_t channels, uint32_t sample_rate,
                       uint16_t bits_per_sample, bool is_float);

bool wav_writer_open(WavWriter *w, const char *path, uint16_t channels,
                     uint32_t sample_rate, uint16_t bits_per_sample, bool is_float,
                     char *err, size_t errcap);
bool wav_writer_write(WavWriter *w, const void *data, size_t bytes);
bool wav_writer_close(WavWriter *w);

bool wav_read(const char *path, WavAudio *out, char *err, size_t errcap);
void wav_audio_free(WavAudio *a);

#endif /* ANGELSOUND_WAV_H */