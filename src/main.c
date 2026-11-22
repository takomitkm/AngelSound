#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "practice.h"
#include "ui.h"

#define REC_NAME_CAP 64
#define PATH_CAP 1024

typedef struct {
    char **files;
    int count;
} FileList;

/* Recordings folder, persisted in SDL's per-user pref dir. */
static char g_recordings_dir[PATH_CAP];
static char g_folder_label[PATH_CAP + 16];
static char g_picked_dir[PATH_CAP];
static bool g_folder_picked;
static bool g_folder_pick_failed;

static void settings_path(char *out, size_t cap)
{
    char *pref = SDL_GetPrefPath("Aimo", "AngelSound");
    if (pref == NULL) {
        out[0] = '\0';
        return;
    }
    snprintf(out, cap, "%s/settings.txt", pref);
    SDL_free(pref);
}

static void default_recordings_dir(char *out, size_t cap)
{
    const char *env = getenv("ANGELSOUND_DIR");
    if (env != NULL && *env != '\0') {
        snprintf(out, cap, "%s", env);
        return;
    }
    const char *music = SDL_GetUserFolder(SDL_FOLDER_MUSIC);
    if (music != NULL && *music != '\0') {
        snprintf(out, cap, "%s/AngelSound", music);
    } else {
        snprintf(out, cap, "AngelSound");
    }
}

static void load_settings(void)
{
    g_recordings_dir[0] = '\0';

    const char *env = getenv("ANGELSOUND_DIR");
    if (env != NULL && *env != '\0' && SDL_CreateDirectory(env)) {
        snprintf(g_recordings_dir, sizeof(g_recordings_dir), "%s", env);
        return;
    }

    char sp[PATH_CAP];
    settings_path(sp, sizeof(sp));
    if (sp[0] != '\0') {
        size_t size = 0;
        char *data = SDL_LoadFile(sp, &size);
        if (data != NULL) {
            static const char key[] = "recordings_dir=";
            size_t key_len = sizeof(key) - 1;
            if (size >= key_len && memcmp(data, key, key_len) == 0) {
                size_t len = size - key_len;
                while (len > 0 &&
                       (data[key_len + len - 1] == '\n' || data[key_len + len - 1] == '\r')) {
                    len--;
                }
                if (len > 0 && len < sizeof(g_recordings_dir)) {
                    memcpy(g_recordings_dir, data + key_len, len);
                    g_recordings_dir[len] = '\0';
                }
            }
            SDL_free(data);
        }
    }

    if (g_recordings_dir[0] == '\0') {
        default_recordings_dir(g_recordings_dir, sizeof(g_recordings_dir));
    }
    if (!SDL_CreateDirectory(g_recordings_dir)) {
        default_recordings_dir(g_recordings_dir, sizeof(g_recordings_dir));
        if (!SDL_CreateDirectory(g_recordings_dir)) {
            snprintf(g_recordings_dir, sizeof(g_recordings_dir), ".");
        }
    }
}

static void save_settings(void)
{
    char sp[PATH_CAP];
    settings_path(sp, sizeof(sp));
    if (sp[0] == '\0') {
        return;
    }
    char buf[PATH_CAP + 32];
    int len = snprintf(buf, sizeof(buf), "recordings_dir=%s\n", g_recordings_dir);
    if (len > 0) {
        SDL_SaveFile(sp, buf, (size_t)len);
    }
}

static void dir_path(char *out, size_t cap, const char *name)
{
    snprintf(out, cap, "%s/%s", g_recordings_dir, name);
}

static bool path_exists(const char *path)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info);
}

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
    char **items = SDL_GlobDirectory(g_recordings_dir, "ASREC_*.wav", 0, &n);
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
        char full[PATH_CAP];
        dir_path(full, sizeof(full), buf);
        if (!path_exists(full)) {
            return; /* free name */
        }
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
                             "Recordings are saved as ASREC_*.wav in the recordings folder "
                             "(default: Music/AngelSound; use Folder to change).\n"
                             "Capture uses the microphone's native format with no "
                             "resampling or format conversion.\n\n"
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

static void SDLCALL folder_dialog_cb(void *userdata, const char *const *filelist, int filter)
{
    (void)userdata;
    (void)filter;
    if (filelist == NULL || filelist[0] == NULL) {
        g_folder_pick_failed = true;
        return;
    }
    snprintf(g_picked_dir, sizeof(g_picked_dir), "%s", filelist[0]);
    g_folder_picked = true;
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
    if (window == NULL && !smoke) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer *renderer = window != NULL ? SDL_CreateRenderer(window, NULL) : NULL;
    if (renderer != NULL) {
        SDL_SetRenderLogicalPresentation(renderer, UI_LOGICAL_W, UI_LOGICAL_H,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
    } else if (!smoke) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }
    if (smoke) {
        printf("smoke: video=%s renderer=%s\n",
               SDL_GetCurrentVideoDriver() != NULL ? SDL_GetCurrentVideoDriver() : "none",
               renderer != NULL ? "yes" : "no");
        fflush(stdout);
    }

    char err[256];
    AudioEngine *engine = NULL;
    if (!audio_init(&engine, err, sizeof(err))) {
        fprintf(stderr, "audio_init: %s\n", err);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "AngelSound", err, window);
        return 1;
    }

    Ui *ui = NULL;
    if (renderer != NULL && !ui_create(window, renderer, &ui, err, sizeof(err))) {
        fprintf(stderr, "ui_create: %s\n", err);
        if (!smoke) {
            return 1;
        }
    }

    FileList list = { 0 };
    load_settings();
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
            UiAction action = ui != NULL ? ui_handle_event(ui, &e, &model) : UI_ACTION_NONE;
            switch (action) {
            case UI_ACTION_QUIT:
                running = false;
                break;
            case UI_ACTION_RECORD:
                if (audio_state(engine) == AUDIO_STATE_IDLE) {
                    char rec_path[PATH_CAP];
                    recording_name(rec_name, sizeof(rec_name));
                    dir_path(rec_path, sizeof(rec_path), rec_name);
                    if (audio_start_recording(engine, rec_path, err, sizeof(err))) {
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
                        format_seconds(frames, (uint32_t)audio_record_rate(engine), dur, sizeof(dur));
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
                        char play_path[PATH_CAP];
                        dir_path(play_path, sizeof(play_path), list.files[model.selected]);
                        if (!audio_play(engine, play_path, err, sizeof(err))) {
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
                open_directory(g_recordings_dir);
                break;
            case UI_ACTION_DELETE:
                if (model.selected >= 0 && model.selected < list.count) {
                    char victim[256];
                    snprintf(victim, sizeof(victim), "%s", list.files[model.selected]);
                    if (confirm_delete(window, victim)) {
                        char victim_path[PATH_CAP];
                        dir_path(victim_path, sizeof(victim_path), victim);
                        audio_stop_playback(engine);
                        if (!SDL_RemovePath(victim_path)) {
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
            case UI_ACTION_CHOOSE_FOLDER:
                SDL_ShowOpenFolderDialog(folder_dialog_cb, NULL, window,
                                         g_recordings_dir, false);
                break;
            case UI_ACTION_ABOUT:
                show_about(window);
                break;
            case UI_ACTION_NONE:
            default:
                break;
            }
        }

        if (g_folder_picked) {
            g_folder_picked = false;
            if (SDL_CreateDirectory(g_picked_dir)) {
                snprintf(g_recordings_dir, sizeof(g_recordings_dir), "%s", g_picked_dir);
                save_settings();
                refresh_files(&list);
                model.selected = -1;
                ui_set_status(ui, "Recordings folder changed");
            } else {
                ui_set_error(ui, "Cannot use that folder");
            }
        }
        if (g_folder_pick_failed) {
            g_folder_pick_failed = false;
            ui_set_error(ui, "Folder dialog unavailable on this system");
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
        snprintf(g_folder_label, sizeof(g_folder_label), "Folder: %s", g_recordings_dir);
        model.folder_label = g_folder_label;
        refresh_input_label(engine);
        model.input_label = g_input_label;
        if (practice_should_warn(&practice, now)) {
            practice_mark_warned(&practice);
            ui_set_status(ui, "You have practiced for 5+ minutes - take a break.");
        }
        model.practice_warn = practice_current_ms(&practice, now) >= PRACTICE_WARN_MS;

        if (ui != NULL && renderer != NULL) {
            ui_render(ui, &model);
        }

        if (smoke && ++smoke_frames >= 5) {
            running = false;
        } else {
            SDL_Delay(10);
        }
    }

    audio_shutdown(engine);
    if (ui != NULL) {
        ui_destroy(ui);
    }
    filelist_free(&list);
    if (renderer != NULL) {
        SDL_DestroyRenderer(renderer);
    }
    if (window != NULL) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
    return 0;
}