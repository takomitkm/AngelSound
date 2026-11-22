#ifndef ANGELSOUND_STORAGE_PURE_H
#define ANGELSOUND_STORAGE_PURE_H

/* Pure string helpers for the recordings location.  Kept SDL-free so the
 * storage tests do not need an audio/video device. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

size_t storage_parse_settings(const char *text, char *out, size_t cap);
int storage_format_settings(const char *location, char *out, size_t cap);
int storage_join(char *out, size_t cap, const char *dir, const char *name);
bool storage_is_saf(const char *location);
void storage_suffix_name(const char *base, int n, char *out, size_t cap);
int storage_format_take_name(int year, int month, int day, int hour, int minute,
                             int second, char *out, size_t cap);

#endif /* ANGELSOUND_STORAGE_PURE_H */