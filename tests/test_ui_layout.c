#include <stdio.h>
#include <string.h>

#include "ui_layout.h"

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static bool rect_overlaps(UiRect a, UiRect b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static const UiButton *find_button(const UiButton *buttons, int count, UiAction action)
{
    for (int i = 0; i < count; i++) {
        if (buttons[i].action == action) {
            return &buttons[i];
        }
    }
    return NULL;
}

int main(void)
{
    const char *files[] = { "a.wav", "b.wav", "c.wav" };
    UiModel m;
    memset(&m, 0, sizeof(m));
    m.files = (const char *const *)files;
    m.file_count = 3;
    m.selected = 0;
    m.input_label = "Input: System default";
    m.folder_label = "Folder: /tmp";
    m.audio_state = AUDIO_STATE_IDLE;

    UiButton buttons[UI_LAYOUT_CAP];
    int count = ui_layout_buttons(&m, buttons, UI_LAYOUT_CAP);
    CHECK(count == 10);

    for (int i = 0; i < count; i++) {
        CHECK(buttons[i].rect.x >= 0);
        CHECK(buttons[i].rect.y >= 0);
        CHECK(buttons[i].rect.x + buttons[i].rect.w <= UI_LOGICAL_W);
        CHECK(buttons[i].rect.y + buttons[i].rect.h <= UI_LOGICAL_H);
    }
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            CHECK(!rect_overlaps(buttons[i].rect, buttons[j].rect));
        }
    }

    const UiButton *record = find_button(buttons, count, UI_ACTION_RECORD);
    const UiButton *stop = find_button(buttons, count, UI_ACTION_STOP);
    const UiButton *play = find_button(buttons, count, UI_ACTION_PLAY);
    const UiButton *pause = find_button(buttons, count, UI_ACTION_PAUSE);
    const UiButton *resume = find_button(buttons, count, UI_ACTION_RESUME);
    const UiButton *del = find_button(buttons, count, UI_ACTION_DELETE);
    CHECK(record && record->enabled);
    CHECK(stop && !stop->enabled);
    CHECK(play && play->enabled);
    CHECK(pause && !pause->enabled);
    CHECK(resume && !resume->enabled);
    CHECK(del && del->enabled);

    m.audio_state = AUDIO_STATE_RECORDING;
    count = ui_layout_buttons(&m, buttons, UI_LAYOUT_CAP);
    CHECK(!find_button(buttons, count, UI_ACTION_RECORD)->enabled);
    CHECK(find_button(buttons, count, UI_ACTION_STOP)->enabled);
    CHECK(find_button(buttons, count, UI_ACTION_PLAY)->enabled);
    CHECK(!find_button(buttons, count, UI_ACTION_DELETE)->enabled);

    m.audio_state = AUDIO_STATE_PLAYING;
    count = ui_layout_buttons(&m, buttons, UI_LAYOUT_CAP);
    CHECK(find_button(buttons, count, UI_ACTION_PAUSE)->enabled);
    CHECK(!find_button(buttons, count, UI_ACTION_RESUME)->enabled);

    m.audio_state = AUDIO_STATE_PAUSED;
    count = ui_layout_buttons(&m, buttons, UI_LAYOUT_CAP);
    CHECK(!find_button(buttons, count, UI_ACTION_PAUSE)->enabled);
    CHECK(find_button(buttons, count, UI_ACTION_RESUME)->enabled);

    m.audio_state = AUDIO_STATE_IDLE;
    m.selected = -1;
    count = ui_layout_buttons(&m, buttons, UI_LAYOUT_CAP);
    CHECK(!find_button(buttons, count, UI_ACTION_PLAY)->enabled);
    CHECK(!find_button(buttons, count, UI_ACTION_DELETE)->enabled);

    m.selected = 0;
    count = ui_layout_buttons(&m, buttons, UI_LAYOUT_CAP);
    record = find_button(buttons, count, UI_ACTION_RECORD);
    CHECK(ui_hit_action(&m, record->rect.x + 5, record->rect.y + 5, NULL) ==
          UI_ACTION_RECORD);
    stop = find_button(buttons, count, UI_ACTION_STOP);
    CHECK(ui_hit_action(&m, stop->rect.x + 5, stop->rect.y + 5, NULL) ==
          UI_ACTION_NONE); /* disabled while idle */

    int selected = -1;
    UiRect list = ui_list_rect();
    CHECK(ui_hit_action(&m, list.x + 10, list.y + ui_row_height() * 1 + 5, &selected) ==
          UI_ACTION_NONE);
    CHECK(selected == 1);

    m.file_count = 0;
    selected = 7;
    CHECK(ui_hit_action(&m, list.x + 10, list.y + 5, &selected) == UI_ACTION_NONE);
    CHECK(selected == 7); /* untouched when there is no row */

    /* clamping */
    char names[30][10];
    const char *many[30];
    for (int i = 0; i < 30; i++) {
        snprintf(names[i], sizeof(names[i]), "f%02d.wav", i);
        many[i] = names[i];
    }
    m.files = (const char *const *)many;
    m.file_count = 30;
    m.selected = 29;
    m.scroll = 0;
    ui_clamp_selection(&m);
    CHECK(m.scroll == 30 - ui_visible_rows());
    m.selected = 0;
    m.scroll = 10;
    ui_clamp_selection(&m);
    CHECK(m.scroll == 0);
    m.selected = 99;
    ui_clamp_selection(&m);
    CHECK(m.selected == 29);
    m.file_count = 0;
    m.selected = 4;
    ui_clamp_selection(&m);
    CHECK(m.selected == -1 && m.scroll == 0);

    /* clipping */
    char buf[32];
    CHECK(ui_clip_text("short", buf, sizeof(buf), 10) == 5);
    CHECK(strcmp(buf, "short") == 0);
    CHECK(ui_clip_text("abcdefghij", buf, sizeof(buf), 6) == 6);
    CHECK(strcmp(buf, "abc...") == 0);
    CHECK(ui_clip_text("abcdef", buf, 4, 6) == 3);
    CHECK(strcmp(buf, "...") == 0);
    CHECK(ui_clip_text(NULL, buf, sizeof(buf), 5) == 0);

    if (failures == 0) {
        printf("test_ui_layout: PASS\n");
        return 0;
    }
    fprintf(stderr, "test_ui_layout: %d failure(s)\n", failures);
    return 1;
}