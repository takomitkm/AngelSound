#include "recordlist.h"

#include <stdlib.h>
#include <string.h>

static int cmp_name(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static char *dup_string(const char *s)
{
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy != NULL) {
        memcpy(copy, s, len);
    }
    return copy;
}

void recordlist_init(RecordList *list)
{
    list->names = NULL;
    list->count = 0;
}

void recordlist_free(RecordList *list)
{
    for (int i = 0; i < list->count; i++) {
        free(list->names[i]);
    }
    free(list->names);
    list->names = NULL;
    list->count = 0;
}

bool recordlist_add(RecordList *list, const char *name)
{
    char **grown = realloc(list->names, sizeof(char *) * (size_t)(list->count + 1));
    if (grown == NULL) {
        return false;
    }
    list->names = grown;
    char *copy = dup_string(name);
    if (copy == NULL) {
        return false;
    }
    list->names[list->count] = copy;
    list->count++;
    return true;
}

void recordlist_remove(RecordList *list, int index)
{
    if (index < 0 || index >= list->count) {
        return;
    }
    free(list->names[index]);
    memmove(&list->names[index], &list->names[index + 1],
            sizeof(char *) * (size_t)(list->count - index - 1));
    list->count--;
}

void recordlist_clear(RecordList *list)
{
    recordlist_free(list);
}

void recordlist_sort(RecordList *list)
{
    if (list->count > 1) {
        qsort(list->names, (size_t)list->count, sizeof(char *), cmp_name);
    }
}

int recordlist_find(const RecordList *list, const char *name)
{
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}