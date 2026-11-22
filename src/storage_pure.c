#include "storage_pure.h"

#include <stdio.h>
#include <string.h>

size_t storage_parse_settings(const char *text, char *out, size_t cap)
{
    static const char key[] = "recordings_dir=";
    const size_t key_len = sizeof(key) - 1;

    if (text == NULL || out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';

    /* Tolerate a UTF-8 BOM from editors. */
    if (strlen(text) >= 3 && (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        text += 3;
    }
    if (strncmp(text, key, key_len) != 0) {
        return 0;
    }
    const char *value = text + key_len;
    size_t len = strcspn(value, "\r\n");
    if (len == 0 || len >= cap) {
        return 0;
    }
    memcpy(out, value, len);
    out[len] = '\0';
    return len;
}

int storage_format_settings(const char *location, char *out, size_t cap)
{
    int len = snprintf(out, cap, "recordings_dir=%s\n", location != NULL ? location : "");
    return len >= 0 && (size_t)len < cap ? len : -1;
}

int storage_join(char *out, size_t cap, const char *dir, const char *name)
{
    if (dir == NULL || out == NULL || cap == 0) {
        return -1;
    }
    if (name == NULL || name[0] == '\0') {
        int len = snprintf(out, cap, "%s", dir);
        return len >= 0 && (size_t)len < cap ? len : -1;
    }
    size_t dir_len = strlen(dir);
    bool has_sep = dir_len > 0 && (dir[dir_len - 1] == '/' || dir[dir_len - 1] == '\\');
    int len = snprintf(out, cap, has_sep ? "%s%s" : "%s/%s", dir, name);
    return len >= 0 && (size_t)len < cap ? len : -1;
}

bool storage_is_saf(const char *location)
{
    return location != NULL && strncmp(location, "content://", 10) == 0;
}

void storage_suffix_name(const char *base, int n, char *out, size_t cap)
{
    if (n <= 0 || base == NULL) {
        snprintf(out, cap, "%s", base != NULL ? base : "");
        return;
    }
    size_t len = strlen(base);
    if (len >= 4 && strcmp(base + len - 4, ".wav") == 0) {
        snprintf(out, cap, "%.*s-%d%s", (int)(len - 4), base, n, ".wav");
    } else {
        snprintf(out, cap, "%s-%d", base, n);
    }
}

int storage_format_take_name(int year, int month, int day, int hour, int minute,
                             int second, char *out, size_t cap)
{
    int len = snprintf(out, cap, "ASREC_%04d%02d%02d-%02d%02d%02d.wav", year, month,
                       day, hour, minute, second);
    return len >= 0 && (size_t)len < cap ? len : -1;
}