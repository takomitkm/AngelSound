#ifndef ANGELSOUND_RECORDLIST_H
#define ANGELSOUND_RECORDLIST_H

#include <stdbool.h>

typedef struct {
    char **names;
    int count;
} RecordList;

void recordlist_init(RecordList *list);
void recordlist_free(RecordList *list);
bool recordlist_add(RecordList *list, const char *name);
void recordlist_remove(RecordList *list, int index);
void recordlist_clear(RecordList *list);
void recordlist_sort(RecordList *list);
int recordlist_find(const RecordList *list, const char *name);

#endif /* ANGELSOUND_RECORDLIST_H */