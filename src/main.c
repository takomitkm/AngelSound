#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <string.h>

#include "engine.h"

/* Desktop entry point: the whole app lives behind the engine API so the
 * Android Activity can drive the exact same code as lifecycle callbacks. */
int main(int argc, char **argv)
{
    bool smoke = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--smoke") == 0) {
            smoke = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("AngelSound - pseudo-voice practice recorder\n"
                   "Usage: AngelSound [--smoke]\n"
                   "  --smoke  run a few frames headlessly and exit (CI)\n");
            return 0;
        }
    }

    engine_set_smoke(smoke);
    if (engine_create(1080, 690) != 0) {
        return 1;
    }
    engine_start();
    engine_stop();
    return 0;
}
