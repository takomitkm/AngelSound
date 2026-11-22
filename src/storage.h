#ifndef ANGELSOUND_STORAGE_H
#define ANGELSOUND_STORAGE_H

/* Recordings location: a filesystem directory on desktop, an app-specific
 * directory on Android, or (after picking one) an Android SAF tree URI.
 * Pure string helpers live in storage_pure.h and are unit-tested. */

#include <stdbool.h>
#include <stddef.h>

#include "recordlist.h"

bool storage_init(char *err, size_t errcap);
void storage_shutdown(void);

const char *storage_display(void);
bool storage_list(RecordList *out, char *err, size_t errcap);
bool storage_make_name(char *name, size_t cap);
bool storage_take_path(const char *name, char *path, size_t cap, char *err,
                       size_t errcap);
bool storage_commit_take(const char *name, const char *path, char *err, size_t errcap);
bool storage_playback_path(const char *name, char *path, size_t cap, char *err,
                           size_t errcap);
bool storage_delete(const char *name, char *err, size_t errcap);

bool storage_choose_folder(void);
/* 0 = no result, 1 = location changed, -1 = pick failed */
int storage_poll_folder(void);

#endif /* ANGELSOUND_STORAGE_H */