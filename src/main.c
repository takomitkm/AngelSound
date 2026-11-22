#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "practice.h"
#include "ui.h"

#define REC_NAME_CAP 64

typedef struct {
    char **files;
    int count;
} FileList;

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void filelist_free(FileList *list)
{
    for (int i = 0; i < list->count; i++) {
        free(list->files[i]);
    }
    free(list->files);
    list->files = NULL;
    list->count = 0;
}

static bool filelist_add(FileList *list, const char *name)
{
    char **grown = realloc(list->files, sizeof(char *) * (size_t)(list->count + 1));
    if (grown == NULL) {
        return false;
    }
    list->files = grown;
    list->files[list->count] = SDL_strdup(name);
    if (list->files[list->count] == NULL) {
        return false;
    }
    list->count++;
    return true;
}

static void filelist_remove(FileList *list, int index)
{
    if (index < 0 || index >= list->count) {
        return;
    }
    free(list->files[index]);
    memmove(&list->files[index], &list->files[index + 1],
            sizeof(char *) * (size_t)(list->count - index - 1));
    list->count--;
}

static void refresh_files(FileList *list)
{
    filelist_free(list);
    int n = 0;
    char **items = SDL_GlobDirectory(".", "ASREC_*.wav", 0, &n);
    if (items != NULL) {
        for (int i = 0; i < n; i++) {
            filelist_add(list, items[i]);
        }
        SDL_free(items);
    }
    if (list->count > 1) {
        qsort(list->files, (size_t)list->count, sizeof(char *), cmp_str);
    }
}

static void recording_name(char *buf, size_t cap)
{
    char stamp[32];
    SDL_Time ticks = 0;
    SDL_DateTime dt;
    memset(&dt, 0, sizeof(dt));
    if (!SDL_GetCurrentTime(&ticks) || !SDL_TimeToDateTime(ticks, &dt, true)) {
        snprintf(stamp, sizeof(stamp), "%u", (unsigned)SDL_GetTicks());
    } else {
        snprintf(stamp, sizeof(stamp), "%04d%02d%02d-%02d%02d%02d",
                 dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    }
    snprintf(buf, cap, "ASREC_%s.wav", stamp);
    for (int n = 1; n < 100; n++) {
        FILE *f = fopen(buf, "rb");
        if (f == NULL) {
            return; /* free name */
        }
        fclose(f);
        snprintf(buf, cap, "ASREC_%s-%d.wav", stamp, n);
    }
}

static void open_directory(const char *path)
{
#ifdef _WIN32
    const char *args[] = { "explorer.exe", path, NULL };
#elif defined(__APPLE__)
    const char *args[] = { "open", path, NULL };
#else
    const char *args[] = { "xdg-open", path, NULL };
#endif
    SDL_Process *proc = SDL_CreateProcess(args, false);
    if (proc != NULL) {
        SDL_DestroyProcess(proc);
    }
}

static void show_about(SDL_Window *window)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "About AngelSound",
                             "AngelSound - pseudo-voice practice recorder\n"
                             "C + SDL3 rewrite of MoeDisk/AngelSound (Delphi).\n\n"
                             "Recordings are saved as ASREC_*.wav in the app's "
                             "working directory (16-bit mono 44.1 kHz).\n\n"
                             "Keys: R record · S stop · Space play/pause · "
                             "Del delete · B browse",
                             window);
}

static bool confirm_delete(SDL_Window *window, const char *name)
{
    const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel" },
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Delete" },
    };
    char msg[512];
    snprintf(msg, sizeof(msg), "Delete '%s'?\nThis cannot be undone.", name);
    SDL_MessageBoxData mb;
    memset(&mb, 0, sizeof(mb));
    mb.flags = SDL_MESSAGEBOX_WARNING;
    mb.window = window;
    mb.title = "Delete recording";
    mb.message = msg;
    mb.numbuttons = 2;
    mb.buttons = buttons;
    int id = 0;
    return SDL_ShowMessageBox(&mb, &id) && id == 1;
}

static void format_seconds(uint64_t frames, uint32_t rate, char *out, size_t cap)
{
    practice_format_time(rate > 0 ? frames * 1000 / rate : 0, out, cap);
}

static char g_input_label[160];

static void refresh_input_label(const AudioEngine *engine)
{
    char name[128];
    audio_input_name(engine, audio_selected_input(engine), name, sizeof(name));
    snprintf(g_input_label, sizeof(g_input_label), "Input: %s", name);
}

static int find_file(const FileList *list, const char *name)
{
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->files[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

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

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "AngelSound",
                                 "Failed to initialize SDL.", NULL);
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("AngelSound - pseudo-voice practice recorder",
                                          1080, 690,
                                          SDL_WINDOW_RESIZABLE |
                                              SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == NULL) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (renderer == NULL) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderLogicalPresentation(renderer, UI_LOGICAL_W, UI_LOGICAL_H,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);

    char err[256];
    AudioEngine *engine = NULL;
    if (!audio_init(&engine, err, sizeof(err))) {
        fprintf(stderr, "audio_init: %s\n", err);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "AngelSound", err, window);
        return 1;
    }

    Ui *ui = NULL;
    if (!ui_create(window, renderer, &ui, err, sizeof(err))) {
        fprintf(stderr, "ui_create: %s\n", err);
        return 1;
    }

    FileList list = { 0 };
    refresh_files(&list);

    Practice practice;
    practice_init(&practice);

    UiModel model;
    memset(&model, 0, sizeof(model));
    model.selected = -1;

    char rec_name[REC_NAME_CAP];
    uint64_t rec_start_ticks = 0;
    bool running = true;
    int smoke_frames = 0;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                running = false;
                continue;
            }
            UiAction action = ui_handle_event(ui, &e, &model);
            switch (action) {
            case UI_ACTION_QUIT:
                running = false;
                break;
            case UI_ACTION_RECORD:
                if (audio_state(engine) == AUDIO_STATE_IDLE) {
                    recording_name(rec_name, sizeof(rec_name));
                    if (audio_start_recording(engine, rec_name, err, sizeof(err))) {
                        practice_take_start(&practice, SDL_GetTicks());
                        rec_start_ticks = SDL_GetTicks();
                        char toast[192];
                        snprintf(toast, sizeof(toast), "Recording %s ...", rec_name);
                        ui_set_status(ui, toast);
                    } else {
                        ui_set_error(ui, err);
                    }
                }
                break;
            case UI_ACTION_STOP:
                if (audio_state(engine) == AUDIO_STATE_RECORDING) {
                    uint64_t frames = 0;
                    if (audio_stop_recording(engine, &frames, err, sizeof(err))) {
                        practice_take_stop(&practice, SDL_GetTicks());
                        filelist_add(&list, rec_name);
                        if (list.count > 1) {
                            qsort(list.files, (size_t)list.count, sizeof(char *), cmp_str);
                        }
                        model.selected = find_file(&list, rec_name);
                        char dur[32], toast[192];
                        format_seconds(frames, AUDIO_SAMPLE_RATE, dur, sizeof(dur));
                        snprintf(toast, sizeof(toast), "Saved %s (%s)", rec_name, dur);
                        ui_set_status(ui, toast);
                    } else {
                        practice_take_stop(&practice, SDL_GetTicks());
                        ui_set_error(ui, err);
                    }
                }
                break;
            case UI_ACTION_PLAY:
                if (model.selected >= 0 && model.selected < list.count) {
                    if (audio_state(engine) == AUDIO_STATE_PAUSED) {
                        audio_resume(engine);
                        ui_set_status(ui, "Resumed");
                    } else {
                        if (audio_state(engine) == AUDIO_STATE_PLAYING) {
                            audio_stop_playback(engine);
                        }
                        if (!audio_play(engine, list.files[model.selected], err,
                                        sizeof(err))) {
                            ui_set_error(ui, err);
                        }
                    }
                }
                break;
            case UI_ACTION_PAUSE:
                audio_pause(engine);
                break;
            case UI_ACTION_RESUME:
                audio_resume(engine);
                break;
            case UI_ACTION_BROWSE:
                open_directory(".");
                break;
            case UI_ACTION_DELETE:
                if (model.selected >= 0 && model.selected < list.count) {
                    char victim[256];
                    snprintf(victim, sizeof(victim), "%s", list.files[model.selected]);
                    if (confirm_delete(window, victim)) {
                        audio_stop_playback(engine);
                        if (!SDL_RemovePath(victim)) {
                            ui_set_error(ui, SDL_GetError());
                            break;
                        }
                        filelist_remove(&list, model.selected);
                        model.selected = -1;
                        char toast[320];
                        snprintf(toast, sizeof(toast), "Deleted %s", victim);
                        ui_set_status(ui, toast);
                    }
                }
                break;
            case UI_ACTION_CYCLE_INPUT:
                audio_cycle_input(engine);
                refresh_input_label(engine);
                ui_set_status(ui, g_input_label);
                break;
            case UI_ACTION_ABOUT:
                show_about(window);
                break;
            case UI_ACTION_NONE:
            default:
                break;
            }
        }

        audio_update(engine);

        /* keep the view model fresh */
        model.files = (const char *const *)list.files;
        model.file_count = list.count;
        model.audio_state = audio_state(engine);
        uint64_t now = SDL_GetTicks();
        model.recording_ms = model.audio_state == AUDIO_STATE_RECORDING
                                 ? now - rec_start_ticks
                                 : 0;
        model.session_ms = practice_current_ms(&practice, now);
        model.last_take_ms = practice.last_take_ms;
        model.takes = practice.takes;
        refresh_input_label(engine);
        model.input_label = g_input_label;
        if (practice_should_warn(&practice, now)) {
            practice_mark_warned(&practice);
            ui_set_status(ui, "You have practiced for 5+ minutes - take a break.");
        }
        model.practice_warn = practice_current_ms(&practice, now) >= PRACTICE_WARN_MS;

        ui_render(ui, &model);

        if (smoke && ++smoke_frames >= 5) {
            running = false;
        } else {
            SDL_Delay(10);
        }
    }

    audio_shutdown(engine);
    ui_destroy(ui);
    filelist_free(&list);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}