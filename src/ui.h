#ifndef ANGELSOUND_UI_H
#define ANGELSOUND_UI_H

/* Minimal immediate-mode UI drawn with SDL_Renderer + SDL_RenderDebugText
 * (the 8x8 built-in font).  All coordinates are in a 720x460 logical space
 * that SDL letterboxes onto the window.  Layout/state helpers live in
 * ui_layout.h (SDL-free, unit-tested). */

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>

#include "ui_layout.h"

typedef struct Ui Ui;

bool ui_create(SDL_Window *window, SDL_Renderer *renderer, Ui **out, char *err,
               size_t errcap);
void ui_destroy(Ui *ui);
UiAction ui_handle_event(Ui *ui, SDL_Event *e, UiModel *m);
void ui_set_status(Ui *ui, const char *text);
void ui_set_error(Ui *ui, const char *text);
void ui_render(Ui *ui, UiModel *m);

#endif /* ANGELSOUND_UI_H */