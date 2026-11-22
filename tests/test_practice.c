#include <stdio.h>
#include <string.h>

#include "practice.h"

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void fmt(uint64_t ms, char *out, size_t cap)
{
    practice_format_time(ms, out, cap);
}

int main(void)
{
    char buf[32];

    fmt(0, buf, sizeof(buf));
    CHECK(strcmp(buf, "00:00") == 0);
    fmt(999, buf, sizeof(buf));
    CHECK(strcmp(buf, "00:00") == 0);
    fmt(59000, buf, sizeof(buf));
    CHECK(strcmp(buf, "00:59") == 0);
    fmt(125000, buf, sizeof(buf));
    CHECK(strcmp(buf, "02:05") == 0);
    fmt(PRACTICE_WARN_MS, buf, sizeof(buf));
    CHECK(strcmp(buf, "05:00") == 0);
    fmt(3661000, buf, sizeof(buf));
    CHECK(strcmp(buf, "1:01:01") == 0);

    Practice p;
    practice_init(&p);
    CHECK(p.takes == 0);
    CHECK(practice_current_ms(&p, 5000) == 0);
    CHECK(!practice_should_warn(&p, 5000));

    practice_take_start(&p, 1000);
    practice_take_start(&p, 2000); /* ignored while already taking */
    CHECK(p.taking);
    CHECK(practice_current_ms(&p, 3500) == 2500);
    practice_take_stop(&p, 3500);
    CHECK(!p.taking);
    CHECK(p.takes == 1);
    CHECK(p.session_ms == 2500);
    CHECK(p.last_take_ms == 2500);

    practice_take_stop(&p, 4000); /* ignored when not taking */
    CHECK(p.takes == 1);

    /* warning fires at 5 minutes, only once */
    practice_reset(&p);
    practice_take_start(&p, 0);
    practice_take_stop(&p, PRACTICE_WARN_MS - 1);
    CHECK(!practice_should_warn(&p, PRACTICE_WARN_MS - 1));
    practice_take_start(&p, PRACTICE_WARN_MS - 1);
    CHECK(practice_should_warn(&p, PRACTICE_WARN_MS));
    practice_mark_warned(&p);
    CHECK(!practice_should_warn(&p, PRACTICE_WARN_MS + 10000));

    practice_reset(&p);
    CHECK(p.session_ms == 0 && p.takes == 0 && !p.warned);
    CHECK(practice_current_ms(&p, 999999) == 0);

    if (failures == 0) {
        printf("test_practice: PASS\n");
        return 0;
    }
    fprintf(stderr, "test_practice: %d failure(s)\n", failures);
    return 1;
}