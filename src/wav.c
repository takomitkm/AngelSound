#include "wav.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define WAV_READ_MAX_BYTES (512u * 1024 * 1024)

static void set_err(char *err, size_t cap, const char *fmt, ...)
{
    if (err == NULL || cap == 0) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, cap, fmt, ap);
    va_end(ap);
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

void wav_format_header(uint8_t header[WAV_HEADER_SIZE], uint32_t data_bytes,
                       uint16_t channels, uint32_t sample_rate,
                       uint16_t bits_per_sample, bool is_float)
{
    uint16_t block_align = (uint16_t)(channels * bits_per_sample / 8);
    uint32_t byte_rate = sample_rate * block_align;
    uint16_t format_tag = is_float ? 3 : 1;

    memcpy(header, "RIFF", 4);
    put_u32(header + 4, 36 + data_bytes);
    memcpy(header + 8, "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
    put_u32(header + 16, 16);
    put_u16(header + 20, format_tag);
    put_u16(header + 22, channels);
    put_u32(header + 24, sample_rate);
    put_u32(header + 28, byte_rate);
    put_u16(header + 32, block_align);
    put_u16(header + 34, bits_per_sample);
    memcpy(header + 36, "data", 4);
    put_u32(header + 40, data_bytes);
}

bool wav_writer_open(WavWriter *w, const char *path, uint16_t channels,
                     uint32_t sample_rate, uint16_t bits_per_sample, bool is_float,
                     char *err, size_t errcap)
{
    memset(w, 0, sizeof(*w));
    w->channels = channels;
    w->sample_rate = sample_rate;
    w->bits_per_sample = bits_per_sample;
    w->is_float = is_float;

    w->fp = fopen(path, "wb");
    if (w->fp == NULL) {
        set_err(err, errcap, "cannot open '%s'", path);
        return false;
    }

    uint8_t header[WAV_HEADER_SIZE];
    wav_format_header(header, 0, channels, sample_rate, bits_per_sample, is_float);
    if (fwrite(header, 1, WAV_HEADER_SIZE, w->fp) != WAV_HEADER_SIZE) {
        set_err(err, errcap, "cannot write WAV header to '%s'", path);
        fclose(w->fp);
        w->fp = NULL;
        return false;
    }
    return true;
}

bool wav_writer_write(WavWriter *w, const void *data, size_t bytes)
{
    if (w->fp == NULL || bytes == 0) {
        return false;
    }
    /* A plain RIFF WAV stores its data length as a 32-bit field, and the
     * header reserves 36 bytes before it.  Refuse a write that would push
     * the count past that; accepting it would wrap and leave a header that
     * describes a file larger than what is actually on disk. */
    if ((uint64_t)bytes > (uint64_t)UINT32_MAX - 36u - w->data_bytes) {
        w->limit_reached = true;
        return false;
    }
    if (fwrite(data, 1, bytes, w->fp) != bytes) {
        return false;
    }
    w->data_bytes += (uint32_t)bytes;
    return true;
}

bool wav_writer_close(WavWriter *w)
{
    if (w->fp == NULL) {
        return false;
    }
    uint8_t header[WAV_HEADER_SIZE];
    wav_format_header(header, w->data_bytes, w->channels, w->sample_rate,
                      w->bits_per_sample, w->is_float);
    bool ok = fseek(w->fp, 0, SEEK_SET) == 0 &&
              fwrite(header, 1, WAV_HEADER_SIZE, w->fp) == WAV_HEADER_SIZE &&
              fflush(w->fp) == 0;
    ok &= fclose(w->fp) == 0;
    w->fp = NULL;
    return ok;
}

bool wav_read(const char *path, WavAudio *out, char *err, size_t errcap)
{
    memset(out, 0, sizeof(*out));

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        set_err(err, errcap, "cannot open '%s'", path);
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        set_err(err, errcap, "cannot seek '%s'", path);
        fclose(fp);
        return false;
    }
    long file_size = ftell(fp);
    if (file_size < 0 || (unsigned long)file_size > WAV_READ_MAX_BYTES) {
        set_err(err, errcap, "'%s' is empty or too large", path);
        fclose(fp);
        return false;
    }
    rewind(fp);
    uint8_t *buf = malloc((size_t)file_size);
    if (buf == NULL) {
        set_err(err, errcap, "out of memory reading '%s'", path);
        fclose(fp);
        return false;
    }
    if (fread(buf, 1, (size_t)file_size, fp) != (size_t)file_size) {
        set_err(err, errcap, "cannot read '%s'", path);
        free(buf);
        fclose(fp);
        return false;
    }
    fclose(fp);

    if (file_size < 44 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        set_err(err, errcap, "'%s' is not a WAV file", path);
        free(buf);
        return false;
    }

    uint32_t size = (uint32_t)file_size;
    uint32_t off = 12;
    bool have_fmt = false;
    bool have_data = false;
    uint16_t tag = 1, channels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t *data = NULL;
    uint32_t data_bytes = 0;

    while (off + 8 <= size) {
        const uint8_t *id = buf + off;
        uint32_t chunk_size = get_u32(buf + off + 4);
        uint32_t body = off + 8;
        /* body <= size here (loop condition), so size - body cannot wrap.
         * Using a subtraction instead of body + chunk_size avoids the 32-bit
         * wrap where a huge declared size (e.g. 0xfffffff8) would make the
         * sum come out small and slip past the bound check, letting the
         * reader copy gigabytes out of a small buffer. */
        if (chunk_size > size - body) {
            chunk_size = size - body; /* tolerate a truncated data chunk */
        }
        if (memcmp(id, "fmt ", 4) == 0 && chunk_size >= 16) {
            tag = get_u16(buf + body);
            channels = get_u16(buf + body + 2);
            rate = get_u32(buf + body + 4);
            bits = get_u16(buf + body + 14);
            if (tag == 0xFFFE && chunk_size >= 40) {
                /* WAVEFORMATEXTENSIBLE: the first 2 bytes of the SubFormat
                 * GUID (offset 24 in the fmt chunk) carry the real format tag. */
                tag = get_u16(buf + body + 24);
            }
            have_fmt = true;
        } else if (memcmp(id, "data", 4) == 0) {
            data = buf + body;
            data_bytes = chunk_size;
            have_data = true;
        }
        off = body + chunk_size + (chunk_size & 1);
    }

    if (!have_fmt || !have_data) {
        set_err(err, errcap, "'%s' has no fmt/data chunk", path);
        free(buf);
        return false;
    }
    bool is_float = tag == 3;
    if ((tag != 1 && tag != 3) || (is_float && bits != 32) ||
        (!is_float && bits != 8 && bits != 16 && bits != 32) ||
        channels < 1 || channels > 8 || rate < 1 || rate > 384000) {
        set_err(err, errcap, "'%s' uses an unsupported WAV format", path);
        free(buf);
        return false;
    }

    uint8_t *pcm = malloc(data_bytes > 0 ? data_bytes : 1);
    if (pcm == NULL) {
        set_err(err, errcap, "out of memory reading '%s'", path);
        free(buf);
        return false;
    }
    if (data_bytes > 0) {
        memcpy(pcm, data, data_bytes);
    }
    free(buf);

    out->data = pcm;
    out->data_bytes = data_bytes;
    out->channels = channels;
    out->sample_rate = rate;
    out->bits_per_sample = bits;
    out->is_float = is_float;
    return true;
}

void wav_audio_free(WavAudio *a)
{
    if (a != NULL) {
        free(a->data);
        memset(a, 0, sizeof(*a));
    }
}