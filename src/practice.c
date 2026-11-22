#include "practice.h"

#include <stdio.h>

void practice_init(Practice *p)
{
    p->session_ms = 0;
    p->take_started_ms = 0;
    p->last_take_ms = 0;
    p->takes = 0;
    p->taking = false;
    p->warned = false;
}

void practice_reset(Practice *p)
{
    practice_init(p);
}

void practice_take_start(Practice *p, uint64_t now_ms)
{
    if (p->taking) {
        return;
    }
    p->taking = true;
    p->take_started_ms = now_ms;
}

void practice_take_stop(Practice *p, uint64_t now_ms)
{
    if (!p->taking) {
        return;
    }
    p->taking = false;
    uint64_t delta = now_ms >= p->take_started_ms ? now_ms - p->take_started_ms : 0;
    p->session_ms += delta;
    p->last_take_ms = delta;
    p->takes++;
}

uint64_t practice_current_ms(const Practice *p, uint64_t now_ms)
{
    uint64_t active = 0;
    if (p->taking && now_ms > p->take_started_ms) {
        active = now_ms - p->take_started_ms;
    }
    return p->session_ms + active;
}

bool practice_should_warn(const Practice *p, uint64_t now_ms)
{
    return !p->warned && practice_current_ms(p, now_ms) >= PRACTICE_WARN_MS;
}

void practice_mark_warned(Practice *p)
{
    p->warned = true;
}

void practice_format_time(uint64_t ms, char *out, size_t cap)
{
    uint64_t total_seconds = ms / 1000;
    uint64_t hours = total_seconds / 3600;
    uint64_t minutes = (total_seconds % 3600) / 60;
    uint64_t seconds = total_seconds % 60;
    if (hours > 0) {
        snprintf(out, cap, "%llu:%02llu:%02llu",
                 (unsigned long long)hours, (unsigned long long)minutes,
                 (unsigned long long)seconds);
    } else {
        snprintf(out, cap, "%02llu:%02llu",
                 (unsigned long long)minutes, (unsigned long long)seconds);
    }
}