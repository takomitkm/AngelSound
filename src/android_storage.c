#if defined(__ANDROID__)

#include "android_storage.h"

#include <SDL3/SDL.h>
#include <jni.h>
#include <stdlib.h>
#include <string.h>

static jclass g_cls;
static jmethodID g_pick;
static jmethodID g_take_uri;
static jmethodID g_list;
static jmethodID g_exists;
static jmethodID g_read;
static jmethodID g_write;
static jmethodID g_delete;

static int g_mic_state;

typedef struct {
    JNIEnv *env;
    jobject activity;
    bool ready;
} JniCall;

static bool jni_ready(JNIEnv *env, jobject activity, JniCall *call)
{
    call->env = env;
    call->activity = activity;
    call->ready = false;

    if (env == NULL || activity == NULL) {
        SDL_SetError("no Android JNI environment/activity");
        return false;
    }
    if (g_cls == NULL) {
        jclass local = (*env)->GetObjectClass(env, activity);
        if (local == NULL) {
            return false;
        }
        g_cls = (jclass)(*env)->NewGlobalRef(env, local);
        (*env)->DeleteLocalRef(env, local);
    }
    if (g_pick == NULL) {
        g_pick = (*env)->GetMethodID(env, g_cls, "pickFolder", "()V");
        g_take_uri = (*env)->GetMethodID(env, g_cls, "getPickedUri",
                                         "()Ljava/lang/String;");
        g_list = (*env)->GetMethodID(env, g_cls, "listWavs",
                                     "(Ljava/lang/String;)[Ljava/lang/String;");
        g_exists = (*env)->GetMethodID(env, g_cls, "exists",
                                       "(Ljava/lang/String;Ljava/lang/String;)Z");
        g_read = (*env)->GetMethodID(env, g_cls, "readToCache",
                                     "(Ljava/lang/String;Ljava/lang/String;"
                                     "Ljava/lang/String;)Z");
        g_write = (*env)->GetMethodID(env, g_cls, "writeFromCache",
                                      "(Ljava/lang/String;Ljava/lang/String;"
                                      "Ljava/lang/String;)Z");
        g_delete = (*env)->GetMethodID(env, g_cls, "deleteDoc",
                                       "(Ljava/lang/String;Ljava/lang/String;)Z");
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return false;
    }
    call->ready = g_pick != NULL && g_take_uri != NULL && g_list != NULL &&
                  g_exists != NULL && g_read != NULL && g_write != NULL &&
                  g_delete != NULL;
    return call->ready;
}

static bool jni_begin(JniCall *call)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    return jni_ready(env, activity, call);
}

static void jni_end(JniCall *call)
{
    if (call->env != NULL && call->activity != NULL) {
        (*call->env)->DeleteLocalRef(call->env, call->activity);
    }
    call->activity = NULL;
}

static bool check_exception(JNIEnv *env, const char *where)
{
    if ((*env)->ExceptionCheck(env)) {
        SDL_Log("JNI exception in %s", where);
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return true;
    }
    return false;
}

static jstring make_string(JNIEnv *env, const char *text)
{
    return (*env)->NewStringUTF(env, text != NULL ? text : "");
}

bool android_storage_pick_folder(void)
{
    JniCall call;
    if (!jni_begin(&call)) {
        return false;
    }
    (*call.env)->CallVoidMethod(call.env, call.activity, g_pick);
    bool ok = !check_exception(call.env, "pickFolder");
    jni_end(&call);
    return ok;
}

char *android_storage_take_picked_uri(void)
{
    JniCall call;
    if (!jni_begin(&call)) {
        return NULL;
    }
    JNIEnv *env = call.env;
    jstring value = (jstring)(*env)->CallObjectMethod(env, call.activity, g_take_uri);
    char *copy = NULL;
    if (!check_exception(env, "getPickedUri") && value != NULL) {
        const char *chars = (*env)->GetStringUTFChars(env, value, NULL);
        if (chars != NULL) {
            size_t len = strlen(chars);
            copy = malloc(len + 1);
            if (copy != NULL) {
                memcpy(copy, chars, len + 1);
            }
            (*env)->ReleaseStringUTFChars(env, value, chars);
        }
        (*env)->DeleteLocalRef(env, value);
    }
    jni_end(&call);
    return copy;
}

bool android_storage_list(const char *uri, char ***names, int *count)
{
    *names = NULL;
    *count = 0;
    JniCall call;
    if (!jni_begin(&call)) {
        return false;
    }
    JNIEnv *env = call.env;
    jstring juri = make_string(env, uri);
    jobjectArray array =
        (jobjectArray)(*env)->CallObjectMethod(env, call.activity, g_list, juri);
    (*env)->DeleteLocalRef(env, juri);
    if (check_exception(env, "listWavs") || array == NULL) {
        jni_end(&call);
        return false;
    }
    jsize len = (*env)->GetArrayLength(env, array);
    char **out = len > 0 ? calloc((size_t)len, sizeof(char *)) : NULL;
    if (len > 0 && out == NULL) {
        (*env)->DeleteLocalRef(env, array);
        jni_end(&call);
        SDL_SetError("out of memory listing SAF folder");
        return false;
    }
    for (jsize i = 0; i < len; i++) {
        jstring item = (jstring)(*env)->GetObjectArrayElement(env, array, i);
        const char *chars =
            item != NULL ? (*env)->GetStringUTFChars(env, item, NULL) : NULL;
        if (chars != NULL) {
            size_t n = strlen(chars);
            out[i] = malloc(n + 1);
            if (out[i] != NULL) {
                memcpy(out[i], chars, n + 1);
            }
            (*env)->ReleaseStringUTFChars(env, item, chars);
        }
        if (item != NULL) {
            (*env)->DeleteLocalRef(env, item);
        }
        if (out[i] == NULL) {
            for (jsize k = 0; k < i; k++) {
                free(out[k]);
            }
            free(out);
            (*env)->DeleteLocalRef(env, array);
            jni_end(&call);
            SDL_SetError("out of memory copying SAF names");
            return false;
        }
    }
    (*env)->DeleteLocalRef(env, array);
    jni_end(&call);
    *names = out;
    *count = (int)len;
    return true;
}

static bool call_two_strings(const char *uri, const char *name, jmethodID method,
                             const char *what)
{
    JniCall call;
    if (!jni_begin(&call)) {
        return false;
    }
    JNIEnv *env = call.env;
    jstring juri = make_string(env, uri);
    jstring jname = make_string(env, name);
    jboolean ok =
        (*env)->CallBooleanMethod(env, call.activity, method, juri, jname);
    (*env)->DeleteLocalRef(env, juri);
    (*env)->DeleteLocalRef(env, jname);
    bool good = !check_exception(env, what) && ok == JNI_TRUE;
    jni_end(&call);
    return good;
}

bool android_storage_exists(const char *uri, const char *name)
{
    JniCall call;
    if (!jni_begin(&call)) {
        return false;
    }
    jni_end(&call);
    return call_two_strings(uri, name, g_exists, "exists");
}

bool android_storage_delete(const char *uri, const char *name)
{
    JniCall call;
    if (!jni_begin(&call)) {
        return false;
    }
    jni_end(&call);
    return call_two_strings(uri, name, g_delete, "deleteDoc");
}

static bool call_three_strings(const char *uri, const char *name, const char *path,
                               jmethodID method, const char *what)
{
    JniCall call;
    if (!jni_begin(&call)) {
        return false;
    }
    JNIEnv *env = call.env;
    jstring juri = make_string(env, uri);
    jstring jname = make_string(env, name);
    jstring jpath = make_string(env, path);
    jboolean ok =
        (*env)->CallBooleanMethod(env, call.activity, method, juri, jname, jpath);
    (*env)->DeleteLocalRef(env, juri);
    (*env)->DeleteLocalRef(env, jname);
    (*env)->DeleteLocalRef(env, jpath);
    bool good = !check_exception(env, what) && ok == JNI_TRUE;
    jni_end(&call);
    return good;
}

bool android_storage_read_to_cache(const char *uri, const char *name, const char *dest)
{
    JniCall call;
    if (!jni_begin(&call)) {
        return false;
    }
    jni_end(&call);
    return call_three_strings(uri, name, dest, g_read, "readToCache");
}

bool android_storage_write_from_cache(const char *uri, const char *name, const char *src)
{
    JniCall call;
    if (!jni_begin(&call)) {
        return false;
    }
    jni_end(&call);
    return call_three_strings(uri, name, src, g_write, "writeFromCache");
}

static void SDLCALL mic_callback(void *userdata, const char *permission, bool granted)
{
    (void)userdata;
    (void)permission;
    g_mic_state = granted ? 1 : -1;
}

bool android_request_microphone(void)
{
    g_mic_state = 0;
    return SDL_RequestAndroidPermission("android.permission.RECORD_AUDIO", mic_callback,
                                        NULL);
}

int android_microphone_state(void)
{
    return g_mic_state;
}

#endif /* __ANDROID__ */