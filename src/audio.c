#include "audio.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#define REC_BYTES_PER_FRAME (AUDIO_CHANNELS * 2) /* S16 mono */

struct AudioEngine {
    SDL_AudioDeviceID *inputs;
    int input_count;
    int input_index; /* 0 = system default, 1..input_count = inputs[i-1] */

    SDL_AudioDeviceID rec_dev;
    SDL_AudioStream *rec_stream;
    WavWriter rec_wav;
    bool rec_wav_open;
    uint64_t rec_frames;

    SDL_AudioDeviceID play_dev;
    SDL_AudioStream *play_stream;

    AudioState state;
};

static SDL_AudioSpec capture_spec(void)
{
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = AUDIO_CHANNELS;
    spec.freq = AUDIO_SAMPLE_RATE;
    return spec;
}

static void close_recording(AudioEngine *a)
{
    if (a->rec_stream != NULL) {
        SDL_UnbindAudioStream(a->rec_stream);
        SDL_DestroyAudioStream(a->rec_stream);
        a->rec_stream = NULL;
    }
    if (a->rec_dev != 0) {
        SDL_CloseAudioDevice(a->rec_dev);
        a->rec_dev = 0;
    }
    a->rec_wav_open = false;
    a->state = AUDIO_STATE_IDLE;
}

static void close_playback(AudioEngine *a)
{
    if (a->play_stream != NULL) {
        SDL_UnbindAudioStream(a->play_stream);
        SDL_DestroyAudioStream(a->play_stream);
        a->play_stream = NULL;
    }
    if (a->play_dev != 0) {
        SDL_CloseAudioDevice(a->play_dev);
        a->play_dev = 0;
    }
    a->state = AUDIO_STATE_IDLE;
}

/* Drain whatever the capture stream has produced into the WAV file. */
static void pump_recording(AudioEngine *a)
{
    if (a->rec_stream == NULL || !a->rec_wav_open) {
        return;
    }
    int avail = SDL_GetAudioStreamAvailable(a->rec_stream);
    while (avail > 0) {
        uint8_t buf[8192];
        int len = avail < (int)sizeof(buf) ? avail : (int)sizeof(buf);
        int got = SDL_GetAudioStreamData(a->rec_stream, buf, len);
        if (got <= 0) {
            break;
        }
        if (!wav_writer_write(&a->rec_wav, buf, (size_t)got)) {
            break; /* keep recording; the file may be incomplete */
        }
        a->rec_frames += (uint64_t)got / REC_BYTES_PER_FRAME;
        avail -= got;
    }
}

bool audio_init(AudioEngine **out, char *err, size_t errcap)
{
    *out = NULL;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        snprintf(err, errcap, "SDL_InitSubSystem(AUDIO): %s", SDL_GetError());
        return false;
    }
    AudioEngine *a = calloc(1, sizeof(*a));
    if (a == NULL) {
        snprintf(err, errcap, "out of memory");
        return false;
    }
    a->input_index = 0;
    a->state = AUDIO_STATE_IDLE;
    int count = 0;
    a->inputs = SDL_GetAudioRecordingDevices(&count);
    a->input_count = a->inputs != NULL ? count : 0;
    *out = a;
    return true;
}

void audio_shutdown(AudioEngine *a)
{
    if (a == NULL) {
        return;
    }
    if (a->state == AUDIO_STATE_RECORDING) {
        uint64_t frames = 0;
        char err[256];
        audio_stop_recording(a, &frames, err, sizeof(err));
    }
    if (a->state == AUDIO_STATE_PLAYING || a->state == AUDIO_STATE_PAUSED) {
        close_playback(a);
    }
    SDL_free(a->inputs);
    free(a);
}

int audio_input_choices(const AudioEngine *a)
{
    return a->input_count + 1;
}

int audio_selected_input(const AudioEngine *a)
{
    return a->input_index;
}

void audio_cycle_input(AudioEngine *a)
{
    a->input_index = (a->input_index + 1) % (a->input_count + 1);
}

void audio_input_name(const AudioEngine *a, int index, char *buf, size_t cap)
{
    if (index <= 0) {
        snprintf(buf, cap, "System default");
        return;
    }
    const char *name = NULL;
    if (a->inputs != NULL && index <= a->input_count) {
        name = SDL_GetAudioDeviceName(a->inputs[index - 1]);
    }
    snprintf(buf, cap, "%s", name != NULL ? name : "(unnamed device)");
}

AudioState audio_state(const AudioEngine *a)
{
    return a->state;
}

uint64_t audio_record_frames(const AudioEngine *a)
{
    return a->rec_frames;
}

bool audio_start_recording(AudioEngine *a, const char *path, char *err, size_t errcap)
{
    if (a->state == AUDIO_STATE_RECORDING) {
        snprintf(err, errcap, "already recording");
        return false;
    }

    SDL_AudioDeviceID want = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
    if (a->input_index > 0 && a->inputs != NULL && a->input_index <= a->input_count) {
        want = a->inputs[a->input_index - 1];
    }

    SDL_AudioSpec spec = capture_spec();
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(want, &spec);
    if (dev == 0) {
        snprintf(err, errcap, "open recording device: %s", SDL_GetError());
        return false;
    }

    SDL_AudioStream *stream = SDL_CreateAudioStream(&spec, &spec);
    if (stream == NULL) {
        snprintf(err, errcap, "create capture stream: %s", SDL_GetError());
        SDL_CloseAudioDevice(dev);
        return false;
    }
    if (!SDL_BindAudioStream(dev, stream)) {
        snprintf(err, errcap, "bind capture stream: %s", SDL_GetError());
        SDL_DestroyAudioStream(stream);
        SDL_CloseAudioDevice(dev);
        return false;
    }

    WavWriter wav;
    if (!wav_writer_open(&wav, path, AUDIO_CHANNELS, AUDIO_SAMPLE_RATE, 16, false,
                         err, errcap)) {
        SDL_UnbindAudioStream(stream);
        SDL_DestroyAudioStream(stream);
        SDL_CloseAudioDevice(dev);
        return false;
    }

    a->rec_dev = dev;
    a->rec_stream = stream;
    a->rec_wav = wav;
    a->rec_wav_open = true;
    a->rec_frames = 0;
    a->state = AUDIO_STATE_RECORDING;

    if (!SDL_ResumeAudioDevice(dev)) {
        snprintf(err, errcap, "resume recording device: %s", SDL_GetError());
        close_recording(a);
        return false;
    }
    return true;
}

bool audio_stop_recording(AudioEngine *a, uint64_t *frames, char *err, size_t errcap)
{
    if (a->state != AUDIO_STATE_RECORDING) {
        snprintf(err, errcap, "not recording");
        return false;
    }
    SDL_PauseAudioDevice(a->rec_dev);
    pump_recording(a);
    bool ok = a->rec_wav_open ? wav_writer_close(&a->rec_wav) : false;
    if (!ok) {
        snprintf(err, errcap, "finalize WAV failed");
    }
    uint64_t count = a->rec_frames;
    close_recording(a);
    if (frames != NULL) {
        *frames = count;
    }
    return ok;
}

static SDL_AudioFormat wav_format_to_sdl(const WavAudio *wa, char *err, size_t errcap)
{
    if (wa->bits_per_sample == 8) {
        return SDL_AUDIO_U8;
    }
    if (wa->bits_per_sample == 16) {
        return SDL_AUDIO_S16LE;
    }
    if (wa->bits_per_sample == 32) {
        return wa->is_float ? SDL_AUDIO_F32LE : SDL_AUDIO_S32LE;
    }
    snprintf(err, errcap, "unsupported sample width %u", wa->bits_per_sample);
    return 0;
}

bool audio_play(AudioEngine *a, const char *path, char *err, size_t errcap)
{
    WavAudio wa;
    if (!wav_read(path, &wa, err, errcap)) {
        return false;
    }
    SDL_AudioFormat src_fmt = wav_format_to_sdl(&wa, err, errcap);
    if (src_fmt == 0) {
        wav_audio_free(&wa);
        return false;
    }
    SDL_AudioSpec src;
    src.format = src_fmt;
    src.channels = wa.channels;
    src.freq = (int)wa.sample_rate;

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (dev == 0) {
        snprintf(err, errcap, "open playback device: %s", SDL_GetError());
        wav_audio_free(&wa);
        return false;
    }
    SDL_AudioSpec dst;
    if (!SDL_GetAudioDeviceFormat(dev, &dst, NULL)) {
        snprintf(err, errcap, "playback device format: %s", SDL_GetError());
        SDL_CloseAudioDevice(dev);
        wav_audio_free(&wa);
        return false;
    }

    SDL_AudioStream *stream = SDL_CreateAudioStream(&src, &dst);
    if (stream == NULL) {
        snprintf(err, errcap, "create playback stream: %s", SDL_GetError());
        SDL_CloseAudioDevice(dev);
        wav_audio_free(&wa);
        return false;
    }
    if (wa.data_bytes > 0 &&
        !SDL_PutAudioStreamData(stream, wa.data, (int)wa.data_bytes)) {
        snprintf(err, errcap, "queue playback: %s", SDL_GetError());
        SDL_DestroyAudioStream(stream);
        SDL_CloseAudioDevice(dev);
        wav_audio_free(&wa);
        return false;
    }
    if (!SDL_FlushAudioStream(stream)) {
        snprintf(err, errcap, "flush playback stream: %s", SDL_GetError());
        SDL_DestroyAudioStream(stream);
        SDL_CloseAudioDevice(dev);
        wav_audio_free(&wa);
        return false;
    }
    wav_audio_free(&wa); /* SDL_PutAudioStreamData made its own copy */

    if (!SDL_BindAudioStream(dev, stream)) {
        snprintf(err, errcap, "bind playback stream: %s", SDL_GetError());
        SDL_DestroyAudioStream(stream);
        SDL_CloseAudioDevice(dev);
        return false;
    }
    if (!SDL_ResumeAudioDevice(dev)) {
        snprintf(err, errcap, "resume playback device: %s", SDL_GetError());
        close_playback(a);
        return false;
    }

    a->play_dev = dev;
    a->play_stream = stream;
    a->state = AUDIO_STATE_PLAYING;
    return true;
}

void audio_pause(AudioEngine *a)
{
    if (a->state == AUDIO_STATE_PLAYING) {
        SDL_PauseAudioDevice(a->play_dev);
        a->state = AUDIO_STATE_PAUSED;
    }
}

void audio_resume(AudioEngine *a)
{
    if (a->state == AUDIO_STATE_PAUSED) {
        SDL_ResumeAudioDevice(a->play_dev);
        a->state = AUDIO_STATE_PLAYING;
    }
}

void audio_stop_playback(AudioEngine *a)
{
    if (a->state == AUDIO_STATE_PLAYING || a->state == AUDIO_STATE_PAUSED) {
        SDL_PauseAudioDevice(a->play_dev);
        close_playback(a);
    }
}

void audio_update(AudioEngine *a)
{
    if (a->state == AUDIO_STATE_RECORDING) {
        pump_recording(a);
    } else if (a->state == AUDIO_STATE_PLAYING && a->play_stream != NULL) {
        if (SDL_GetAudioStreamQueued(a->play_stream) <= 0) {
            close_playback(a);
        }
    }
}