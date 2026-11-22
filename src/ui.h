#ifndef ANGELSOUND_UI_H
#define ANGELSOUND_UI_H

/* Minimal immediate-mode UI drawn with SDL_Renderer + SDL_RenderDebugText
 * (the 8x8 built-in font).  All coordinates are in a 720x460 logical space
 * that SDL letterboxes onto the window. */

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio.h"

#define UI_LOGICAL_W 720
#define UI_LOGICAL_H 460

typedef enum {
    UI_ACTION_NONE = 0,
    UI_ACTION_RECORD,
    UI_ACTION_STOP,
    UI_ACTION_PLAY,
    UI_ACTION_PAUSE,
    UI_ACTION_RESUME,
    UI_ACTION_BROWSE,
    UI_ACTION_DELETE,
    UI_ACTION_CYCLE_INPUT,
    UI_ACTION_CHOOSE_FOLDER,
    UI_ACTION_ABOUT,
    UI_ACTION_QUIT
} UiAction;

typedef struct {
    const char *const *files;
    int file_count;
    int selected;
    int scroll;

    const char *input_label; /* "Input: ..." composed by main */
    const char *folder_label; /* "Folder: ..." composed by main */
    AudioState audio_state;
    uint64_t recording_ms;
    uint64_t session_ms;
    uint64_t last_take_ms;
    int takes;
    bool practice_warn;
} UiModel;

typedef struct Ui Ui;

bool ui_create(SDL_Window *window, SDL_Renderer *renderer, Ui **out,
               char *err, size_t errcap);
void ui_destroy(Ui *ui);
UiAction ui_handle_event(Ui *ui, SDL_Event *e, UiModel *m);
void ui_set_status(Ui *ui, const char *text);
void ui_set_error(Ui *ui, const char *text);
void ui_render(Ui *ui, UiModel *m);

#endif /* ANGELSOUND_UI_H */