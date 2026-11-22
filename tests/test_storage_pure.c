#include <stdio.h>
#include <string.h>

#include "storage_pure.h"

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
    char out[128];

    memset(out, 'x', sizeof(out));
    CHECK(storage_parse_settings("recordings_dir=/home/u/Music/AngelSound\n", out,
                                 sizeof(out)) == 24);
    CHECK(strcmp(out, "/home/u/Music/AngelSound") == 0);

    CHECK(storage_parse_settings("recordings_dir=C:\\Music\\AngelSound\r\n", out,
                                 sizeof(out)) > 0);
    CHECK(strcmp(out, "C:\\Music\\AngelSound") == 0);

    CHECK(storage_parse_settings("\xEF\xBB\xBFrecordings_dir=/x\n", out, sizeof(out)) == 2);
    CHECK(strcmp(out, "/x") == 0);

    CHECK(storage_parse_settings("other=1\n", out, sizeof(out)) == 0);
    CHECK(out[0] == '\0');
    CHECK(storage_parse_settings("recordings_dir=\n", out, sizeof(out)) == 0);
    CHECK(storage_parse_settings(NULL, out, sizeof(out)) == 0);
    CHECK(storage_parse_settings("recordings_dir=/abc", out, 4) == 0);

    char line[160];
    int n = storage_format_settings("content://tree/primary%3AMusic", line, sizeof(line));
    CHECK(n > 0);
    CHECK(strcmp(line, "recordings_dir=content://tree/primary%3AMusic\n") == 0);
    CHECK(storage_format_settings("x", line, 4) < 0);

    CHECK(storage_join(out, sizeof(out), "/a", "b.wav") > 0);
    CHECK(strcmp(out, "/a/b.wav") == 0);
    CHECK(storage_join(out, sizeof(out), "/a/", "b.wav") > 0);
    CHECK(strcmp(out, "/a/b.wav") == 0);
    CHECK(storage_join(out, sizeof(out), "/a", "") > 0);
    CHECK(strcmp(out, "/a") == 0);
    CHECK(storage_join(out, sizeof(out), "/a", "0123456789") > 0);
    CHECK(storage_join(out, 6, "/a", "0123456789") < 0);

    CHECK(storage_is_saf("content://com.android.externalstorage.documents/tree/x"));
    CHECK(!storage_is_saf("/home/u/Music"));
    CHECK(!storage_is_saf(""));
    CHECK(!storage_is_saf(NULL));

    storage_suffix_name("ASREC_1.wav", 0, out, sizeof(out));
    CHECK(strcmp(out, "ASREC_1.wav") == 0);
    storage_suffix_name("ASREC_1.wav", 3, out, sizeof(out));
    CHECK(strcmp(out, "ASREC_1-3.wav") == 0);
    storage_suffix_name("plain", 2, out, sizeof(out));
    CHECK(strcmp(out, "plain-2") == 0);

    CHECK(storage_format_take_name(2026, 11, 23, 9, 5, 7, out, sizeof(out)) > 0);
    CHECK(strcmp(out, "ASREC_20261123-090507.wav") == 0);
    CHECK(storage_format_take_name(2026, 11, 23, 9, 5, 7, out, 10) < 0);

    if (failures == 0) {
        printf("test_storage_pure: PASS\n");
        return 0;
    }
    fprintf(stderr, "test_storage_pure: %d failure(s)\n", failures);
    return 1;
}