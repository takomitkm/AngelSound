#include "storage.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "storage_pure.h"

#if defined(__ANDROID__)
#include "android_storage.h"
#endif

#define PATH_CAP 1024

static char g_location[PATH_CAP];
static char g_pref[PATH_CAP];
static char g_cache[PATH_CAP];
static char g_display[PATH_CAP + 8];
static bool g_saf;

static bool g_folder_pending;
static int g_folder_result; /* 0 none, 1 picked, -1 canceled/failed */
static char g_folder_value[PATH_CAP];
#if defined(__ANDROID__)
/* Grace period before the SAF picker result is considered lost.  Some ROMs
 * destroy the activity that launched the picker, so the result never
 * arrives; after this delay we fall back to the public folder. */
static uint64_t g_folder_deadline;
#define FOLDER_PICK_GRACE_MS 4000
#endif

static void set_err(char *err, size_t cap, const char *text)
{
    if (err != NULL && cap > 0) {
        snprintf(err, cap, "%s", text);
    }
}

static void update_display(void)
{
    if (g_saf) {
        snprintf(g_display, sizeof(g_display), "SAF: %s", g_location);
    } else {
        snprintf(g_display, sizeof(g_display), "%s", g_location);
    }
}

static bool path_is_dir(const char *path)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

/* Check that a plain directory really accepts writes.  Android's scoped
 * storage makes Java's canWrite() report false for public folders that the
 * native layer can still write, so probe with a real file instead. */
static bool path_is_writable(const char *path)
{
    char probe[PATH_CAP];
    if (storage_join(probe, sizeof(probe), path, ".angelsound-write-test") < 0) {
        return false;
    }
    FILE *f = fopen(probe, "wb");
    if (f == NULL) {
        return false;
    }
    fclose(f);
    remove(probe);
    return true;
}

static void default_location(char *out, size_t cap)
{
#if defined(__ANDROID__)
    const char *base = SDL_GetAndroidExternalStoragePath();
    if (base == NULL || *base == '\0') {
        base = SDL_GetAndroidInternalStoragePath();
    }
    if (base != NULL && *base != '\0') {
        storage_join(out, cap, base, "AngelSound");
        return;
    }
    snprintf(out, cap, "AngelSound");
#else
    const char *env = getenv("ANGELSOUND_DIR");
    if (env != NULL && *env != '\0') {
        snprintf(out, cap, "%s", env);
        return;
    }
    const char *music = SDL_GetUserFolder(SDL_FOLDER_MUSIC);
    if (music != NULL && *music != '\0') {
        storage_join(out, cap, music, "AngelSound");
        return;
    }
    snprintf(out, cap, "AngelSound");
#endif
}

static void settings_path(char *out, size_t cap)
{
    out[0] = '\0';
    if (g_pref[0] != '\0') {
        storage_join(out, cap, g_pref, "settings.txt");
    }
}

static void save_settings(void)
{
    char path[PATH_CAP];
    char text[PATH_CAP + 32];
    settings_path(path, sizeof(path));
    if (path[0] == '\0') {
        return;
    }
    int len = storage_format_settings(g_location, text, sizeof(text));
    if (len > 0) {
        SDL_SaveFile(path, text, (size_t)len);
    }
}

static bool apply_location(const char *location, bool saf, char *err, size_t errcap)
{
    if (location == NULL || *location == '\0') {
        set_err(err, errcap, "empty recordings folder");
        return false;
    }
    if (!saf && !path_is_dir(location) && !SDL_CreateDirectory(location)) {
        set_err(err, errcap, "cannot create recordings folder");
        return false;
    }
    if (!saf && !path_is_writable(location)) {
        set_err(err, errcap, "recordings folder is not writable");
        return false;
    }
    snprintf(g_location, sizeof(g_location), "%s", location);
    g_saf = saf;
    update_display();
    save_settings();
    return true;
}

bool storage_init(char *err, size_t errcap)
{
    g_location[0] = '\0';
    g_pref[0] = '\0';
    g_cache[0] = '\0';
    g_saf = false;
    g_folder_pending = false;
    g_folder_result = 0;
#if defined(__ANDROID__)
    g_folder_deadline = 0;
#endif

    char *pref = SDL_GetPrefPath("Aimo", "AngelSound");
    if (pref != NULL) {
        snprintf(g_pref, sizeof(g_pref), "%s", pref);
        SDL_free(pref);
        storage_join(g_cache, sizeof(g_cache), g_pref, "cache");
        SDL_CreateDirectory(g_cache);
    }

    char settings[PATH_CAP];
    settings_path(settings, sizeof(settings));
    if (settings[0] != '\0') {
        size_t size = 0;
        char *data = SDL_LoadFile(settings, &size);
        if (data != NULL) {
            char value[PATH_CAP];
            if (storage_parse_settings(data, value, sizeof(value)) > 0) {
                snprintf(g_location, sizeof(g_location), "%s", value);
            }
            SDL_free(data);
        }
    }

    if (g_location[0] == '\0') {
        default_location(g_location, sizeof(g_location));
    }
    g_saf = storage_is_saf(g_location);
    if (!g_saf && !path_is_dir(g_location)) {
        if (!SDL_CreateDirectory(g_location)) {
            default_location(g_location, sizeof(g_location));
            g_saf = storage_is_saf(g_location);
            if (!g_saf && !SDL_CreateDirectory(g_location)) {
                snprintf(g_location, sizeof(g_location), ".");
            }
        }
    }
    update_display();
    (void)err;
    (void)errcap;
    return true;
}

void storage_shutdown(void)
{
}

const char *storage_display(void)
{
    return g_display;
}

static bool name_exists(const char *name)
{
    if (g_saf) {
#if defined(__ANDROID__)
        return android_storage_exists(g_location, name);
#else
        return false;
#endif
    }
    char path[PATH_CAP];
    if (storage_join(path, sizeof(path), g_location, name) < 0) {
        return true;
    }
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info);
}

bool storage_list(RecordList *out, char *err, size_t errcap)
{
    recordlist_clear(out);
    if (g_saf) {
#if defined(__ANDROID__)
        char **names = NULL;
        int count = 0;
        if (!android_storage_list(g_location, &names, &count)) {
            set_err(err, errcap, "cannot list SAF folder");
            return false;
        }
        for (int i = 0; i < count; i++) {
            recordlist_add(out, names[i]);
            free(names[i]);
        }
        free(names);
#else
        set_err(err, errcap, "SAF folder is Android-only");
        return false;
#endif
    } else {
        int count = 0;
        char **items = SDL_GlobDirectory(g_location, "ASREC_*.wav", 0, &count);
        if (items == NULL) {
            set_err(err, errcap, SDL_GetError());
            return false;
        }
        for (int i = 0; i < count; i++) {
            recordlist_add(out, items[i]);
        }
        SDL_free(items);
    }
    recordlist_sort(out);
    return true;
}

bool storage_make_name(char *name, size_t cap)
{
    char base[64];
    SDL_Time ticks = 0;
    SDL_DateTime dt;
    memset(&dt, 0, sizeof(dt));
    if (!SDL_GetCurrentTime(&ticks) || !SDL_TimeToDateTime(ticks, &dt, true)) {
        snprintf(base, sizeof(base), "ASREC_%u.wav", (unsigned)SDL_GetTicks());
    } else if (storage_format_take_name(dt.year, dt.month, dt.day, dt.hour, dt.minute,
                                        dt.second, base, sizeof(base)) < 0) {
        return false;
    }
    for (int n = 0; n < 100; n++) {
        storage_suffix_name(base, n, name, cap);
        if (!name_exists(name)) {
            return true;
        }
    }
    return false;
}

bool storage_take_path(const char *name, char *path, size_t cap, char *err,
                       size_t errcap)
{
    if (g_saf) {
        if (g_cache[0] == '\0' || storage_join(path, cap, g_cache, name) < 0) {
            set_err(err, errcap, "no cache folder for SAF recording");
            return false;
        }
        return true;
    }
    if (storage_join(path, cap, g_location, name) < 0) {
        set_err(err, errcap, "recording path too long");
        return false;
    }
    return true;
}

bool storage_commit_take(const char *name, const char *path, char *err, size_t errcap)
{
    if (!g_saf) {
        return true; /* already written into the recordings folder */
    }
#if defined(__ANDROID__)
    if (!android_storage_write_from_cache(g_location, name, path)) {
        set_err(err, errcap, "cannot write recording into SAF folder");
        return false;
    }
    SDL_RemovePath(path);
    return true;
#else
    (void)name;
    (void)path;
    set_err(err, errcap, "SAF folder is Android-only");
    return false;
#endif
}

bool storage_playback_path(const char *name, char *path, size_t cap, char *err,
                           size_t errcap)
{
    if (!g_saf) {
        if (storage_join(path, cap, g_location, name) < 0) {
            set_err(err, errcap, "recording path too long");
            return false;
        }
        return true;
    }
#if defined(__ANDROID__)
    char cache_name[128];
    snprintf(cache_name, sizeof(cache_name), "play-%s", name);
    if (storage_join(path, cap, g_cache, cache_name) < 0) {
        set_err(err, errcap, "cache path too long");
        return false;
    }
    if (!android_storage_read_to_cache(g_location, name, path)) {
        set_err(err, errcap, "cannot read recording from SAF folder");
        return false;
    }
    return true;
#else
    (void)name;
    set_err(err, errcap, "SAF folder is Android-only");
    return false;
#endif
}

bool storage_delete(const char *name, char *err, size_t errcap)
{
    if (g_saf) {
#if defined(__ANDROID__)
        if (!android_storage_delete(g_location, name)) {
            set_err(err, errcap, "cannot delete SAF recording");
            return false;
        }
        return true;
#else
        set_err(err, errcap, "SAF folder is Android-only");
        return false;
#endif
    }
    char path[PATH_CAP];
    if (storage_join(path, sizeof(path), g_location, name) < 0) {
        set_err(err, errcap, "recording path too long");
        return false;
    }
    if (!SDL_RemovePath(path)) {
        set_err(err, errcap, SDL_GetError());
        return false;
    }
    return true;
}

#if !defined(__ANDROID__)
static void SDLCALL folder_cb(void *userdata, const char *const *filelist, int filter)
{
    (void)userdata;
    (void)filter;
    if (filelist != NULL && filelist[0] != NULL) {
        snprintf(g_folder_value, sizeof(g_folder_value), "%s", filelist[0]);
        g_folder_result = 1;
    } else {
        g_folder_result = -1;
    }
}
#endif

bool storage_choose_folder(void)
{
    if (g_folder_pending) {
        return false;
    }
#if defined(__ANDROID__)
    if (!android_storage_pick_folder()) {
        return false;
    }
    g_folder_pending = true;
    g_folder_result = 0;
    g_folder_deadline = SDL_GetTicks() + FOLDER_PICK_GRACE_MS;
    return true;
#else
    g_folder_result = 0;
    g_folder_value[0] = '\0';
    SDL_ShowOpenFolderDialog(folder_cb, NULL, NULL, g_saf ? NULL : g_location, false);
    g_folder_pending = true;
    return true;
#endif
}

int storage_poll_folder(void)
{
#if defined(__ANDROID__)
    char *uri = android_storage_take_picked_uri();
    if (uri == NULL) {
        /* No SAF result yet.  If the picker has been open long enough, the
         * result is never going to arrive (the activity was destroyed), so
         * fall back to the public recordings folder. */
        if (g_folder_pending && SDL_GetTicks() >= g_folder_deadline) {
            char *fallback = android_storage_public_folder();
            g_folder_pending = false;
            if (fallback == NULL || *fallback == '\0') {
                free(fallback);
                return -1;
            }
            bool ok = apply_location(fallback, false, NULL, 0);
            free(fallback);
            return ok ? 1 : -1;
        }
        return 0;
    }
    if (*uri == '\0') {
        free(uri);
        g_folder_pending = false;
        return -1;
    }
    /* On Android the picker may destroy/recreate the activity, losing the
     * pending flag.  Accept a URI whenever the Java side found a fresh
     * persisted grant, but skip it if it is already the current folder. */
    if (!g_folder_pending && strcmp(g_location, uri) == 0) {
        free(uri);
        return 0;
    }
    bool ok = apply_location(uri, true, NULL, 0);
    free(uri);
    g_folder_pending = false;
    return ok ? 1 : -1;
#else
    if (!g_folder_pending) {
        return 0;
    }
    int result = g_folder_result;
    g_folder_result = 0;
    g_folder_pending = false;
    if (result < 0) {
        return -1;
    }
    return apply_location(g_folder_value, false, NULL, 0) ? 1 : -1;
#endif
}