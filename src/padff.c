#include "padff/padff.h"

#include <string.h>

#define PFF_FULL 65535u
#define PFF_ALIGN 8u
#define PFF_SLOPE_SCAN 64u

struct pff_slot {
    pff_effect_t effect;
    uint8_t loaded;
    uint8_t playing;
    uint8_t steady;
    uint32_t start;
    uint32_t count;
};

struct pff_mixer {
    unsigned slots;
    int mix;
    uint16_t gain;
    uint16_t motor_max;
    uint16_t sent_strong;
    uint16_t sent_weak;
    uint8_t has_sent;
    uint32_t interval;
    uint32_t last_send;
    struct pff_slot slot[];
};

typedef struct {
    uint32_t strong;
    uint32_t weak;
    uint32_t wait;
} pff_sample_t;

typedef char pff_effect_size_check[sizeof(pff_effect_t) == 24 && offsetof(pff_effect_t, size) == 0 ? 1 : -1];
typedef char pff_slot_size_check[sizeof(struct pff_slot) <= 48 ? 1 : -1];
typedef char pff_head_size_check[offsetof(struct pff_mixer, slot) + PFF_ALIGN - 1 <= 64 ? 1 : -1];

static uint32_t pff_min(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static uint32_t pff_level_intensity(int32_t level)
{
    uint32_t magnitude = (uint32_t)(level < 0 ? -level : level) * 2u;

    return pff_min(magnitude, PFF_FULL);
}

static uint32_t pff_lerp(uint32_t from, uint32_t to, uint32_t pos, uint32_t span)
{
    int64_t diff = (int64_t)to - (int64_t)from;

    return (uint32_t)((int64_t)from + diff * (int64_t)pos / (int64_t)span);
}

static uint32_t pff_base_level(const pff_effect_t *e, uint32_t u, int *sloping)
{
    int32_t level = e->level;

    if (e->type == PFF_RAMP && e->length_ms > 0 && e->level != e->level_end) {
        *sloping = 1;
        level = (int32_t)((int64_t)e->level + ((int64_t)e->level_end - e->level) * u / e->length_ms);
    }
    return pff_level_intensity(level);
}

static void pff_apply_envelope(const pff_effect_t *e, uint32_t u, pff_sample_t *out, int *sloping)
{
    uint32_t len = e->length_ms;
    uint32_t left = len - u;

    if (e->attack_ms > 0 && u < e->attack_ms) {
        *sloping |= out->strong != e->attack_level || out->weak != e->attack_level;
        out->strong = pff_lerp(e->attack_level, out->strong, u, e->attack_ms);
        out->weak = pff_lerp(e->attack_level, out->weak, u, e->attack_ms);
        out->wait = pff_min(out->wait, e->attack_ms - u);
        return;
    }
    if (e->fade_ms == 0 || len == 0)
        return;
    if (left > e->fade_ms) {
        out->wait = pff_min(out->wait, left - e->fade_ms);
        return;
    }
    *sloping |= out->strong != e->fade_level || out->weak != e->fade_level;
    out->strong = pff_lerp(e->fade_level, out->strong, left, e->fade_ms);
    out->weak = pff_lerp(e->fade_level, out->weak, left, e->fade_ms);
}

static void pff_effect_sample(const pff_effect_t *e, uint32_t u, pff_sample_t *out)
{
    int sloping = 0;

    out->wait = e->length_ms > 0 ? (uint32_t)e->length_ms - u : PFF_NEVER;
    if (e->type == PFF_RUMBLE) {
        out->strong = e->strong;
        out->weak = e->weak;
    } else {
        out->strong = pff_base_level(e, u, &sloping);
        out->weak = out->strong;
    }
    pff_apply_envelope(e, u, out, &sloping);
    if (sloping)
        out->wait = 1;
}

static void pff_slot_sample(const struct pff_slot *s, uint32_t now, pff_sample_t *out)
{
    const pff_effect_t *e = &s->effect;
    int32_t since = (int32_t)(now - s->start);
    uint32_t t;

    out->strong = 0;
    out->weak = 0;
    out->wait = PFF_NEVER;
    if (!s->playing)
        return;
    if (s->steady) {
        pff_effect_sample(e, e->attack_ms, out);
        return;
    }
    if (since < 0) {
        out->wait = s->start - now;
        return;
    }
    t = (uint32_t)since;
    if (e->length_ms > 0) {
        uint32_t period = (uint32_t)e->delay_ms + e->length_ms;

        if (t / period >= s->count)
            return;
        t %= period;
    }
    if (t < e->delay_ms) {
        out->wait = e->delay_ms - t;
        return;
    }
    pff_effect_sample(e, t - e->delay_ms, out);
}

static void pff_slot_rebase(struct pff_slot *s, uint32_t now)
{
    const pff_effect_t *e = &s->effect;
    int32_t since = (int32_t)(now - s->start);
    uint32_t steady = (uint32_t)e->delay_ms + e->attack_ms;
    uint32_t period = (uint32_t)e->delay_ms + e->length_ms;
    uint32_t reps;

    if (!s->playing || s->steady || since < 0)
        return;
    if (e->length_ms == 0) {
        s->steady |= (uint32_t)since >= steady;
        return;
    }
    reps = (uint32_t)since / period;
    if (reps >= s->count) {
        s->playing = 0;
        return;
    }
    s->start += reps * period;
    s->count -= reps;
}

static uint32_t pff_mix(int mix, uint32_t acc, uint32_t value)
{
    if (mix == PFF_MIX_SUM)
        return pff_min(acc + value, PFF_FULL);
    return acc > value ? acc : value;
}

static uint16_t pff_quantize(const pff_mixer_t *m, uint32_t value)
{
    return (uint16_t)((value * m->motor_max + PFF_FULL / 2) / PFF_FULL);
}

static void pff_mixer_sample(const pff_mixer_t *m, uint32_t now, pff_sample_t *out)
{
    unsigned i;
    pff_sample_t one;

    out->strong = 0;
    out->weak = 0;
    out->wait = PFF_NEVER;
    for (i = 0; i < m->slots; i++) {
        pff_slot_sample(&m->slot[i], now, &one);
        out->strong = pff_mix(m->mix, out->strong, one.strong * m->gain / PFF_FULL);
        out->weak = pff_mix(m->mix, out->weak, one.weak * m->gain / PFF_FULL);
        out->wait = pff_min(out->wait, one.wait);
    }
    out->strong = pff_quantize(m, out->strong);
    out->weak = pff_quantize(m, out->weak);
}

static uint32_t pff_rate_left(const pff_mixer_t *m, uint32_t now)
{
    uint32_t elapsed = now - m->last_send;

    if (!m->has_sent || elapsed >= m->interval)
        return 0;
    return m->interval - elapsed;
}

static struct pff_slot *pff_get_slot(pff_mixer_t *m, unsigned slot)
{
    if (m == NULL || slot >= m->slots)
        return NULL;
    return &m->slot[slot];
}

int pff_mixer_init(void *mem, size_t mem_len, unsigned slots, pff_mixer_t **out_mixer)
{
    uintptr_t addr = (uintptr_t)mem;
    size_t pad = (size_t)((PFF_ALIGN - addr % PFF_ALIGN) % PFF_ALIGN);
    size_t need = offsetof(struct pff_mixer, slot) + (size_t)slots * sizeof(struct pff_slot);
    pff_mixer_t *m;

    if (out_mixer == NULL)
        return PFF_ERR_ARG;
    *out_mixer = NULL;
    if (mem == NULL || slots == 0 || slots > PFF_MAX_SLOTS)
        return PFF_ERR_ARG;
    if (mem_len < pad || mem_len - pad < need)
        return PFF_ERR_SPACE;
    m = (pff_mixer_t *)(void *)((unsigned char *)mem + pad);
    memset(m, 0, need);
    m->slots = slots;
    m->mix = PFF_MIX_MAX;
    m->gain = PFF_FULL;
    m->motor_max = PFF_FULL;
    *out_mixer = m;
    return PFF_OK;
}

int pff_mixer_set_mix(pff_mixer_t *mixer, int mix)
{
    if (mixer == NULL || (mix != PFF_MIX_MAX && mix != PFF_MIX_SUM))
        return PFF_ERR_ARG;
    mixer->mix = mix;
    return PFF_OK;
}

int pff_mixer_set_gain(pff_mixer_t *mixer, uint16_t gain)
{
    if (mixer == NULL)
        return PFF_ERR_ARG;
    mixer->gain = gain;
    return PFF_OK;
}

int pff_mixer_set_scale(pff_mixer_t *mixer, uint16_t motor_max)
{
    if (mixer == NULL || motor_max == 0)
        return PFF_ERR_ARG;
    mixer->motor_max = motor_max;
    return PFF_OK;
}

int pff_mixer_set_interval(pff_mixer_t *mixer, uint32_t interval_ms)
{
    if (mixer == NULL || interval_ms > INT32_MAX)
        return PFF_ERR_ARG;
    mixer->interval = interval_ms;
    return PFF_OK;
}

int pff_mixer_upload(pff_mixer_t *mixer, unsigned slot, const pff_effect_t *effect, uint32_t now_ms)
{
    struct pff_slot *s;

    if (mixer == NULL || effect == NULL || effect->size != sizeof(pff_effect_t))
        return PFF_ERR_ARG;
    s = pff_get_slot(mixer, slot);
    if (s == NULL)
        return PFF_ERR_SLOT;
    if (effect->type < PFF_RUMBLE || effect->type > PFF_RAMP)
        return PFF_ERR_EFFECT;
    pff_slot_rebase(s, now_ms);
    s->effect = *effect;
    s->loaded = 1;
    s->steady = 0;
    s->start = now_ms;
    return PFF_OK;
}

int pff_mixer_erase(pff_mixer_t *mixer, unsigned slot)
{
    struct pff_slot *s;

    if (mixer == NULL)
        return PFF_ERR_ARG;
    s = pff_get_slot(mixer, slot);
    if (s == NULL)
        return PFF_ERR_SLOT;
    s->loaded = 0;
    s->playing = 0;
    return PFF_OK;
}

int pff_mixer_play(pff_mixer_t *mixer, unsigned slot, uint32_t count, uint32_t now_ms)
{
    struct pff_slot *s;

    if (mixer == NULL)
        return PFF_ERR_ARG;
    s = pff_get_slot(mixer, slot);
    if (s == NULL || !s->loaded)
        return PFF_ERR_SLOT;
    s->playing = count > 0;
    s->steady = 0;
    s->start = now_ms;
    s->count = count;
    return PFF_OK;
}

int pff_mixer_stop_all(pff_mixer_t *mixer)
{
    unsigned i;

    if (mixer == NULL)
        return PFF_ERR_ARG;
    for (i = 0; i < mixer->slots; i++)
        mixer->slot[i].playing = 0;
    return PFF_OK;
}

int pff_mixer_poll(pff_mixer_t *mixer, uint32_t now_ms, uint16_t *out_strong, uint16_t *out_weak,
                   int *out_send)
{
    pff_sample_t now;
    unsigned i;

    if (mixer == NULL || out_strong == NULL || out_weak == NULL || out_send == NULL)
        return PFF_ERR_ARG;
    for (i = 0; i < mixer->slots; i++)
        pff_slot_rebase(&mixer->slot[i], now_ms);
    if (pff_rate_left(mixer, now_ms) == 0)
        mixer->has_sent = 0;
    pff_mixer_sample(mixer, now_ms, &now);
    *out_send = (now.strong != mixer->sent_strong || now.weak != mixer->sent_weak) && !mixer->has_sent;
    if (*out_send) {
        mixer->sent_strong = (uint16_t)now.strong;
        mixer->sent_weak = (uint16_t)now.weak;
        mixer->last_send = now_ms;
        mixer->has_sent = 1;
    }
    *out_strong = mixer->sent_strong;
    *out_weak = mixer->sent_weak;
    return PFF_OK;
}

static uint32_t pff_slope_wait(const pff_mixer_t *m, uint32_t now, const pff_sample_t *at)
{
    pff_sample_t later;
    uint32_t k;

    for (k = 1; k < PFF_SLOPE_SCAN; k++) {
        pff_mixer_sample(m, now + k, &later);
        if (later.strong != at->strong || later.weak != at->weak || later.wait != 1)
            return k;
    }
    return k;
}

int pff_mixer_next(const pff_mixer_t *mixer, uint32_t now_ms, uint32_t *out_wait_ms)
{
    pff_sample_t now;
    uint32_t rate_left;

    if (mixer == NULL || out_wait_ms == NULL)
        return PFF_ERR_ARG;
    pff_mixer_sample(mixer, now_ms, &now);
    rate_left = pff_rate_left(mixer, now_ms);
    if (now.wait == 1)
        now.wait = pff_slope_wait(mixer, now_ms, &now);
    if (now.strong != mixer->sent_strong || now.weak != mixer->sent_weak)
        *out_wait_ms = rate_left;
    else if (now.wait == PFF_NEVER)
        *out_wait_ms = rate_left > 0 ? rate_left : PFF_NEVER;
    else
        *out_wait_ms = now.wait > rate_left ? now.wait : rate_left;
    return PFF_OK;
}

const char *pff_strerror(int status)
{
    switch (status) {
    case PFF_OK:
        return "ok";
    case PFF_ERR_ARG:
        return "invalid argument";
    case PFF_ERR_SPACE:
        return "memory block too small";
    case PFF_ERR_SLOT:
        return "slot out of range or empty";
    case PFF_ERR_EFFECT:
        return "unknown effect type";
    default:
        return "unknown status";
    }
}
