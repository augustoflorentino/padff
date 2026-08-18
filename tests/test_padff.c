#include "padff/padff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                    \
        }                                                               \
    } while (0)

#define SLOTS 4

static unsigned char mem[PFF_MIXER_SIZE(SLOTS) + 8];

typedef struct {
    uint16_t strong;
    uint16_t weak;
    int send;
} poll_t;

static pff_mixer_t *new_mixer(void)
{
    pff_mixer_t *m = NULL;

    CHECK(pff_mixer_init(mem, sizeof(mem), SLOTS, &m) == PFF_OK);
    CHECK(m != NULL);
    return m;
}

static poll_t poll_at(pff_mixer_t *m, uint32_t now)
{
    poll_t p;

    CHECK(pff_mixer_poll(m, now, &p.strong, &p.weak, &p.send) == PFF_OK);
    return p;
}

static uint32_t wait_at(const pff_mixer_t *m, uint32_t now)
{
    uint32_t wait = 0;

    CHECK(pff_mixer_next(m, now, &wait) == PFF_OK);
    return wait;
}

static void expect(pff_mixer_t *m, uint32_t now, int send, uint16_t strong, uint16_t weak)
{
    poll_t p = poll_at(m, now);

    if (p.send != send || p.strong != strong || p.weak != weak) {
        fprintf(stderr, "t=%u: got send=%d %u/%u, want send=%d %u/%u\n", (unsigned)now, p.send,
                (unsigned)p.strong, (unsigned)p.weak, send, (unsigned)strong, (unsigned)weak);
        exit(1);
    }
}

static pff_effect_t rumble(uint16_t strong, uint16_t weak, uint16_t length_ms)
{
    pff_effect_t e;

    memset(&e, 0, sizeof(e));
    e.size = sizeof(e);
    e.type = PFF_RUMBLE;
    e.strong = strong;
    e.weak = weak;
    e.length_ms = length_ms;
    return e;
}

static pff_effect_t level(uint16_t type, int16_t start, int16_t end, uint16_t length_ms)
{
    pff_effect_t e;

    memset(&e, 0, sizeof(e));
    e.size = sizeof(e);
    e.type = type;
    e.level = start;
    e.level_end = end;
    e.length_ms = length_ms;
    return e;
}

static void test_init(void)
{
    static unsigned char raw[PFF_MIXER_SIZE(PFF_MAX_SLOTS) + 8];
    pff_mixer_t *m = NULL;
    size_t shift;

    CHECK(pff_mixer_init(mem, sizeof(mem), 0, &m) == PFF_ERR_ARG);
    CHECK(m == NULL);
    CHECK(pff_mixer_init(mem, sizeof(mem), PFF_MAX_SLOTS + 1, &m) == PFF_ERR_ARG);
    CHECK(pff_mixer_init(NULL, sizeof(mem), 1, &m) == PFF_ERR_ARG);
    CHECK(pff_mixer_init(mem, sizeof(mem), 1, NULL) == PFF_ERR_ARG);
    CHECK(pff_mixer_init(mem, 8, SLOTS, &m) == PFF_ERR_SPACE);
    for (shift = 0; shift < 8; shift++) {
        CHECK(pff_mixer_init(raw + shift, PFF_MIXER_SIZE(PFF_MAX_SLOTS), PFF_MAX_SLOTS, &m) == PFF_OK);
        CHECK(pff_mixer_init(raw + shift, PFF_MIXER_SIZE(1), 1, &m) == PFF_OK);
    }
}

static void test_space(void)
{
    pff_mixer_t *m = NULL;
    size_t len;
    size_t first_ok = 0;
    size_t shift;

    for (shift = 0; shift < 8; shift++) {
        for (len = 0; len <= PFF_MIXER_SIZE(SLOTS); len++) {
            int status = pff_mixer_init(mem + shift, len, SLOTS, &m);

            CHECK(status == PFF_OK || status == PFF_ERR_SPACE);
            CHECK((status == PFF_OK) == (m != NULL));
            if (status == PFF_OK && first_ok == 0)
                first_ok = len;
            CHECK(first_ok == 0 || status == PFF_OK);
        }
        CHECK(first_ok > 0);
        first_ok = 0;
    }
}

static void test_rumble(void)
{
    pff_mixer_t *m = new_mixer();
    pff_effect_t e = rumble(65535, 32768, 100);

    expect(m, 0, 0, 0, 0);
    CHECK(wait_at(m, 0) == PFF_NEVER);
    CHECK(pff_mixer_upload(m, 0, &e, 0) == PFF_OK);
    CHECK(pff_mixer_play(m, 0, 1, 10) == PFF_OK);
    CHECK(wait_at(m, 10) == 0);
    expect(m, 10, 1, 65535, 32768);
    CHECK(wait_at(m, 60) == 50);
    expect(m, 60, 0, 65535, 32768);
    expect(m, 109, 0, 65535, 32768);
    expect(m, 110, 1, 0, 0);
    CHECK(wait_at(m, 110) == PFF_NEVER);
}

static void test_repeat_and_delay(void)
{
    pff_mixer_t *m = new_mixer();
    pff_effect_t e = rumble(1000, 2000, 20);

    e.delay_ms = 10;
    CHECK(pff_mixer_upload(m, 1, &e, 0) == PFF_OK);
    CHECK(pff_mixer_play(m, 1, 3, 0) == PFF_OK);
    expect(m, 0, 0, 0, 0);
    CHECK(wait_at(m, 0) == 10);
    expect(m, 10, 1, 1000, 2000);
    CHECK(wait_at(m, 10) == 20);
    expect(m, 30, 1, 0, 0);
    expect(m, 40, 1, 1000, 2000);
    expect(m, 60, 1, 0, 0);
    expect(m, 75, 1, 1000, 2000);
    expect(m, 89, 0, 1000, 2000);
    expect(m, 90, 1, 0, 0);
    expect(m, 100, 0, 0, 0);
    CHECK(wait_at(m, 100) == PFF_NEVER);
}

static void test_infinite(void)
{
    pff_mixer_t *m = new_mixer();
    pff_effect_t e = rumble(500, 600, 0);
    uint32_t t;

    CHECK(pff_mixer_upload(m, 0, &e, 0) == PFF_OK);
    CHECK(pff_mixer_play(m, 0, 1, 0) == PFF_OK);
    expect(m, 0, 1, 500, 600);
    CHECK(wait_at(m, 0) == PFF_NEVER);
    for (t = 0; t < 40; t++)
        expect(m, t * 0x10000000u, 0, 500, 600);
    CHECK(pff_mixer_play(m, 0, 0, 5) == PFF_OK);
    expect(m, 5, 1, 0, 0);
}

static void test_long_idle(void)
{
    pff_mixer_t *m = new_mixer();
    pff_effect_t endless = rumble(40000, 40000, 0);
    pff_effect_t tap = rumble(10000, 10000, 100);

    endless.attack_ms = 50;
    CHECK(pff_mixer_upload(m, 0, &endless, 0) == PFF_OK);
    CHECK(pff_mixer_play(m, 0, 1, 0) == PFF_OK);
    expect(m, 0, 0, 0, 0);
    CHECK(wait_at(m, 0x80000000u) == 0x80000000u);
    expect(m, 50, 1, 40000, 40000);
    CHECK(wait_at(m, 50) == PFF_NEVER);
    CHECK(pff_mixer_upload(m, 1, &tap, 0x80000010u) == PFF_OK);
    CHECK(pff_mixer_play(m, 1, 1, 0x80000010u) == PFF_OK);
    expect(m, 0x80000010u, 0, 40000, 40000);
    expect(m, 0xc0000000u, 0, 40000, 40000);
    expect(m, 0x00000005u, 0, 40000, 40000);
}

static void test_wraparound(void)
{
    pff_mixer_t *m = new_mixer();
    pff_effect_t e = rumble(700, 800, 50);
    uint32_t start = UINT32_MAX - 20;

    CHECK(pff_mixer_upload(m, 0, &e, start) == PFF_OK);
    CHECK(pff_mixer_play(m, 0, 2, start) == PFF_OK);
    expect(m, start, 1, 700, 800);
    expect(m, start + 49, 0, 700, 800);
    expect(m, start + 60, 0, 700, 800);
    expect(m, start + 100, 1, 0, 0);
}

static void test_levels(void)
{
    pff_mixer_t *m = new_mixer();
    pff_effect_t constant = level(PFF_CONSTANT, -16384, 0, 0);
    pff_effect_t periodic = level(PFF_PERIODIC, 32767, 0, 100);
    pff_effect_t ramp = level(PFF_RAMP, -16384, 16384, 100);

    CHECK(pff_mixer_upload(m, 0, &constant, 0) == PFF_OK);
    CHECK(pff_mixer_play(m, 0, 1, 0) == PFF_OK);
    expect(m, 0, 1, 32768, 32768);
    CHECK(pff_mixer_upload(m, 0, &periodic, 1) == PFF_OK);
    expect(m, 1, 1, 65534, 65534);
    CHECK(pff_mixer_upload(m, 0, &ramp, 2) == PFF_OK);
    expect(m, 2, 1, 32768, 32768);
    CHECK(wait_at(m, 2) == 1);
    expect(m, 52, 1, 0, 0);
    expect(m, 77, 1, 16384, 16384);
    expect(m, 102, 1, 0, 0);
    ramp = level(PFF_RAMP, -32768, -32768, 10);
    CHECK(pff_mixer_upload(m, 0, &ramp, 200) == PFF_OK);
    CHECK(pff_mixer_play(m, 0, 1, 200) == PFF_OK);
    expect(m, 200, 1, 65535, 65535);
    CHECK(wait_at(m, 200) == 10);
}

static void test_envelope(void)
{
    pff_mixer_t *m = new_mixer();
    pff_effect_t e = level(PFF_CONSTANT, 16384, 0, 1000);

    e.attack_ms = 100;
    e.attack_level = 0;
    e.fade_ms = 200;
    e.fade_level = 65535;
    CHECK(pff_mixer_upload(m, 0, &e, 0) == PFF_OK);
    CHECK(pff_mixer_play(m, 0, 1, 0) == PFF_OK);
    expect(m, 0, 0, 0, 0);
    CHECK(wait_at(m, 0) == 1);
    expect(m, 50, 1, 16384, 16384);
    expect(m, 100, 1, 32768, 32768);
    CHECK(wait_at(m, 100) == 700);
    expect(m, 800, 0, 32768, 32768);
    expect(m, 900, 1, 49152, 49152);
    expect(m, 999, 1, 65372, 65372);
    expect(m, 1000, 1, 0, 0);

    e = rumble(40000, 20000, 0);
    e.attack_ms = 10;
    e.attack_level = 40000;
    e.fade_ms = 10;
    CHECK(pff_mixer_upload(m, 1, &e, 2000) == PFF_OK);
    CHECK(pff_mixer_play(m, 1, 1, 2000) == PFF_OK);
    expect(m, 2000, 1, 40000, 40000);
    CHECK(wait_at(m, 2000) == 1);
    expect(m, 2005, 1, 40000, 30000);
    expect(m, 2010, 1, 40000, 20000);
    CHECK(wait_at(m, 2010) == PFF_NEVER);
}

static void test_slow_slope_wakes(void)
{
    static uint64_t woken_mem[PFF_MIXER_SIZE(SLOTS) / 8 + 1];
    pff_mixer_t *every = new_mixer();
    pff_mixer_t *woken = NULL;
    pff_effect_t e = level(PFF_RAMP, 0, 32767, 1000);
    uint32_t t;
    uint32_t next = 0;
    unsigned wakes = 0;

    CHECK(pff_mixer_init(woken_mem, sizeof(woken_mem), SLOTS, &woken) == PFF_OK);
    CHECK(pff_mixer_set_scale(every, 255) == PFF_OK && pff_mixer_set_scale(woken, 255) == PFF_OK);
    CHECK(pff_mixer_upload(every, 0, &e, 0) == PFF_OK && pff_mixer_upload(woken, 0, &e, 0) == PFF_OK);
    CHECK(pff_mixer_play(every, 0, 1, 0) == PFF_OK && pff_mixer_play(woken, 0, 1, 0) == PFF_OK);
    for (t = 0; t <= 1000; t++) {
        poll_t p = poll_at(every, t);

        if (t != next) {
            CHECK(!p.send);
            continue;
        }
        expect(woken, t, p.send, p.strong, p.weak);
        next = t + wait_at(woken, t);
        wakes++;
    }
    CHECK(wakes < 300);
}

static int playing_at(const pff_mixer_t *m, unsigned slot, uint32_t now)
{
    int playing = -1;

    CHECK(pff_mixer_playing(m, slot, now, &playing) == PFF_OK);
    return playing;
}

static void test_stop_and_playing(void)
{
    pff_mixer_t *m = new_mixer();
    pff_effect_t a = rumble(1000, 1000, 50);
    pff_effect_t b = rumble(2000, 2000, 0);

    a.delay_ms = 10;
    CHECK(pff_mixer_upload(m, 0, &a, 0) == PFF_OK);
    CHECK(pff_mixer_upload(m, 1, &b, 0) == PFF_OK);
    CHECK(playing_at(m, 0, 0) == 0);
    CHECK(pff_mixer_play(m, 0, 2, 0) == PFF_OK);
    CHECK(pff_mixer_play(m, 1, 1, 0) == PFF_OK);
    CHECK(playing_at(m, 0, 5) == 1);
    CHECK(playing_at(m, 0, 119) == 1);
    CHECK(playing_at(m, 0, 120) == 0);
    CHECK(playing_at(m, 1, 100000) == 1);
    expect(m, 0, 1, 2000, 2000);
    CHECK(pff_mixer_stop(m, 1) == PFF_OK);
    CHECK(playing_at(m, 1, 20) == 0);
    expect(m, 20, 1, 1000, 1000);
    CHECK(pff_mixer_stop(m, SLOTS) == PFF_ERR_SLOT);
    CHECK(pff_mixer_stop(NULL, 0) == PFF_ERR_ARG);
    CHECK(pff_mixer_playing(m, SLOTS, 0, &(int){0}) == PFF_ERR_SLOT);
    CHECK(pff_mixer_playing(m, 0, 0, NULL) == PFF_ERR_ARG);
    CHECK(pff_mixer_play(m, 1, 1, 30) == PFF_OK);
    CHECK(playing_at(m, 1, 30) == 1);
}

static void test_mix_gain_scale(void)
{
    pff_mixer_t *m = new_mixer();
    pff_effect_t a = rumble(40000, 10000, 0);
    pff_effect_t b = rumble(30000, 20000, 0);

    CHECK(pff_mixer_upload(m, 0, &a, 0) == PFF_OK);
    CHECK(pff_mixer_upload(m, 3, &b, 0) == PFF_OK);
    CHECK(pff_mixer_play(m, 0, 1, 0) == PFF_OK);
    CHECK(pff_mixer_play(m, 3, 1, 0) == PFF_OK);
    expect(m, 0, 1, 40000, 20000);
    CHECK(pff_mixer_set_mix(m, PFF_MIX_SUM) == PFF_OK);
    expect(m, 1, 1, 65535, 30000);
    CHECK(pff_mixer_set_gain(m, 32768) == PFF_OK);
    expect(m, 2, 1, 35000, 15000);
    CHECK(pff_mixer_set_scale(m, 255) == PFF_OK);
    expect(m, 3, 1, 136, 58);
    CHECK(pff_mixer_set_gain(m, 0) == PFF_OK);
    expect(m, 4, 1, 0, 0);
    CHECK(pff_mixer_set_mix(m, 2) == PFF_ERR_ARG);
    CHECK(pff_mixer_set_scale(m, 0) == PFF_ERR_ARG);
    CHECK(pff_mixer_set_gain(NULL, 1) == PFF_ERR_ARG);
    CHECK(pff_mixer_set_mix(NULL, PFF_MIX_MAX) == PFF_ERR_ARG);
    CHECK(pff_mixer_set_scale(NULL, 1) == PFF_ERR_ARG);
}

static void test_interval(void)
{
    pff_mixer_t *m = new_mixer();
    pff_effect_t a = rumble(1000, 1000, 5);
    pff_effect_t b = rumble(2000, 2000, 0);

    CHECK(pff_mixer_set_interval(m, 20) == PFF_OK);
    CHECK(pff_mixer_upload(m, 0, &a, 0) == PFF_OK);
    CHECK(pff_mixer_upload(m, 1, &b, 0) == PFF_OK);
    CHECK(pff_mixer_play(m, 0, 1, 0) == PFF_OK);
    expect(m, 0, 1, 1000, 1000);
    expect(m, 5, 0, 1000, 1000);
    CHECK(wait_at(m, 5) == 15);
    CHECK(pff_mixer_play(m, 1, 1, 6) == PFF_OK);
    expect(m, 19, 0, 1000, 1000);
    expect(m, 20, 1, 2000, 2000);
    CHECK(pff_mixer_stop_all(m) == PFF_OK);
    CHECK(wait_at(m, 21) == 19);
    expect(m, 40, 1, 0, 0);
    CHECK(pff_mixer_set_interval(NULL, 1) == PFF_ERR_ARG);
    CHECK(pff_mixer_set_interval(m, 0x80000000u) == PFF_ERR_ARG);
}

static void test_interval_after_idle(void)
{
    pff_mixer_t *m = new_mixer();
    pff_effect_t a = rumble(1000, 1000, 5);

    CHECK(pff_mixer_set_interval(m, 20) == PFF_OK);
    CHECK(pff_mixer_upload(m, 0, &a, 0) == PFF_OK);
    CHECK(pff_mixer_play(m, 0, 1, 0) == PFF_OK);
    expect(m, 0, 1, 1000, 1000);
    CHECK(wait_at(m, 0) == 20);
    CHECK(wait_at(m, 5) == 15);
    expect(m, 20, 1, 0, 0);
    CHECK(wait_at(m, 20) == 20);
    expect(m, 40, 0, 0, 0);
    CHECK(wait_at(m, 40) == PFF_NEVER);
}

static void test_commands(void)
{
    pff_mixer_t *m = new_mixer();
    pff_effect_t e = rumble(100, 100, 0);
    pff_effect_t bad = rumble(100, 100, 0);
    uint16_t s;
    uint16_t w;
    int send;
    uint32_t wait;

    bad.type = 0;
    CHECK(pff_mixer_upload(m, 0, &bad, 0) == PFF_ERR_EFFECT);
    bad.type = PFF_RAMP + 1;
    CHECK(pff_mixer_upload(m, 0, &bad, 0) == PFF_ERR_EFFECT);
    bad.type = PFF_RUMBLE;
    bad.size = 22;
    CHECK(pff_mixer_upload(m, 0, &bad, 0) == PFF_ERR_ARG);
    bad.size = 0;
    CHECK(pff_mixer_upload(m, 0, &bad, 0) == PFF_ERR_ARG);
    CHECK(pff_mixer_upload(m, SLOTS, &e, 0) == PFF_ERR_SLOT);
    CHECK(pff_mixer_upload(m, 0, NULL, 0) == PFF_ERR_ARG);
    CHECK(pff_mixer_upload(NULL, 0, &e, 0) == PFF_ERR_ARG);
    CHECK(pff_mixer_play(m, 0, 1, 0) == PFF_ERR_SLOT);
    CHECK(pff_mixer_play(m, SLOTS, 1, 0) == PFF_ERR_SLOT);
    CHECK(pff_mixer_play(NULL, 0, 1, 0) == PFF_ERR_ARG);
    CHECK(pff_mixer_erase(m, SLOTS) == PFF_ERR_SLOT);
    CHECK(pff_mixer_erase(NULL, 0) == PFF_ERR_ARG);
    CHECK(pff_mixer_stop_all(NULL) == PFF_ERR_ARG);
    CHECK(pff_mixer_upload(m, 2, &e, 0) == PFF_OK);
    CHECK(pff_mixer_play(m, 2, 1, 0) == PFF_OK);
    expect(m, 0, 1, 100, 100);
    CHECK(pff_mixer_erase(m, 2) == PFF_OK);
    CHECK(pff_mixer_erase(m, 2) == PFF_OK);
    CHECK(pff_mixer_play(m, 2, 1, 0) == PFF_ERR_SLOT);
    expect(m, 1, 1, 0, 0);
    CHECK(pff_mixer_poll(NULL, 0, &s, &w, &send) == PFF_ERR_ARG);
    CHECK(pff_mixer_poll(m, 0, NULL, &w, &send) == PFF_ERR_ARG);
    CHECK(pff_mixer_poll(m, 0, &s, NULL, &send) == PFF_ERR_ARG);
    CHECK(pff_mixer_poll(m, 0, &s, &w, NULL) == PFF_ERR_ARG);
    CHECK(pff_mixer_next(NULL, 0, &wait) == PFF_ERR_ARG);
    CHECK(pff_mixer_next(m, 0, NULL) == PFF_ERR_ARG);
}

static void test_upload_while_playing(void)
{
    pff_mixer_t *m = new_mixer();
    pff_effect_t e = rumble(100, 100, 50);

    CHECK(pff_mixer_upload(m, 0, &e, 0) == PFF_OK);
    CHECK(pff_mixer_play(m, 0, 1, 0) == PFF_OK);
    expect(m, 0, 1, 100, 100);
    e.strong = 300;
    CHECK(pff_mixer_upload(m, 0, &e, 40) == PFF_OK);
    expect(m, 40, 1, 300, 100);
    expect(m, 89, 0, 300, 100);
    expect(m, 90, 1, 0, 0);
}

static void test_upload_after_end(void)
{
    pff_mixer_t *m = new_mixer();
    pff_effect_t e = rumble(100, 100, 50);

    CHECK(pff_mixer_upload(m, 0, &e, 0) == PFF_OK);
    CHECK(pff_mixer_play(m, 0, 1, 0) == PFF_OK);
    e.strong = 300;
    CHECK(pff_mixer_upload(m, 0, &e, 80) == PFF_OK);
    expect(m, 80, 0, 0, 0);
    CHECK(wait_at(m, 80) == PFF_NEVER);
}

static void test_strerror(void)
{
    int status;

    for (status = PFF_OK; status <= PFF_ERR_EFFECT; status++)
        CHECK(strcmp(pff_strerror(status), "unknown status") != 0);
    CHECK(strcmp(pff_strerror(-1), "unknown status") == 0);
}

int main(void)
{
    test_init();
    test_space();
    test_rumble();
    test_repeat_and_delay();
    test_infinite();
    test_long_idle();
    test_wraparound();
    test_levels();
    test_envelope();
    test_slow_slope_wakes();
    test_stop_and_playing();
    test_mix_gain_scale();
    test_interval();
    test_interval_after_idle();
    test_commands();
    test_upload_while_playing();
    test_upload_after_end();
    test_strerror();
    return 0;
}
