#ifndef ANGELSOUND_PRACTICE_H
#define ANGELSOUND_PRACTICE_H

/* Practice session bookkeeping: how many takes were recorded, how long the
 * session has been going, and a gentle "take a break" nudge (the original
 * app's tab hint was "练习时间不要太久"). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PRACTICE_WARN_MS (5ULL * 60 * 1000)

typedef struct {
    uint64_t session_ms;     /* accumulated finished takes */
    uint64_t take_started_ms;
    uint64_t last_take_ms;
    int takes;
    bool taking;
    bool warned;
} Practice;

void practice_init(Practice *p);
void practice_reset(Practice *p);
void practice_take_start(Practice *p, uint64_t now_ms);
void practice_take_stop(Practice *p, uint64_t now_ms);
uint64_t practice_current_ms(const Practice *p, uint64_t now_ms);
bool practice_should_warn(const Practice *p, uint64_t now_ms);
void practice_mark_warned(Practice *p);
void practice_format_time(uint64_t ms, char *out, size_t cap);

#endif /* ANGELSOUND_PRACTICE_H */