#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "practice.h"

/* geometry (logical units) */
#define LIST_X 16.0f
#define LIST_Y 56.0f
#define LIST_W 400.0f
#define LIST_H 300.0f
#define ROW_H 20.0f
#define RIGHT_X 432.0f
#define RIGHT_W 272.0f

/* palette */
static const SDL_FRect g_list_rect = { LIST_X, LIST_Y, LIST_W, LIST_H };

typedef struct {
    SDL_FRect rect;
    const char *label;
    UiAction action;
    bool enabled;
    int style; /* 0 normal, 1 record(green), 2 stop(red), 3 play(blue) */
} UiButton;

struct Ui {
    SDL_Window *window;
    SDL_Renderer *renderer;
    float mx;
    float my;
    char status[192];
    Uint64 status_until;
    bool status_is_error;
};

static void fill_rect(SDL_Renderer *r, SDL_FRect rect, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    SDL_RenderFillRect(r, &rect);
}

static void stroke_rect(SDL_Renderer *r, SDL_FRect rect, Uint8 cr, Uint8 cg, Uint8 cb)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderRect(r, &rect);
}

static void draw_text(SDL_Renderer *r, float x, float y, const char *s, Uint8 cr, Uint8 cg, Uint8 cb)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderDebugText(r, x, y, s);
}

static void draw_text_clipped(SDL_Renderer *r, float x, float y, const char *s,
                              float max_w, Uint8 cr, Uint8 cg, Uint8 cb)
{
    float max_chars = max_w / (float)SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    char buf[128];
    size_t len = strlen(s);
    if ((float)len > max_chars) {
        len = (size_t)max_chars;
        if (len > sizeof(buf) - 4) {
            len = sizeof(buf) - 4;
        }
        if (len > 3) {
            memcpy(buf, s, len - 3);
            buf[len - 3] = '\0';
            strncat(buf, "...", sizeof(buf) - strlen(buf) - 1);
            s = buf;
        }
    }
    draw_text(r, x, y, s, cr, cg, cb);
}

static bool in_rect(const SDL_FRect *rect, float x, float y)
{
    return x >= rect->x && x < rect->x + rect->w && y >= rect->y && y < rect->y + rect->h;
}

static int visible_rows(void)
{
    return (int)(LIST_H / ROW_H);
}

static void clamp_view(UiModel *m)
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
    int max_scroll = m->file_count > visible_rows() ? m->file_count - visible_rows() : 0;
    if (m->scroll > max_scroll) {
        m->scroll = max_scroll;
    }
    if (m->scroll < 0) {
        m->scroll = 0;
    }
    /* keep the selection on screen */
    if (m->selected < m->scroll) {
        m->scroll = m->selected;
    }
    if (m->selected >= m->scroll + visible_rows()) {
        m->scroll = m->selected - visible_rows() + 1;
    }
}

static int layout_buttons(const UiModel *m, UiButton *buttons, int cap)
{
    int n = 0;
    bool has_sel = m->selected >= 0 && m->selected < m->file_count;
    AudioState st = m->audio_state;

    buttons[n++] = (UiButton){ { RIGHT_X, 56, 128, 40 }, "Record", UI_ACTION_RECORD,
                               st == AUDIO_STATE_IDLE, 1 };
    buttons[n++] = (UiButton){ { RIGHT_X + 144, 56, 128, 40 }, "Stop", UI_ACTION_STOP,
                               st == AUDIO_STATE_RECORDING, 2 };
    buttons[n++] = (UiButton){ { RIGHT_X, 108, 84, 32 }, "Play", UI_ACTION_PLAY,
                               has_sel, 3 };
    buttons[n++] = (UiButton){ { RIGHT_X + 92, 108, 84, 32 }, "Pause", UI_ACTION_PAUSE,
                               st == AUDIO_STATE_PLAYING, 3 };
    buttons[n++] = (UiButton){ { RIGHT_X + 184, 108, 88, 32 }, "Resume", UI_ACTION_RESUME,
                               st == AUDIO_STATE_PAUSED, 3 };
    buttons[n++] = (UiButton){ { RIGHT_X, 152, 128, 32 }, "Browse", UI_ACTION_BROWSE, true, 0 };
    buttons[n++] = (UiButton){ { RIGHT_X + 144, 152, 128, 32 }, "Delete", UI_ACTION_DELETE,
                               has_sel && st != AUDIO_STATE_RECORDING, 0 };
    buttons[n++] = (UiButton){ { RIGHT_X, 196, RIGHT_W, 32 }, m->input_label,
                               UI_ACTION_CYCLE_INPUT, true, 0 };
    buttons[n++] = (UiButton){ { RIGHT_X, 240, 128, 32 }, "About", UI_ACTION_ABOUT, true, 0 };
    (void)cap;
    return n;
}

/* Pause/Resume are real buttons; Space toggles play/pause/resume. */
static UiAction key_action(const UiModel *m, SDL_Keycode key)
{
    bool has_sel = m->selected >= 0 && m->selected < m->file_count;
    switch (key) {
    case SDLK_R:
        return m->audio_state == AUDIO_STATE_IDLE ? UI_ACTION_RECORD : UI_ACTION_NONE;
    case SDLK_S:
        return m->audio_state == AUDIO_STATE_RECORDING ? UI_ACTION_STOP : UI_ACTION_NONE;
    case SDLK_SPACE:
        if (m->audio_state == AUDIO_STATE_PLAYING) {
            return UI_ACTION_PAUSE;
        }
        if (m->audio_state == AUDIO_STATE_PAUSED) {
            return UI_ACTION_RESUME;
        }
        return has_sel ? UI_ACTION_PLAY : UI_ACTION_NONE;
    case SDLK_DELETE:
        return has_sel && m->audio_state != AUDIO_STATE_RECORDING ? UI_ACTION_DELETE
                                                                  : UI_ACTION_NONE;
    case SDLK_B:
        return UI_ACTION_BROWSE;
    case SDLK_ESCAPE:
        return UI_ACTION_QUIT;
    default:
        return UI_ACTION_NONE;
    }
}

bool ui_create(SDL_Window *window, SDL_Renderer *renderer, Ui **out, char *err, size_t errcap)
{
    Ui *ui = calloc(1, sizeof(*ui));
    if (ui == NULL) {
        snprintf(err, errcap, "out of memory");
        return false;
    }
    ui->window = window;
    ui->renderer = renderer;
    *out = ui;
    return true;
}

void ui_destroy(Ui *ui)
{
    free(ui);
}

void ui_set_status(Ui *ui, const char *text)
{
    snprintf(ui->status, sizeof(ui->status), "%s", text);
    ui->status_until = SDL_GetTicks() + 4000;
    ui->status_is_error = false;
}

void ui_set_error(Ui *ui, const char *text)
{
    snprintf(ui->status, sizeof(ui->status), "%s", text);
    ui->status_until = SDL_GetTicks() + 6000;
    ui->status_is_error = true;
}

UiAction ui_handle_event(Ui *ui, SDL_Event *e, UiModel *m)
{
    clamp_view(m);
    switch (e->type) {
    case SDL_EVENT_MOUSE_MOTION:
        SDL_ConvertEventToRenderCoordinates(ui->renderer, e);
        ui->mx = e->motion.x;
        ui->my = e->motion.y;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        SDL_ConvertEventToRenderCoordinates(ui->renderer, e);
        if (e->button.button != SDL_BUTTON_LEFT) {
            break;
        }
        float x = e->button.x;
        float y = e->button.y;
        UiButton buttons[16];
        int n = layout_buttons(m, buttons, 16);
        for (int i = 0; i < n; i++) {
            if (buttons[i].enabled && in_rect(&buttons[i].rect, x, y)) {
                return buttons[i].action;
            }
        }
        if (in_rect(&g_list_rect, x, y)) {
            int idx = m->scroll + (int)((y - LIST_Y) / ROW_H);
            if (idx >= 0 && idx < m->file_count) {
                m->selected = idx;
            }
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        SDL_ConvertEventToRenderCoordinates(ui->renderer, e);
        ui->mx = e->wheel.mouse_x;
        ui->my = e->wheel.mouse_y;
        if (in_rect(&g_list_rect, ui->mx, ui->my)) {
            m->scroll += (int)e->wheel.y;
            clamp_view(m);
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        if (e->key.key == SDLK_UP) {
            if (m->selected > 0) {
                m->selected--;
            }
            clamp_view(m);
        } else if (e->key.key == SDLK_DOWN) {
            if (m->selected < m->file_count - 1) {
                m->selected++;
            }
            clamp_view(m);
        } else {
            return key_action(m, e->key.key);
        }
        break;
    default:
        break;
    }
    return UI_ACTION_NONE;
}

static void draw_button(Ui *ui, const UiButton *b, const UiModel *m)
{
    SDL_Renderer *r = ui->renderer;
    bool hover = b->enabled && in_rect(&b->rect, ui->mx, ui->my);
    Uint8 fr = 44, fg = 48, fb = 60;
    if (b->style == 1) {
        fr = 40; fg = 130; fb = 78;
    } else if (b->style == 2) {
        fr = 160; fg = 62; fb = 62;
    } else if (b->style == 3) {
        fr = 52; fg = 94; fb = 170;
    }
    if (hover) {
        fr = (Uint8)(fr + 28);
        fg = (Uint8)(fg + 28);
        fb = (Uint8)(fb + 28);
    }
    fill_rect(r, b->rect, fr, fg, fb, 255);
    stroke_rect(r, b->rect, 90, 96, 116);
    Uint8 tr = b->enabled ? 236 : 120;
    float text_w = (float)strlen(b->label) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    float tx = b->rect.x + (b->rect.w - text_w) / 2.0f;
    if (tx < b->rect.x + 2.0f) {
        tx = b->rect.x + 2.0f;
    }
    float ty = b->rect.y + (b->rect.h - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) / 2.0f;
    draw_text_clipped(r, tx, ty, b->label, b->rect.w - 4.0f, tr, tr, tr);
    (void)m;
}

static void draw_list(Ui *ui, UiModel *m)
{
    SDL_Renderer *r = ui->renderer;
    fill_rect(r, g_list_rect, 30, 32, 40, 255);
    stroke_rect(r, g_list_rect, 70, 76, 92);

    clamp_view(m);
    int rows = visible_rows();
    int first = m->scroll;
    int last = first + rows < m->file_count ? first + rows : m->file_count;
    for (int i = first; i < last; i++) {
        SDL_FRect row = { LIST_X + 1, LIST_Y + 1 + (i - first) * ROW_H, LIST_W - 2, ROW_H };
        if (i == m->selected) {
            fill_rect(r, row, 46, 84, 150, 255);
        } else if ((i - first) % 2 == 1) {
            fill_rect(r, row, 34, 36, 45, 255);
        }
        char label[256];
        snprintf(label, sizeof(label), "%3d. %s", i + 1, m->files[i]);
        draw_text_clipped(r, row.x + 8, row.y + 6, label, LIST_W - 24, 226, 230, 240);
    }
    if (m->file_count == 0) {
        draw_text(r, LIST_X + 8, LIST_Y + 8, "(no recordings yet - press R to record)",
                  120, 128, 144);
    }

    /* scrollbar */
    if (m->file_count > rows) {
        float track_h = LIST_H - 2;
        float thumb_h = track_h * rows / m->file_count;
        float thumb_y = LIST_Y + 1 + track_h * m->scroll / m->file_count;
        SDL_FRect track = { LIST_X + LIST_W - 10, LIST_Y + 1, 8, track_h };
        SDL_FRect thumb = { track.x, thumb_y, 8, thumb_h };
        fill_rect(r, track, 24, 26, 32, 255);
        fill_rect(r, thumb, 90, 96, 116, 255);
    }
}

void ui_render(Ui *ui, UiModel *m)
{
    SDL_Renderer *r = ui->renderer;
    clamp_view(m);

    fill_rect(r, (SDL_FRect){ 0, 0, UI_LOGICAL_W, UI_LOGICAL_H }, 24, 26, 32, 255);

    draw_text(r, 16, 14, "AngelSound", 96, 165, 250);
    draw_text(r, 16 + 11 * 8 + 8, 14, "- pseudo-voice practice recorder", 150, 156, 170);
    if (m->input_label != NULL) {
        draw_text(r, 16, 30, m->input_label, 150, 156, 170);
    }
    if (m->audio_state == AUDIO_STATE_RECORDING) {
        char rec[32], rec_line[48];
        practice_format_time(m->recording_ms, rec, sizeof(rec));
        snprintf(rec_line, sizeof(rec_line), "REC %s", rec);
        draw_text(r, UI_LOGICAL_W - 88, 14, rec_line, 240, 96, 96);
    } else if (m->audio_state == AUDIO_STATE_PAUSED) {
        draw_text(r, UI_LOGICAL_W - 72, 14, "PAUSED", 230, 198, 110);
    }

    draw_list(ui, m);

    UiButton buttons[16];
    int n = layout_buttons(m, buttons, 16);
    for (int i = 0; i < n; i++) {
        draw_button(ui, &buttons[i], m);
    }

    /* practice stats + hints */
    char session[32], last[32], line[96];
    practice_format_time(m->session_ms, session, sizeof(session));
    practice_format_time(m->last_take_ms, last, sizeof(last));
    snprintf(line, sizeof(line), "Take %d · Session %s · Last %s", m->takes, session, last);
    draw_text(r, RIGHT_X, 286, line, 190, 196, 210);
    draw_text(r, RIGHT_X, 302, "Keys: R record · S stop · Space play", 120, 128, 144);
    draw_text(r, RIGHT_X, 316, "Up/Down select · Del delete · B browse", 120, 128, 144);

    if (m->practice_warn) {
        SDL_FRect warn = { RIGHT_X, 340, RIGHT_W, 44 };
        fill_rect(r, warn, 150, 96, 24, 255);
        stroke_rect(r, warn, 200, 140, 40);
        draw_text(r, RIGHT_X + 8, 350, "Practiced 5+ min.", 250, 226, 180);
        draw_text(r, RIGHT_X + 8, 364, "Take a break - your voice needs rest.", 250, 226, 180);
    }

    /* status toast */
    Uint64 now = SDL_GetTicks();
    if (ui->status[0] != '\0' && now < ui->status_until) {
        draw_text(r, 16, UI_LOGICAL_H - 22, ui->status,
                  ui->status_is_error ? 240 : 190, ui->status_is_error ? 120 : 196,
                  ui->status_is_error ? 120 : 250);
    }
    draw_text(r, UI_LOGICAL_W - 120, UI_LOGICAL_H - 22, "C + SDL3", 110, 116, 132);

    SDL_RenderPresent(r);
}