#include "padff/padff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EVENTS 4096
#define MAX_LINES 65536
#define TRACE_SLOTS 16

typedef struct {
    uint32_t ms;
    char op[8];
    long arg[13];
} event_t;

typedef struct {
    event_t events[MAX_EVENTS];
    size_t count;
    int mix;
    uint16_t scale;
    uint32_t interval;
    uint32_t end;
} trace_t;

typedef struct {
    char lines[MAX_LINES][32];
    size_t count;
} output_t;

static trace_t trace;
static output_t want;
static output_t got;

static int parse_directive(const char *line)
{
    char word[16];
    long value = 0;

    if (sscanf(line, "%15s %ld", word, &value) != 2)
        return 0;
    if (strcmp(word, "mix") == 0)
        trace.mix = (int)value;
    else if (strcmp(word, "scale") == 0)
        trace.scale = (uint16_t)value;
    else if (strcmp(word, "interval") == 0)
        trace.interval = (uint32_t)value;
    else if (strcmp(word, "end") == 0)
        trace.end = (uint32_t)value;
    else
        return 0;
    return 1;
}

static int parse_event(const char *line)
{
    event_t *ev = &trace.events[trace.count];
    unsigned long ms;
    long *a = ev->arg;

    if (trace.count == MAX_EVENTS)
        return 0;
    memset(ev, 0, sizeof(*ev));
    if (sscanf(line, "%lu %7s %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld", &ms, ev->op, &a[0], &a[1],
               &a[2], &a[3], &a[4], &a[5], &a[6], &a[7], &a[8], &a[9], &a[10], &a[11]) < 2)
        return 0;
    ev->ms = (uint32_t)ms;
    trace.count++;
    if (ev->ms + 2000 > trace.end)
        trace.end = ev->ms + 2000;
    return 1;
}

static int load_trace(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    int ok = 1;

    if (f == NULL)
        return 0;
    trace.scale = 65535;
    while (ok && fgets(line, sizeof(line), f) != NULL) {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        ok = (line[0] >= '0' && line[0] <= '9') ? parse_event(line) : parse_directive(line);
    }
    fclose(f);
    return ok;
}

static int load_expected(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[32];

    if (f == NULL)
        return 0;
    while (fgets(line, sizeof(line), f) != NULL && want.count < MAX_LINES) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] != '#' && line[0] != '\0')
            snprintf(want.lines[want.count++], sizeof(want.lines[0]), "%s", line);
    }
    fclose(f);
    return 1;
}

static void apply(pff_mixer_t *m, const event_t *ev, uint32_t now)
{
    const long *a = ev->arg;
    pff_effect_t e;

    if (strcmp(ev->op, "upload") == 0) {
        e.size = sizeof(e);
        e.type = (uint16_t)a[1];
        e.strong = (uint16_t)a[2];
        e.weak = (uint16_t)a[3];
        e.level = (int16_t)a[4];
        e.level_end = (int16_t)a[5];
        e.delay_ms = (uint16_t)a[6];
        e.length_ms = (uint16_t)a[7];
        e.attack_ms = (uint16_t)a[8];
        e.attack_level = (uint16_t)a[9];
        e.fade_ms = (uint16_t)a[10];
        e.fade_level = (uint16_t)a[11];
        pff_mixer_upload(m, (unsigned)a[0], &e, now);
    } else if (strcmp(ev->op, "play") == 0) {
        pff_mixer_play(m, (unsigned)a[0], (uint32_t)a[1], now);
    } else if (strcmp(ev->op, "erase") == 0) {
        pff_mixer_erase(m, (unsigned)a[0]);
    } else if (strcmp(ev->op, "gain") == 0) {
        pff_mixer_set_gain(m, (uint16_t)a[0]);
    } else if (strcmp(ev->op, "halt") == 0) {
        pff_mixer_stop(m, (unsigned)a[0]);
    } else if (strcmp(ev->op, "stop") == 0) {
        pff_mixer_stop_all(m);
    }
}

static uint32_t step(pff_mixer_t *m, uint32_t t, uint32_t base, int sleep)
{
    uint16_t strong;
    uint16_t weak;
    int send;
    uint32_t wait;

    pff_mixer_poll(m, base + t, &strong, &weak, &send);
    if (send && got.count < MAX_LINES)
        snprintf(got.lines[got.count++], sizeof(got.lines[0]), "%u %u %u", (unsigned)t, strong, weak);
    if (!sleep)
        return 1;
    pff_mixer_next(m, base + t, &wait);
    if (wait == 0) {
        fprintf(stderr, "next returned 0 right after poll at %u\n", (unsigned)t);
        exit(1);
    }
    return wait;
}

static void run(uint32_t base, int sleep)
{
    static unsigned char mem[PFF_MIXER_SIZE(TRACE_SLOTS)];
    pff_mixer_t *m = NULL;
    pff_config_t config;
    size_t next_ev = 0;
    uint32_t t = 0;

    got.count = 0;
    config.size = sizeof(config);
    config.mix = (uint32_t)trace.mix;
    config.motor_max = (uint32_t)trace.scale;
    config.interval_ms = (uint32_t)trace.interval;
    pff_mixer_init(mem, sizeof(mem), TRACE_SLOTS, &config, &m);
    while (t <= trace.end) {
        uint32_t wait;

        while (next_ev < trace.count && trace.events[next_ev].ms <= t)
            apply(m, &trace.events[next_ev++], base + t);
        wait = step(m, t, base, sleep);
        if (next_ev < trace.count && trace.events[next_ev].ms - t < wait)
            wait = trace.events[next_ev].ms - t;
        if (wait > trace.end - t)
            break;
        t += wait;
    }
}

static int compare(const char *label)
{
    size_t i;

    for (i = 0; i < want.count || i < got.count; i++) {
        const char *w = i < want.count ? want.lines[i] : "(none)";
        const char *g = i < got.count ? got.lines[i] : "(none)";

        if (strcmp(w, g) != 0) {
            fprintf(stderr, "%s: line %zu: want \"%s\", got \"%s\"\n", label, i + 1, w, g);
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    static const uint32_t bases[] = {0, 0xfffff000u};
    size_t b;
    int ok = 1;

    if (argc == 2 && load_trace(argv[1])) {
        run(0, 0);
        for (b = 0; b < got.count; b++)
            printf("%s\n", got.lines[b]);
        return 0;
    }
    if (argc != 3 || !load_trace(argv[1]) || !load_expected(argv[2])) {
        fprintf(stderr, "usage: trace_run <trace> [expected]\n");
        return 2;
    }
    for (b = 0; b < sizeof(bases) / sizeof(bases[0]); b++) {
        run(bases[b], 0);
        ok &= compare(b ? "tick, near wrap" : "tick");
        run(bases[b], 1);
        ok &= compare(b ? "next, near wrap" : "next");
    }
    return ok ? 0 : 1;
}
