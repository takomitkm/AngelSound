#ifndef ANGELSOUND_ANDROID_STORAGE_H
#define ANGELSOUND_ANDROID_STORAGE_H

#if defined(__ANDROID__)

#include <stdbool.h>
#include <stddef.h>

/* Thin JNI layer over AngelSoundActivity's SAF helpers. */
bool android_storage_pick_folder(void);
/* NULL = no answer yet, "" = canceled, otherwise a malloc'd tree URI. */
char *android_storage_take_picked_uri(void);
/* Public fallback folder path, or NULL/"" when it is not usable. */
char *android_storage_public_folder(void);
bool android_storage_list(const char *uri, char ***names, int *count);
bool android_storage_exists(const char *uri, const char *name);
bool android_storage_read_to_cache(const char *uri, const char *name, const char *dest);
bool android_storage_write_from_cache(const char *uri, const char *name, const char *src);
bool android_storage_delete(const char *uri, const char *name);

/* Runtime RECORD_AUDIO permission. */
bool android_request_microphone(void);
/* 0 = pending/unknown, 1 = granted, -1 = denied */
int android_microphone_state(void);

#endif /* __ANDROID__ */

#endif /* ANGELSOUND_ANDROID_STORAGE_H */