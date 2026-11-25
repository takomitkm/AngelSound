#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "practice.h"
#include "recordlist.h"
#include "storage.h"
#include "ui.h"

#if defined(__ANDROID__)
#include "android_storage.h"
#endif

#define REC_NAME_CAP 64
#define PATH_CAP 1024

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Mutex *lock;
    AudioEngine *engine;
    Ui *ui;

    Practice practice;
    RecordList list;
    UiModel model;

    char rec_name[REC_NAME_CAP];
    char rec_path[PATH_CAP];
    uint64_t rec_start_ticks;
    bool permission_pending;

    bool background_stopped;
    char background_name[REC_NAME_CAP];
} App;

static char g_input_label[160];
static char g_folder_label[PATH_CAP + 16];

static void refresh_input_label(const App *app)
{
    char name[128];
    audio_input_name(app->engine, audio_selected_input(app->engine), name, sizeof(name));
    snprintf(g_input_label, sizeof(g_input_label), "Input: %s", name);
}

static void refresh_files(App *app)
{
    char err[256];
    if (!storage_list(&app->list, err, sizeof(err))) {
        ui_set_error(app->ui, err);
    }
    ui_clamp_selection(&app->model);
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

static void start_recording(App *app)
{
    char e[256];
    SDL_LockMutex(app->lock);
    if (audio_state(app->engine) != AUDIO_STATE_IDLE) {
        SDL_UnlockMutex(app->lock);
        return;
    }
    if (!storage_make_name(app->rec_name, sizeof(app->rec_name))) {
        SDL_UnlockMutex(app->lock);
        ui_set_error(app->ui, "Cannot create a unique recording name");
        return;
    }
    if (!storage_take_path(app->rec_name, app->rec_path, sizeof(app->rec_path), e,
                           sizeof(e))) {
        SDL_UnlockMutex(app->lock);
        ui_set_error(app->ui, e);
        return;
    }
    if (!audio_start_recording(app->engine, app->rec_path, e, sizeof(e))) {
        SDL_UnlockMutex(app->lock);
        ui_set_error(app->ui, e);
        return;
    }
    app->rec_start_ticks = SDL_GetTicks();
    practice_take_start(&app->practice, app->rec_start_ticks);
    SDL_UnlockMutex(app->lock);
    char toast[192];
    snprintf(toast, sizeof(toast), "Recording %s ...", app->rec_name);
    ui_set_status(app->ui, toast);
}

static void stop_recording(App *app)
{
    char e[256];
    uint64_t frames = 0;
    SDL_LockMutex(app->lock);
    if (audio_state(app->engine) != AUDIO_STATE_RECORDING) {
        SDL_UnlockMutex(app->lock);
        return;
    }
    bool ok = audio_stop_recording(app->engine, &frames, e, sizeof(e));
    SDL_UnlockMutex(app->lock);
    practice_take_stop(&app->practice, SDL_GetTicks());
    if (!ok) {
        ui_set_error(app->ui, e);
        return;
    }
    if (!storage_commit_take(app->rec_name, app->rec_path, e, sizeof(e))) {
        ui_set_error(app->ui, e);
        return;
    }
    recordlist_add(&app->list, app->rec_name);
    recordlist_sort(&app->list);
    app->model.selected = recordlist_find(&app->list, app->rec_name);
    char dur[32], toast[192];
    format_seconds(frames, (uint32_t)audio_record_rate(app->engine), dur, sizeof(dur));
    if (audio_recording_limited(app->engine)) {
        snprintf(toast, sizeof(toast), "Saved %s (%s, truncated: 4 GB WAV limit)",
                 app->rec_name, dur);
    } else {
        snprintf(toast, sizeof(toast), "Saved %s (%s)", app->rec_name, dur);
    }
    ui_set_status(app->ui, toast);
}

static void cycle_input(App *app)
{
    audio_cycle_input(app->engine);
    refresh_input_label(app);
    ui_set_status(app->ui, g_input_label);
}

static void handle_play(App *app)
{
    if (app->model.selected < 0 || app->model.selected >= app->list.count) {
        return;
    }
    char path[PATH_CAP], e[256];
    const char *name = app->list.names[app->model.selected];
    if (!storage_playback_path(name, path, sizeof(path), e, sizeof(e))) {
        ui_set_error(app->ui, e);
        return;
    }
    bool ok = true;
    SDL_LockMutex(app->lock);
    if (audio_state(app->engine) == AUDIO_STATE_PAUSED) {
        audio_resume(app->engine);
        SDL_UnlockMutex(app->lock);
        ui_set_status(app->ui, "Resumed");
        return;
    }
    if (audio_state(app->engine) == AUDIO_STATE_PLAYING) {
        audio_stop_playback(app->engine);
    }
    ok = audio_play(app->engine, path, e, sizeof(e));
    SDL_UnlockMutex(app->lock);
    if (!ok) {
        ui_set_error(app->ui, e);
    }
}

static void handle_delete(App *app)
{
    if (app->model.selected < 0 || app->model.selected >= app->list.count) {
        return;
    }
    char victim[REC_NAME_CAP];
    snprintf(victim, sizeof(victim), "%s", app->list.names[app->model.selected]);
    if (!confirm_delete(app->window, victim)) {
        return;
    }
    SDL_LockMutex(app->lock);
    audio_stop_playback(app->engine);
    SDL_UnlockMutex(app->lock);
    char err[256];
    if (!storage_delete(victim, err, sizeof(err))) {
        ui_set_error(app->ui, err);
        return;
    }
    recordlist_remove(&app->list, app->model.selected);
    app->model.selected = -1;
    char toast[REC_NAME_CAP + 16];
    snprintf(toast, sizeof(toast), "Deleted %s", victim);
    ui_set_status(app->ui, toast);
}

static void handle_folder(App *app)
{
    if (storage_choose_folder()) {
        ui_set_status(app->ui, "Choose a recordings folder ...");
    } else {
        ui_set_error(app->ui, "Folder picker is busy or unavailable");
    }
}

static void handle_action(App *app, UiAction action)
{
    switch (action) {
    case UI_ACTION_RECORD:
        if (audio_state(app->engine) != AUDIO_STATE_IDLE) {
            break;
        }
#if defined(__ANDROID__)
        if (android_microphone_state() != 1) {
            if (android_request_microphone()) {
                app->permission_pending = true;
                ui_set_status(app->ui, "Waiting for microphone permission ...");
            } else {
                ui_set_error(app->ui, "Cannot request microphone permission");
            }
            break;
        }
#endif
        start_recording(app);
        break;
    case UI_ACTION_STOP:
        stop_recording(app);
        break;
    case UI_ACTION_PLAY:
        handle_play(app);
        break;
    case UI_ACTION_PAUSE:
        SDL_LockMutex(app->lock);
        audio_pause(app->engine);
        SDL_UnlockMutex(app->lock);
        break;
    case UI_ACTION_RESUME:
        SDL_LockMutex(app->lock);
        audio_resume(app->engine);
        SDL_UnlockMutex(app->lock);
        break;
    case UI_ACTION_BROWSE:
#if defined(__ANDROID__)
        ui_set_status(app->ui, "Use Folder to pick a shared recordings folder");
#else
        open_directory(storage_display());
#endif
        break;
    case UI_ACTION_DELETE:
        handle_delete(app);
        break;
    case UI_ACTION_CYCLE_INPUT:
        cycle_input(app);
        break;
    case UI_ACTION_CHOOSE_FOLDER:
        handle_folder(app);
        break;
    case UI_ACTION_ABOUT:
        show_about(app->window);
        break;
    case UI_ACTION_QUIT:
    case UI_ACTION_NONE:
    default:
        break;
    }
}

/* WILL_ENTER_BACKGROUND arrives on the Android UI thread, so serialize with
 * the main loop and finalize an in-flight take before the process sleeps. */
static bool event_watch(void *userdata, SDL_Event *e)
{
    App *app = userdata;
    if (e->type == SDL_EVENT_WILL_ENTER_BACKGROUND) {
        SDL_LockMutex(app->lock);
        if (audio_state(app->engine) == AUDIO_STATE_RECORDING) {
            uint64_t frames = 0;
            char err[256];
            if (audio_stop_recording(app->engine, &frames, err, sizeof(err))) {
                practice_take_stop(&app->practice, SDL_GetTicks());
                if (storage_commit_take(app->rec_name, app->rec_path, err, sizeof(err))) {
                    snprintf(app->background_name, sizeof(app->background_name), "%s",
                             app->rec_name);
                    app->background_stopped = true;
                }
            }
        }
        audio_stop_playback(app->engine);
        SDL_UnlockMutex(app->lock);
    }
    return true;
}

static bool g_presentation_set;
static int g_last_canvas_h;
#if defined(__ANDROID__)
/* Set once a folder pick has been observed, so the loop can end after the
 * picker result lands and the activity is gone. */
static bool g_picker_seen;
/* When the picker is gone and no result ever arrives (cancel path), give up
 * after this long instead of leaving the native loop running forever. */
static uint64_t g_activity_gone_since;
#define ACTIVITY_GONE_TIMEOUT_MS 60000
#endif

/* The manifest locks the activity to landscape, so the UI always uses the
 * fixed landscape canvas.  Keep the logical presentation in sync with the
 * surface.  Cheap enough to call every frame. */
static void apply_ui_mode(SDL_Window *window, SDL_Renderer *renderer)
{
    if (window == NULL || renderer == NULL) {
        return;
    }
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(window, &w, &h);
    if (w <= 0 || h <= 0) {
        return;
    }
    ui_set_portrait(false);
    int canvas_h = (int)ui_canvas_h();
    if (g_presentation_set && canvas_h == g_last_canvas_h) {
        return;
    }
    SDL_SetRenderLogicalPresentation(renderer, UI_LOGICAL_W, canvas_h,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    g_presentation_set = true;
    g_last_canvas_h = canvas_h;
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

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "AngelSound",
                                 "Failed to initialize SDL.", NULL);
        return 1;
    }

    App app;
    memset(&app, 0, sizeof(app));
    app.lock = SDL_CreateMutex();
    if (app.lock == NULL) {
        fprintf(stderr, "SDL_CreateMutex: %s\n", SDL_GetError());
        return 1;
    }

    #if defined(__ANDROID__)
    /* The manifest locks the activity to landscape, so the surface is a
     * landscape display.  Ask for the real display size rather than a fixed
     * guess; SDL resizes the window to the surface anyway. */
    int win_w = 1280;
    int win_h = 720;
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode *mode =
        display != 0 ? SDL_GetCurrentDisplayMode(display) : NULL;
    if (mode != NULL) {
        win_w = mode->w;
        win_h = mode->h;
    }
#else
    int win_w = 1080;
    int win_h = 690;
#endif
    app.window = SDL_CreateWindow("AngelSound - pseudo-voice practice recorder",
                                  win_w, win_h,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (app.window == NULL && !smoke) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    app.renderer =
        app.window != NULL ? SDL_CreateRenderer(app.window, NULL) : NULL;
    if (app.renderer != NULL) {
        SDL_SetRenderVSync(app.renderer, 1);
        apply_ui_mode(app.window, app.renderer);
    } else if (!smoke) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }
    if (smoke) {
        printf("smoke: video=%s renderer=%s\n",
               SDL_GetCurrentVideoDriver() != NULL ? SDL_GetCurrentVideoDriver() : "none",
               app.renderer != NULL ? "yes" : "no");
        fflush(stdout);
    }

    char err[256];
    if (!audio_init(&app.engine, err, sizeof(err))) {
        fprintf(stderr, "audio_init: %s\n", err);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "AngelSound", err, app.window);
        return 1;
    }
    if (app.renderer != NULL &&
        !ui_create(app.window, app.renderer, &app.ui, err, sizeof(err))) {
        fprintf(stderr, "ui_create: %s\n", err);
        if (!smoke) {
            return 1;
        }
    }
    if (!storage_init(err, sizeof(err))) {
        fprintf(stderr, "storage_init: %s\n", err);
        if (!smoke) {
            return 1;
        }
    }

    practice_init(&app.practice);
    recordlist_init(&app.list);
    refresh_files(&app);
    app.model.selected = -1;
    if (app.list.count > 0) {
        app.model.selected = 0;
    }

    SDL_AddEventWatch(event_watch, &app);
    bool running = true;
    int smoke_frames = 0;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
#if defined(__ANDROID__)
                /* The ROM tears down the activity while the SAF picker is on
                 * top, which sends QUIT.  Stay alive so the picker result can
                 * still reach us, then exit once the pick has resolved. */
                if (storage_folder_pending()) {
                    continue;
                }
#endif
                running = false;
                continue;
            }
            UiAction action = app.ui != NULL ? ui_handle_event(app.ui, &e, &app.model)
                                             : UI_ACTION_NONE;
            if (action == UI_ACTION_QUIT) {
                running = false;
            } else if (action != UI_ACTION_NONE) {
                handle_action(&app, action);
            }
        }

        int folder = storage_poll_folder();
#if defined(__ANDROID__)
        /* The picker is done and the activity is gone: end the loop so the
         * process exits cleanly instead of leaving a zombie SDL thread. */
        bool alive = android_storage_activity_alive();
        uint64_t now_ticks = SDL_GetTicks();
        if (g_picker_seen && !storage_folder_pending() && !alive) {
            running = false;
        } else if (g_picker_seen && !alive) {
            /* Picker still unresolved and the activity is gone: bail out if
             * it stays that way, so the process does not linger. */
            if (g_activity_gone_since == 0) {
                g_activity_gone_since = now_ticks;
            } else if (now_ticks - g_activity_gone_since > ACTIVITY_GONE_TIMEOUT_MS) {
                running = false;
            }
        } else {
            g_activity_gone_since = 0;
        }
        if (storage_folder_pending()) {
            g_picker_seen = true;
        }
#endif
        if (folder == 1) {
            refresh_files(&app);
            app.model.selected = app.list.count > 0 ? 0 : -1;
            ui_set_status(app.ui, "Recordings folder changed");
        } else if (folder < 0) {
            ui_set_error(app.ui, "Folder selection failed");
        }

#if defined(__ANDROID__)
        if (app.permission_pending) {
            int state = android_microphone_state();
            if (state == 1) {
                app.permission_pending = false;
                start_recording(&app);
            } else if (state < 0) {
                app.permission_pending = false;
                ui_set_error(app.ui, "Microphone permission denied");
            }
        }
#endif

        if (app.background_stopped) {
            app.background_stopped = false;
            refresh_files(&app);
            app.model.selected = recordlist_find(&app.list, app.background_name);
            ui_set_status(app.ui, "Recording saved before going to background");
        }

        SDL_LockMutex(app.lock);
        audio_update(app.engine);
        SDL_UnlockMutex(app.lock);

        /* A take that hit the 4 GB RIFF limit stops writing in place; stop
         * and save it so the truncated WAV is finalized and the user is told
         * (stop_recording reports "truncated" in its status line). */
        if (audio_state(app.engine) == AUDIO_STATE_RECORDING &&
            audio_recording_limited(app.engine)) {
            stop_recording(&app);
        }

        app.model.files = (const char *const *)app.list.names;
        app.model.file_count = app.list.count;
        app.model.audio_state = audio_state(app.engine);
        uint64_t now = SDL_GetTicks();
        app.model.recording_ms =
            app.model.audio_state == AUDIO_STATE_RECORDING ? now - app.rec_start_ticks : 0;
        app.model.session_ms = practice_current_ms(&app.practice, now);
        app.model.last_take_ms = app.practice.last_take_ms;
        app.model.takes = app.practice.takes;
        snprintf(g_folder_label, sizeof(g_folder_label), "Folder: %s",
                 storage_display());
        app.model.folder_label = g_folder_label;
        refresh_input_label(&app);
        app.model.input_label = g_input_label;
        if (practice_should_warn(&app.practice, now)) {
            practice_mark_warned(&app.practice);
            ui_set_status(app.ui, "You have practiced for 5+ minutes - take a break.");
        }
        app.model.practice_warn =
            practice_current_ms(&app.practice, now) >= PRACTICE_WARN_MS;

        if (app.ui != NULL && app.renderer != NULL) {
            apply_ui_mode(app.window, app.renderer);
            ui_render(app.ui, &app.model);
        }

        if (smoke && ++smoke_frames >= 5) {
            running = false;
        } else {
            SDL_Delay(10);
        }
    }

    SDL_RemoveEventWatch(event_watch, &app);
    audio_shutdown(app.engine);
    if (app.ui != NULL) {
        ui_destroy(app.ui);
    }
    recordlist_free(&app.list);
    storage_shutdown();
    if (app.renderer != NULL) {
        SDL_DestroyRenderer(app.renderer);
    }
    if (app.window != NULL) {
        SDL_DestroyWindow(app.window);
    }
    SDL_DestroyMutex(app.lock);
    SDL_Quit();
    return 0;
}