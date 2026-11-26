#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "engine.h"
#include "practice.h"
#include "recordlist.h"
#include "storage.h"
#include "storage_pure.h"
#include "ui.h"

#if defined(__ANDROID__)
#include <SDL3/SDL_main.h>
#include <jni.h>
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
    engine_request_folder_picker();
    ui_set_status(app->ui, "Choose a recordings folder ...");
}

static void handle_action(App *app, UiAction action)
{
    switch (action) {
    case UI_ACTION_RECORD:
        if (audio_state(app->engine) != AUDIO_STATE_IDLE) {
            break;
        }
#if defined(__ANDROID__)
        if (!android_permission_granted()) {
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

/* Engine process state.  On Android these globals live for the whole
 * Activity; on desktop main() owns the same create/start/stop sequence. */
static SDL_Mutex *g_push_lock;
static App g_app;
static bool g_created;
static bool g_smoke;
static bool g_running;
static bool g_stop_requested;
static bool g_paused;
static bool g_pause_handled;
static bool g_picker_open;
static int g_smoke_frames;

void engine_set_smoke(bool enabled)
{
    g_smoke = enabled;
}

static void push_lock(void)
{
    if (g_push_lock == NULL) {
        g_push_lock = SDL_CreateMutex();
    }
    if (g_push_lock != NULL) {
        SDL_LockMutex(g_push_lock);
    }
}

static void push_unlock(void)
{
    if (g_push_lock != NULL) {
        SDL_UnlockMutex(g_push_lock);
    }
}

static void apply_pending_folder(void)
{
#if defined(__ANDROID__)
    char *uri = NULL;
    if (!android_storage_take_pending_folder(&uri) || uri == NULL) {
        return;
    }
    char err[256];
    bool ok = storage_set_location(uri, storage_is_saf(uri), err, sizeof(err));
    free(uri);
#else
    if (!storage_poll_folder()) {
        return;
    }
    char err[256];
    bool ok = storage_apply_pending_folder();
    snprintf(err, sizeof(err), "Folder selection failed");
#endif
    if (!ok) {
        ui_set_error(g_app.ui, err);
    } else {
        refresh_files(&g_app);
        g_app.model.selected = g_app.list.count > 0 ? 0 : -1;
        ui_set_status(g_app.ui, "Recordings folder changed");
    }
}

int engine_create(int width, int height)
{
    if (g_created) {
        return 0;
    }
    memset(&g_app, 0, sizeof(g_app));
    g_smoke_frames = 0;
    g_paused = false;
    g_picker_open = false;
    g_running = false;
    g_stop_requested = false;

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "AngelSound",
                                 "Failed to initialize SDL.", NULL);
        return 1;
    }

    g_app.lock = SDL_CreateMutex();
    if (g_app.lock == NULL) {
        fprintf(stderr, "SDL_CreateMutex: %s\n", SDL_GetError());
        return 1;
    }

#if defined(__ANDROID__)
    int win_w = 1280;
    int win_h = 720;
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode *mode =
        display != 0 ? SDL_GetCurrentDisplayMode(display) : NULL;
    if (mode != NULL) {
        win_w = mode->w;
        win_h = mode->h;
    }
    (void)width;
    (void)height;
#else
    int win_w = width > 0 ? width : 1080;
    int win_h = height > 0 ? height : 690;
#endif
    g_app.window = SDL_CreateWindow("AngelSound - pseudo-voice practice recorder",
                                    win_w, win_h,
                                    SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (g_app.window == NULL && !g_smoke) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    g_app.renderer =
        g_app.window != NULL ? SDL_CreateRenderer(g_app.window, NULL) : NULL;
    if (g_app.renderer != NULL) {
        SDL_SetRenderVSync(g_app.renderer, 1);
        apply_ui_mode(g_app.window, g_app.renderer);
    } else if (!g_smoke) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }
    if (g_smoke) {
        printf("smoke: video=%s renderer=%s\n",
               SDL_GetCurrentVideoDriver() != NULL ? SDL_GetCurrentVideoDriver() : "none",
               g_app.renderer != NULL ? "yes" : "no");
        fflush(stdout);
    }

    char err[256];
    if (!audio_init(&g_app.engine, err, sizeof(err))) {
        fprintf(stderr, "audio_init: %s\n", err);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "AngelSound", err, g_app.window);
        return 1;
    }
    if (g_app.renderer != NULL &&
        !ui_create(g_app.window, g_app.renderer, &g_app.ui, err, sizeof(err))) {
        fprintf(stderr, "ui_create: %s\n", err);
        if (!g_smoke) {
            return 1;
        }
    }
    if (!storage_init(err, sizeof(err))) {
        fprintf(stderr, "storage_init: %s\n", err);
        if (!g_smoke) {
            return 1;
        }
    }

    practice_init(&g_app.practice);
    recordlist_init(&g_app.list);
    refresh_files(&g_app);
    g_app.model.selected = -1;
    if (g_app.list.count > 0) {
        g_app.model.selected = 0;
    }

    SDL_AddEventWatch(event_watch, &g_app);
    g_created = true;
    return 0;
}

void engine_request_stop(void)
{
    push_lock();
    g_stop_requested = true;
    push_unlock();
}

void engine_stop(void)
{
    if (!g_created) {
        return;
    }
    g_running = false;
    SDL_RemoveEventWatch(event_watch, &g_app);
    audio_shutdown(g_app.engine);
    if (g_app.ui != NULL) {
        ui_destroy(g_app.ui);
    }
    recordlist_free(&g_app.list);
    storage_shutdown();
    if (g_app.renderer != NULL) {
        SDL_DestroyRenderer(g_app.renderer);
    }
    if (g_app.window != NULL) {
        SDL_DestroyWindow(g_app.window);
    }
    SDL_DestroyMutex(g_app.lock);
    SDL_Quit();
    memset(&g_app, 0, sizeof(g_app));
    g_created = false;
    g_paused = false;
    if (g_push_lock != NULL) {
        SDL_DestroyMutex(g_push_lock);
        g_push_lock = NULL;
    }
}

void engine_pause(void)
{
    push_lock();
    g_paused = true;
    push_unlock();
}

void engine_resume(void)
{
    push_lock();
    g_paused = false;
    push_unlock();
}

void engine_set_folder(const char *uri_or_path)
{
    if (uri_or_path == NULL || *uri_or_path == '\0') {
        return;
    }
#if defined(__ANDROID__)
    /* Java persists the grant and passes the Uri directly.  Queue it for the
     * next engine frame so we never touch JNI off the SDL thread. */
    android_storage_queue_folder(uri_or_path);
#else
    storage_choose_folder_path(uri_or_path);
#endif
}

void engine_request_folder_picker(void)
{
#if defined(__ANDROID__)
    /* Set the QUIT gate synchronously BEFORE the Java picker can destroy the
     * activity.  The old polling code did the same with g_folder_pending;
     * doing it in Java's runOnUiThread callback was a race: the SDL thread
     * returns from this call immediately and sees QUIT before the callback
     * ever runs. */
    push_lock();
    g_picker_open = true;
    push_unlock();
    android_storage_show_picker();
#else
    storage_choose_folder();
#endif
}

void engine_start(void)
{
    if (!g_created) {
        return;
    }
    g_running = true;
    while (g_running) {
        push_lock();
        bool stop_requested = g_stop_requested;
        bool paused_now = g_paused;
        bool picker_open = g_picker_open;
        push_unlock();
        /* The SAF picker destroys the Activity on some ROMs, which calls
         * onDestroy() and asks the engine to stop.  Keep running until the
         * picker result (or a cancel) arrives and clears the gate. */
        if (stop_requested && !picker_open) {
            g_running = false;
            break;
        }
        if (paused_now && !g_pause_handled) {
            SDL_Event e;
            SDL_zero(e);
            e.type = SDL_EVENT_WILL_ENTER_BACKGROUND;
            event_watch(&g_app, &e);
            g_pause_handled = true;
        } else if (!paused_now) {
            g_pause_handled = false;
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                /* The SAF picker destroys the Activity on some ROMs, which
                 * SDL reports as QUIT.  The Activity is still alive as a Java
                 * object and will deliver the result (or a cancel), so keep
                 * the engine running until that signal arrives. */
                bool picker_open = false;
                push_lock();
                picker_open = g_picker_open;
                push_unlock();
                if (picker_open || g_paused) {
                    continue;
                }
                g_running = false;
                continue;
            }
            UiAction action = g_app.ui != NULL ? ui_handle_event(g_app.ui, &e, &g_app.model)
                                                : UI_ACTION_NONE;
            if (action == UI_ACTION_QUIT) {
                g_running = false;
            } else if (action != UI_ACTION_NONE) {
                handle_action(&g_app, action);
            }
        }
        push_lock();
        paused_now = g_paused;
        push_unlock();
        if (paused_now) {
            SDL_Delay(50);
            continue;
        }

        apply_pending_folder();

#if defined(__ANDROID__)
        if (g_app.permission_pending) {
            bool granted = false;
            if (android_storage_take_microphone_result(&granted)) {
                g_app.permission_pending = false;
                if (granted) {
                    start_recording(&g_app);
                } else {
                    ui_set_error(g_app.ui, "Microphone permission denied");
                }
            }
        }
#endif

        if (g_app.background_stopped) {
            g_app.background_stopped = false;
            refresh_files(&g_app);
            g_app.model.selected = recordlist_find(&g_app.list, g_app.background_name);
            ui_set_status(g_app.ui, "Recording saved before going to background");
        }

        SDL_LockMutex(g_app.lock);
        audio_update(g_app.engine);
        SDL_UnlockMutex(g_app.lock);

        if (audio_state(g_app.engine) == AUDIO_STATE_RECORDING &&
            audio_recording_limited(g_app.engine)) {
            stop_recording(&g_app);
        }

        g_app.model.files = (const char *const *)g_app.list.names;
        g_app.model.file_count = g_app.list.count;
        g_app.model.audio_state = audio_state(g_app.engine);
        uint64_t now = SDL_GetTicks();
        g_app.model.recording_ms =
            g_app.model.audio_state == AUDIO_STATE_RECORDING ? now - g_app.rec_start_ticks : 0;
        g_app.model.session_ms = practice_current_ms(&g_app.practice, now);
        g_app.model.last_take_ms = g_app.practice.last_take_ms;
        g_app.model.takes = g_app.practice.takes;
        snprintf(g_folder_label, sizeof(g_folder_label), "Folder: %s", storage_display());
        g_app.model.folder_label = g_folder_label;
        refresh_input_label(&g_app);
        g_app.model.input_label = g_input_label;
        if (practice_should_warn(&g_app.practice, now)) {
            practice_mark_warned(&g_app.practice);
            ui_set_status(g_app.ui, "You have practiced for 5+ minutes - take a break.");
        }
        g_app.model.practice_warn =
            practice_current_ms(&g_app.practice, now) >= PRACTICE_WARN_MS;

        if (g_app.ui != NULL && g_app.renderer != NULL) {
            apply_ui_mode(g_app.window, g_app.renderer);
            ui_render(g_app.ui, &g_app.model);
        }

        if (g_smoke && ++g_smoke_frames >= 5) {
            g_running = false;
        } else {
            SDL_Delay(10);
        }
    }
}

#if defined(__ANDROID__)
/* SDLActivity starts this on its SDL thread.  Java owns when the engine is
 * paused/resumed/stopped through the JNI hooks below; this function only keeps
 * the frame loop alive until Java asks it to stop. */
int SDL_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (engine_create(0, 0) != 0) {
        return 1;
    }
    engine_start();
    engine_stop();
    return 0;
}

JNIEXPORT void JNICALL
Java_moe_aimo_angelsound_AngelSoundActivity_nativeSetFolder(JNIEnv *env, jclass clazz,
                                                             jstring folder)
{
    (void)clazz;
    const char *chars = folder != NULL ? (*env)->GetStringUTFChars(env, folder, NULL) : NULL;
    if (chars != NULL) {
        engine_set_folder(chars);
        (*env)->ReleaseStringUTFChars(env, folder, chars);
    }
}

JNIEXPORT void JNICALL
Java_moe_aimo_angelsound_AngelSoundActivity_nativeFolderPickerClosed(JNIEnv *env,
                                                                    jclass clazz)
{
    (void)env;
    (void)clazz;
    push_lock();
    g_picker_open = false;
    push_unlock();
}

JNIEXPORT void JNICALL
Java_moe_aimo_angelsound_AngelSoundActivity_nativeStop(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    engine_request_stop();
}

#endif /* __ANDROID__ */
