#ifndef ANGELSOUND_UI_LAYOUT_H
#define ANGELSOUND_UI_LAYOUT_H

/* Pure layout/state helpers shared by the renderer and the tests.  No SDL
 * dependency here so the unit tests can check enablement and hit boxes. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio.h"

#define UI_LOGICAL_W 720
#define UI_LOGICAL_H 460
/* Portrait canvas: phones hold the app upright, so the list grows and the
 * buttons move into a grid underneath it. */
#define UI_PORTRAIT_H 1280
#define UI_LAYOUT_CAP 12

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
    float x;
    float y;
    float w;
    float h;
} UiRect;

typedef struct {
    UiRect rect;
    const char *label;
    UiAction action;
    bool enabled;
    int style; /* 0 normal, 1 record, 2 stop, 3 play */
} UiButton;

typedef struct {
    const char *const *files;
    int file_count;
    int selected;
    int scroll;

    const char *input_label;
    const char *folder_label;
    AudioState audio_state;
    uint64_t recording_ms;
    uint64_t session_ms;
    uint64_t last_take_ms;
    int takes;
    bool practice_warn;
} UiModel;

UiRect ui_list_rect(void);
float ui_row_height(void);
int ui_visible_rows(void);
void ui_clamp_selection(UiModel *m);
/* Portrait switches the canvas height and moves the buttons under the list. */
void ui_set_portrait(bool portrait);
/* Portrait canvas height, normally derived from the window aspect. */
void ui_set_portrait_size(float canvas_h);
bool ui_is_portrait(void);
float ui_canvas_h(void);
/* Right-hand column used by the landscape layout; equal to the canvas width
 * in portrait, where the stats block sits under the buttons. */
float ui_right_x(void);
float ui_right_w(void);
float ui_stats_y(void);
int ui_layout_buttons(const UiModel *m, UiButton *buttons, int cap);
UiAction ui_hit_action(const UiModel *m, float x, float y, int *selected);
int ui_clip_text(const char *text, char *out, size_t cap, int max_chars);

#endif /* ANGELSOUND_UI_LAYOUT_H */