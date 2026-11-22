#ifndef ANGELSOUND_AUDIO_H
#define ANGELSOUND_AUDIO_H

/* Cross-platform audio engine on SDL3: device enumeration, streaming
 * recording to a WAV file, and WAV playback with pause/resume.  SDL_INIT_AUDIO
 * must have been called (or will be called here) before audio_init().
 *
 * Recording uses the capture device's native format (sample rate, channels,
 * bit depth, integer/float) with no conversion stream, so the WAV is exactly
 * what the device produced. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wav.h"

typedef enum {
    AUDIO_STATE_IDLE,
    AUDIO_STATE_RECORDING,
    AUDIO_STATE_PLAYING,
    AUDIO_STATE_PAUSED
} AudioState;

typedef struct AudioEngine AudioEngine;

bool audio_init(AudioEngine **out, char *err, size_t errcap);
void audio_shutdown(AudioEngine *a);

/* 0 is always the system default device; 1..N are concrete capture devices. */
int audio_input_choices(const AudioEngine *a);
int audio_selected_input(const AudioEngine *a);
void audio_cycle_input(AudioEngine *a);
void audio_input_name(const AudioEngine *a, int index, char *buf, size_t cap);

AudioState audio_state(const AudioEngine *a);
uint64_t audio_record_frames(const AudioEngine *a);
/* Sample rate of the most recent capture (0 before the first recording). */
int audio_record_rate(const AudioEngine *a);

bool audio_start_recording(AudioEngine *a, const char *path, char *err, size_t errcap);
bool audio_stop_recording(AudioEngine *a, uint64_t *frames, char *err, size_t errcap);

bool audio_play(AudioEngine *a, const char *path, char *err, size_t errcap);
void audio_pause(AudioEngine *a);
void audio_resume(AudioEngine *a);
void audio_stop_playback(AudioEngine *a);

/* Call once per frame: drains recorded audio into the WAV file and detects
 * end of playback. */
void audio_update(AudioEngine *a);

#endif /* ANGELSOUND_AUDIO_H */