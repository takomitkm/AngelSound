#ifndef ANGELSOUND_ENGINE_H
#define ANGELSOUND_ENGINE_H

/* Native engine lifecycle.  On Android these are called from Activity
 * callbacks; on desktop `main()` uses the same functions as a thin wrapper. */

#include <stdbool.h>

void engine_set_smoke(bool enabled);
int  engine_create(int width, int height);
void engine_start(void);
void engine_pause(void);
void engine_resume(void);
void engine_stop(void);
/* Push a folder URI or path chosen by Java (Android) / dialog callback
 * (desktop).  The engine applies it immediately; no per-frame polling. */
void engine_set_folder(const char *uri_or_path);
/* Request the Java-side folder picker from native (Android only). */
void engine_request_folder_picker(void);

#endif /* ANGELSOUND_ENGINE_H */
