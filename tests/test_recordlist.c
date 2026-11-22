#include <stdio.h>
#include <string.h>

#include "recordlist.h"

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

int main(void)
{
    RecordList list;
    recordlist_init(&list);
    CHECK(list.count == 0);
    CHECK(list.names == NULL);
    CHECK(recordlist_find(&list, "nope") == -1);

    CHECK(recordlist_add(&list, "b.wav"));
    CHECK(recordlist_add(&list, "a.wav"));
    CHECK(recordlist_add(&list, "c.wav"));
    CHECK(list.count == 3);
    CHECK(recordlist_find(&list, "a.wav") == 1);

    recordlist_sort(&list);
    CHECK(strcmp(list.names[0], "a.wav") == 0);
    CHECK(strcmp(list.names[1], "b.wav") == 0);
    CHECK(strcmp(list.names[2], "c.wav") == 0);

    recordlist_remove(&list, 1);
    CHECK(list.count == 2);
    CHECK(recordlist_find(&list, "b.wav") == -1);
    CHECK(recordlist_find(&list, "c.wav") == 1);

    recordlist_remove(&list, -1); /* ignored */
    recordlist_remove(&list, 99); /* ignored */
    CHECK(list.count == 2);

    recordlist_clear(&list);
    CHECK(list.count == 0);
    CHECK(recordlist_add(&list, "again.wav"));
    CHECK(list.count == 1);
    recordlist_free(&list);
    CHECK(list.count == 0);

    if (failures == 0) {
        printf("test_recordlist: PASS\n");
        return 0;
    }
    fprintf(stderr, "test_recordlist: %d failure(s)\n", failures);
    return 1;
}