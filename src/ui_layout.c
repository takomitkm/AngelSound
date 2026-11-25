#include "ui_layout.h"

#include <stdio.h>
#include <string.h>

#define LIST_X 16.0f
#define LIST_Y 56.0f
#define LIST_W 400.0f
#define LIST_H 300.0f
#define ROW_H 20.0f
#define RIGHT_X 432.0f
#define RIGHT_W 272.0f

/* Portrait: the list spans the screen and the buttons live underneath it. */
#define PORTRAIT_LIST_W 688.0f
#define PORTRAIT_LIST_H 560.0f
#define PORTRAIT_BTN_Y 640.0f
#define PORTRAIT_BTN_H 52.0f
#define PORTRAIT_BIG_H 56.0f
#define PORTRAIT_GAP 16.0f
#define PORTRAIT_COL 332.0f

static bool g_portrait;

void ui_set_portrait(bool portrait)
{
    g_portrait = portrait;
}

bool ui_is_portrait(void)
{
    return g_portrait;
}

float ui_canvas_h(void)
{
    return g_portrait ? (float)UI_PORTRAIT_H : (float)UI_LOGICAL_H;
}

float ui_right_x(void)
{
    return g_portrait ? LIST_X : RIGHT_X;
}

float ui_right_w(void)
{
    return g_portrait ? PORTRAIT_LIST_W : RIGHT_W;
}

float ui_stats_y(void)
{
    return g_portrait ? 968.0f : 286.0f;
}

UiRect ui_list_rect(void)
{
    UiRect rect = { LIST_X, LIST_Y, g_portrait ? PORTRAIT_LIST_W : LIST_W,
                    g_portrait ? PORTRAIT_LIST_H : LIST_H };
    return rect;
}

float ui_row_height(void)
{
    return ROW_H;
}

int ui_visible_rows(void)
{
    UiRect lr = ui_list_rect();
    return (int)(lr.h / ROW_H);
}

void ui_clamp_selection(UiModel *m)
{
    if (m->file_count == 0) {
        m->selected = -1;
        m->scroll = 0;
        return;
    }
    if (m->selected >= m->file_count) {
        m->selected = m->file_count - 1;
    }
    if (m->selected < 0) {
        m->selected = 0;
    }
    int rows = ui_visible_rows();
    int max_scroll = m->file_count > rows ? m->file_count - rows : 0;
    if (m->scroll > max_scroll) {
        m->scroll = max_scroll;
    }
    if (m->scroll < 0) {
        m->scroll = 0;
    }
    if (m->selected < m->scroll) {
        m->scroll = m->selected;
    }
    if (m->selected >= m->scroll + rows) {
        m->scroll = m->selected - rows + 1;
    }
}

static void add_button(UiButton *buttons, int *count, float x, float y, float w,
                       float h, const char *label, UiAction action, bool enabled,
                       int style, int cap)
{
    if (*count >= cap) {
        return; /* caller's buffer is full; never write past it */
    }
    UiButton *b = &buttons[*count];
    b->rect.x = x;
    b->rect.y = y;
    b->rect.w = w;
    b->rect.h = h;
    b->label = label;
    b->action = action;
    b->enabled = enabled;
    b->style = style;
    (*count)++;
}

int ui_layout_buttons(const UiModel *m, UiButton *buttons, int cap)
{
    int n = 0;
    bool has_sel = m->selected >= 0 && m->selected < m->file_count;
    AudioState st = m->audio_state;

    if (g_portrait) {
        float y = PORTRAIT_BTN_Y;
        add_button(buttons, &n, LIST_X, y, PORTRAIT_COL, PORTRAIT_BIG_H, "Record",
                   UI_ACTION_RECORD, st == AUDIO_STATE_IDLE, 1, cap);
        add_button(buttons, &n, LIST_X + PORTRAIT_COL + PORTRAIT_GAP, y, PORTRAIT_COL,
                   PORTRAIT_BIG_H, "Stop", UI_ACTION_STOP,
                   st == AUDIO_STATE_RECORDING, 2, cap);
        y += PORTRAIT_BIG_H + PORTRAIT_GAP;
        add_button(buttons, &n, LIST_X, y, 210, PORTRAIT_BTN_H, "Play",
                   UI_ACTION_PLAY, has_sel, 3, cap);
        add_button(buttons, &n, LIST_X + 210 + 12, y, 210, PORTRAIT_BTN_H, "Pause",
                   UI_ACTION_PAUSE, st == AUDIO_STATE_PLAYING, 3, cap);
        add_button(buttons, &n, LIST_X + 434 + 10, y, 244, PORTRAIT_BTN_H, "Resume",
                   UI_ACTION_RESUME, st == AUDIO_STATE_PAUSED, 3, cap);
        y += PORTRAIT_BTN_H + PORTRAIT_GAP;
        add_button(buttons, &n, LIST_X, y, PORTRAIT_COL, PORTRAIT_BTN_H, "Browse",
                   UI_ACTION_BROWSE, true, 0, cap);
        add_button(buttons, &n, LIST_X + PORTRAIT_COL + PORTRAIT_GAP, y, PORTRAIT_COL,
                   PORTRAIT_BTN_H, "Delete", UI_ACTION_DELETE,
                   has_sel && st != AUDIO_STATE_RECORDING, 0, cap);
        y += PORTRAIT_BTN_H + PORTRAIT_GAP;
        add_button(buttons, &n, LIST_X, y, PORTRAIT_LIST_W, PORTRAIT_BTN_H,
                   m->input_label, UI_ACTION_CYCLE_INPUT, true, 0, cap);
        y += PORTRAIT_BTN_H + PORTRAIT_GAP;
        add_button(buttons, &n, LIST_X, y, PORTRAIT_COL, PORTRAIT_BTN_H, "Folder",
                   UI_ACTION_CHOOSE_FOLDER, true, 0, cap);
        add_button(buttons, &n, LIST_X + PORTRAIT_COL + PORTRAIT_GAP, y, PORTRAIT_COL,
                   PORTRAIT_BTN_H, "About", UI_ACTION_ABOUT, true, 0, cap);
        return n;
    }

    add_button(buttons, &n, RIGHT_X, 56, 128, 40, "Record", UI_ACTION_RECORD,
               st == AUDIO_STATE_IDLE, 1, cap);
    add_button(buttons, &n, RIGHT_X + 144, 56, 128, 40, "Stop", UI_ACTION_STOP,
               st == AUDIO_STATE_RECORDING, 2, cap);
    add_button(buttons, &n, RIGHT_X, 108, 84, 32, "Play", UI_ACTION_PLAY, has_sel, 3, cap);
    add_button(buttons, &n, RIGHT_X + 92, 108, 84, 32, "Pause", UI_ACTION_PAUSE,
               st == AUDIO_STATE_PLAYING, 3, cap);
    add_button(buttons, &n, RIGHT_X + 184, 108, 88, 32, "Resume", UI_ACTION_RESUME,
               st == AUDIO_STATE_PAUSED, 3, cap);
    add_button(buttons, &n, RIGHT_X, 152, 128, 32, "Browse", UI_ACTION_BROWSE, true, 0, cap);
    add_button(buttons, &n, RIGHT_X + 144, 152, 128, 32, "Delete", UI_ACTION_DELETE,
               has_sel && st != AUDIO_STATE_RECORDING, 0, cap);
    add_button(buttons, &n, RIGHT_X, 196, RIGHT_W, 32, m->input_label,
               UI_ACTION_CYCLE_INPUT, true, 0, cap);
    add_button(buttons, &n, RIGHT_X, 240, 128, 32, "Folder", UI_ACTION_CHOOSE_FOLDER,
               true, 0, cap);
    add_button(buttons, &n, RIGHT_X + 144, 240, 128, 32, "About", UI_ACTION_ABOUT, true,
               0, cap);
    return n;
}

static bool inside(const UiRect *r, float x, float y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

UiAction ui_hit_action(const UiModel *m, float x, float y, int *selected)
{
    UiButton buttons[UI_LAYOUT_CAP];
    int n = ui_layout_buttons(m, buttons, UI_LAYOUT_CAP);
    for (int i = 0; i < n; i++) {
        if (buttons[i].enabled && inside(&buttons[i].rect, x, y)) {
            return buttons[i].action;
        }
    }
    UiRect list = ui_list_rect();
    if (inside(&list, x, y)) {
        int idx = m->scroll + (int)((y - LIST_Y) / ROW_H);
        if (selected != NULL && idx >= 0 && idx < m->file_count) {
            *selected = idx;
        }
    }
    return UI_ACTION_NONE;
}

int ui_clip_text(const char *text, char *out, size_t cap, int max_chars)
{
    if (out == NULL || cap == 0) {
        return 0;
    }
    const char *src = text != NULL ? text : "";
    int len = (int)strlen(src);
    if (max_chars < 0) {
        max_chars = 0;
    }
    if (len <= max_chars && (size_t)len < cap) {
        snprintf(out, cap, "%s", src);
        return len;
    }
    int keep = max_chars - 3;
    if (keep < 0) {
        keep = 0;
    }
    if ((size_t)keep > cap - 1) {
        keep = (int)cap - 1;
    }
    if ((size_t)keep > cap - 4) {
        keep = (int)cap - 4;
    }
    memcpy(out, src, (size_t)keep);
    out[keep] = '\0';
    if (cap > (size_t)keep + 3) {
        memcpy(out + keep, "...", 3);
        out[keep + 3] = '\0';
    }
    return keep + 3;
}