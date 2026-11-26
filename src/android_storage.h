#ifndef ANGELSOUND_ANDROID_STORAGE_H
#define ANGELSOUND_ANDROID_STORAGE_H

#if defined(__ANDROID__)

#include <stdbool.h>

/* SAF file operations.  These are called by the engine exactly when a file
 * operation happens, never as a per-frame poll of Activity state. */
bool android_storage_list(const char *uri, char ***names, int *count);
bool android_storage_exists(const char *uri, const char *name);
bool android_storage_read_to_cache(const char *uri, const char *name, const char *dest);
bool android_storage_write_from_cache(const char *uri, const char *name, const char *src);
bool android_storage_delete(const char *uri, const char *name);

/* Java -> native pushes.  The Activity posts the chosen SAF URI or microphone
 * result to these; the engine consumes them on its own thread. */
bool android_storage_take_pending_folder(char **out);
void android_storage_show_picker(void);
void android_storage_queue_folder(const char *uri);
void android_storage_queue_microphone(bool granted);
bool android_storage_take_microphone_result(bool *granted);

/* Runtime RECORD_AUDIO permission. */
bool android_permission_granted(void);
bool android_request_microphone(void);

#endif /* __ANDROID__ */

#endif /* ANGELSOUND_ANDROID_STORAGE_H */
