#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wav.h"

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static unsigned rd32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) |
           ((unsigned)p[3] << 24);
}

static unsigned rd16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

int main(void)
{
    unsigned char hdr[WAV_HEADER_SIZE];
    char err[256] = { 0 };
    const char *path = "test_wav_tmp.wav";

    wav_format_header(hdr, 0, 1, 44100, 16, false);
    CHECK(memcmp(hdr, "RIFF", 4) == 0);
    CHECK(memcmp(hdr + 8, "WAVE", 4) == 0);
    CHECK(memcmp(hdr + 12, "fmt ", 4) == 0);
    CHECK(rd32(hdr + 4) == 36);
    CHECK(rd16(hdr + 20) == 1);       /* PCM */
    CHECK(rd16(hdr + 22) == 1);       /* mono */
    CHECK(rd32(hdr + 24) == 44100);   /* sample rate */
    CHECK(rd32(hdr + 28) == 88200);   /* byte rate */
    CHECK(rd16(hdr + 32) == 2);       /* block align */
    CHECK(rd16(hdr + 34) == 16);      /* bits */
    CHECK(memcmp(hdr + 36, "data", 4) == 0);
    CHECK(rd32(hdr + 40) == 0);

    wav_format_header(hdr, 8, 2, 48000, 32, true);
    CHECK(rd16(hdr + 20) == 3);       /* IEEE float */
    CHECK(rd32(hdr + 4) == 44);
    CHECK(rd32(hdr + 28) == 48000u * 8u);
    CHECK(rd16(hdr + 32) == 8);
    CHECK(rd32(hdr + 40) == 8);

    /* writer round-trip */
    remove(path);
    WavWriter w;
    CHECK(wav_writer_open(&w, path, 1, 44100, 16, false, err, sizeof(err)));
    const unsigned char samples[8] = { 0, 0, 1, 0, 0xff, 0x7f, 0, 0 };
    CHECK(wav_writer_write(&w, samples, sizeof(samples)));
    CHECK(wav_writer_close(&w));
    CHECK(file_size(path) == WAV_HEADER_SIZE + (long)sizeof(samples));

    WavAudio a;
    CHECK(wav_read(path, &a, err, sizeof(err)));
    CHECK(a.data_bytes == sizeof(samples));
    CHECK(a.channels == 1);
    CHECK(a.sample_rate == 44100);
    CHECK(a.bits_per_sample == 16);
    CHECK(!a.is_float);
    CHECK(a.data != NULL && memcmp(a.data, samples, sizeof(samples)) == 0);
    wav_audio_free(&a);
    CHECK(a.data == NULL);
    remove(path);

    /* unsupported / malformed input is rejected, not crashed on */
    FILE *bad = fopen(path, "wb");
    CHECK(bad != NULL);
    if (bad != NULL) {
        fputs("not a wav", bad);
        fclose(bad);
    }
    CHECK(!wav_read(path, &a, err, sizeof(err)));
    remove(path);

    /* header-only recording (what a zero-length take looks like) is valid */
    WavWriter w2;
    CHECK(wav_writer_open(&w2, path, 1, 44100, 16, false, err, sizeof(err)));
    CHECK(wav_writer_close(&w2));
    CHECK(wav_read(path, &a, err, sizeof(err)));
    CHECK(a.data_bytes == 0);
    wav_audio_free(&a);
    remove(path);

    if (failures == 0) {
        printf("test_wav: PASS\n");
        return 0;
    }
    fprintf(stderr, "test_wav: %d failure(s)\n", failures);
    return 1;
}