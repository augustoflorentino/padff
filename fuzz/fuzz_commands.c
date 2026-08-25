#include "padff/padff.h"

#include <stdlib.h>
#include <string.h>

#define FUZZ_SLOTS 4

typedef struct {
    const uint8_t *data;
    size_t size;
} fuzz_input_t;

static uint32_t fuzz_take(fuzz_input_t *in, size_t bytes)
{
    uint32_t value = 0;

    while (bytes-- > 0 && in->size > 0) {
        value = value << 8 | *in->data++;
        in->size--;
    }
    return value;
}

static void fuzz_upload(pff_mixer_t *m, fuzz_input_t *in, uint32_t now)
{
    pff_effect_t e;
    unsigned slot = fuzz_take(in, 1) % (FUZZ_SLOTS + 1);

    e.size = fuzz_take(in, 1) == 0 ? 0 : sizeof(e);
    e.type = (uint16_t)(fuzz_take(in, 1) % 6);
    e.strong = (uint16_t)fuzz_take(in, 2);
    e.weak = (uint16_t)fuzz_take(in, 2);
    e.level = (int16_t)fuzz_take(in, 2);
    e.level_end = (int16_t)fuzz_take(in, 2);
    e.delay_ms = (uint16_t)fuzz_take(in, 2);
    e.length_ms = (uint16_t)fuzz_take(in, 2);
    e.attack_ms = (uint16_t)fuzz_take(in, 2);
    e.attack_level = (uint16_t)fuzz_take(in, 2);
    e.fade_ms = (uint16_t)fuzz_take(in, 2);
    e.fade_level = (uint16_t)fuzz_take(in, 2);
    pff_mixer_upload(m, slot, &e, now);
}

static void fuzz_configure(pff_mixer_t *m, fuzz_input_t *in)
{
    pff_config_t c;

    c.size = fuzz_take(in, 1) == 0 ? 0 : sizeof(c);
    c.mix = fuzz_take(in, 1) % 3;
    c.motor_max = fuzz_take(in, 3) % 0x10002;
    c.interval_ms = fuzz_take(in, 1) % 32;
    pff_mixer_configure(m, &c);
}

static void fuzz_check_quiet(pff_mixer_t *m, uint32_t now, uint32_t until)
{
    uint16_t strong;
    uint16_t weak;
    int send;

    for (; now != until; now++) {
        pff_mixer_poll(m, now, &strong, &weak, &send);
        if (send)
            abort();
    }
}

static void fuzz_check_idle(const pff_mixer_t *m, uint32_t now, uint16_t strong, uint16_t weak, int send)
{
    unsigned i;
    int playing = 0;
    int any = 0;

    for (i = 0; i < FUZZ_SLOTS; i++) {
        pff_mixer_playing(m, i, now, &playing);
        any |= playing;
    }
    if (send && !any && (strong != 0 || weak != 0))
        abort();
}

static uint32_t fuzz_advance(pff_mixer_t *m, fuzz_input_t *in, uint32_t now)
{
    uint32_t step = fuzz_take(in, 1);
    uint32_t wait = 0;
    uint16_t strong;
    uint16_t weak;
    int send;

    pff_mixer_poll(m, now, &strong, &weak, &send);
    fuzz_check_idle(m, now, strong, weak, send);
    pff_mixer_next(m, now, &wait);
    if (wait > 0 && wait != PFF_NEVER)
        fuzz_check_quiet(m, now, now + (wait < step ? wait : step));
    if (wait == PFF_NEVER)
        fuzz_check_quiet(m, now, now + step);
    return now + step;
}

static void fuzz_command(pff_mixer_t *m, fuzz_input_t *in, uint32_t *now)
{
    switch (fuzz_take(in, 1) % 10) {
    case 0:
        fuzz_upload(m, in, *now);
        break;
    case 1:
        pff_mixer_erase(m, fuzz_take(in, 1) % (FUZZ_SLOTS + 1));
        break;
    case 2:
        pff_mixer_play(m, fuzz_take(in, 1) % (FUZZ_SLOTS + 1), fuzz_take(in, 1) % 4, *now);
        break;
    case 3:
        pff_mixer_set_gain(m, (uint16_t)fuzz_take(in, 2));
        break;
    case 4:
    case 5:
    case 6:
        fuzz_configure(m, in);
        break;
    case 7:
        if (fuzz_take(in, 1) % 2)
            pff_mixer_stop_all(m);
        else
            pff_mixer_stop(m, fuzz_take(in, 1) % (FUZZ_SLOTS + 1));
        break;
    case 8:
        *now += fuzz_take(in, 4);
        break;
    default:
        *now = fuzz_advance(m, in, *now);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static unsigned char mem[PFF_MIXER_SIZE(FUZZ_SLOTS)];
    pff_mixer_t *m = NULL;
    fuzz_input_t in = {data, size};
    uint32_t now = fuzz_take(&in, 4);

    if (pff_mixer_init(mem, sizeof(mem), FUZZ_SLOTS, NULL, &m) != PFF_OK)
        abort();
    while (in.size > 0)
        fuzz_command(m, &in, &now);
    return 0;
}
