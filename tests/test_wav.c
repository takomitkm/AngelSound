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

static void put16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
}

static void put32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

static bool write_bytes(const char *path, const unsigned char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    bool ok = fwrite(data, 1, len, f) == len;
    fclose(f);
    return ok;
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

    /* WAVE_FORMAT_EXTENSIBLE with a PCM SubFormat (tag at fmt body + 24). */
    unsigned char ext[70];
    memset(ext, 0, sizeof(ext));
    memcpy(ext, "RIFF", 4);
    put32(ext + 4, (unsigned)sizeof(ext) - 8);
    memcpy(ext + 8, "WAVE", 4);
    memcpy(ext + 12, "fmt ", 4);
    put32(ext + 16, 40);
    put16(ext + 20, 0xFFFE);
    put16(ext + 22, 1);
    put32(ext + 24, 44100);
    put32(ext + 28, 88200);
    put16(ext + 32, 2);
    put16(ext + 34, 16);
    put16(ext + 36, 22); /* cbSize */
    put16(ext + 38, 16); /* valid bits */
    put32(ext + 40, 0);  /* channel mask */
    put16(ext + 44, 1);  /* SubFormat GUID first two bytes = PCM */
    memcpy(ext + 60, "data", 4);
    put32(ext + 64, 2);
    ext[68] = 1;
    ext[69] = 2;
    CHECK(write_bytes(path, ext, sizeof(ext)));
    CHECK(wav_read(path, &a, err, sizeof(err)));
    CHECK(a.channels == 1);
    CHECK(a.sample_rate == 44100);
    CHECK(a.bits_per_sample == 16);
    CHECK(a.data_bytes == 2);
    wav_audio_free(&a);
    remove(path);

    /* 32-bit float round-trip */
    WavWriter wf;
    CHECK(wav_writer_open(&wf, path, 1, 48000, 32, true, err, sizeof(err)));
    const unsigned char floats[8] = { 0, 0, 0x80, 0x3f, 0, 0, 0, 0xbf };
    CHECK(wav_writer_write(&wf, floats, sizeof(floats)));
    CHECK(wav_writer_close(&wf));
    CHECK(wav_read(path, &a, err, sizeof(err)));
    CHECK(a.bits_per_sample == 32);
    CHECK(a.is_float);
    CHECK(a.data_bytes == sizeof(floats));
    wav_audio_free(&a);
    remove(path);

    /* 24-bit PCM has no SDL/WAV path here and must be rejected */
    unsigned char pcm24[47];
    memset(pcm24, 0, sizeof(pcm24));
    memcpy(pcm24, "RIFF", 4);
    put32(pcm24 + 4, 39);
    memcpy(pcm24 + 8, "WAVE", 4);
    memcpy(pcm24 + 12, "fmt ", 4);
    put32(pcm24 + 16, 16);
    put16(pcm24 + 20, 1);
    put16(pcm24 + 22, 1);
    put32(pcm24 + 24, 44100);
    put32(pcm24 + 28, 132300);
    put16(pcm24 + 32, 3);
    put16(pcm24 + 34, 24);
    memcpy(pcm24 + 36, "data", 4);
    put32(pcm24 + 40, 3);
    CHECK(write_bytes(path, pcm24, sizeof(pcm24)));
    CHECK(!wav_read(path, &a, err, sizeof(err)));
    remove(path);

    /* a data chunk that claims more bytes than the file holds is clamped */
    unsigned char short_data[48];
    memset(short_data, 0, sizeof(short_data));
    memcpy(short_data, "RIFF", 4);
    put32(short_data + 4, 40);
    memcpy(short_data + 8, "WAVE", 4);
    memcpy(short_data + 12, "fmt ", 4);
    put32(short_data + 16, 16);
    put16(short_data + 20, 1);
    put16(short_data + 22, 1);
    put32(short_data + 24, 44100);
    put32(short_data + 28, 88200);
    put16(short_data + 32, 2);
    put16(short_data + 34, 16);
    memcpy(short_data + 36, "data", 4);
    put32(short_data + 40, 100);
    CHECK(write_bytes(path, short_data, sizeof(short_data)));
    CHECK(wav_read(path, &a, err, sizeof(err)));
    CHECK(a.data_bytes == 4);
    wav_audio_free(&a);
    remove(path);

    if (failures == 0) {
        printf("test_wav: PASS\n");
        return 0;
    }
    fprintf(stderr, "test_wav: %d failure(s)\n", failures);
    return 1;
}