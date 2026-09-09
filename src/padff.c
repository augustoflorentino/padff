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
    unsigned motors;
    int mix;
    uint16_t gain;
    uint16_t motor_max;
    uint16_t trigger_max;
    uint16_t sent[PFF_MOTORS];
    uint8_t has_sent;
    uint32_t interval;
    uint32_t last_send;
    struct pff_slot slot[];
};

typedef struct {
    uint32_t motor[PFF_MOTORS];
    uint32_t wait;
} pff_sample_t;

#define PFF_EFFECT_SIZE_1 24u

typedef char pff_effect_size_check[sizeof(pff_effect_t) == 28 && offsetof(pff_effect_t, left_trigger) == PFF_EFFECT_SIZE_1 ? 1 : -1];
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

static int pff_envelope(pff_sample_t *out, uint32_t level, uint32_t pos, uint32_t span)
{
    int sloping = 0;
    unsigned i;

    for (i = 0; i < PFF_MOTORS; i++) {
        if (i >= PFF_MOTOR_LEFT_TRIGGER && out->motor[i] == 0)
            continue;
        sloping |= out->motor[i] != level;
        out->motor[i] = pff_lerp(level, out->motor[i], pos, span);
    }
    return sloping;
}

static void pff_apply_envelope(const pff_effect_t *e, uint32_t u, pff_sample_t *out, int *sloping)
{
    uint32_t len = e->length_ms;
    uint32_t left = len - u;

    if (e->attack_ms > 0 && u < e->attack_ms) {
        *sloping |= pff_envelope(out, e->attack_level, u, e->attack_ms);
        out->wait = pff_min(out->wait, e->attack_ms - u);
        return;
    }
    if (e->fade_ms == 0 || len == 0)
        return;
    if (left > e->fade_ms) {
        out->wait = pff_min(out->wait, left - e->fade_ms);
        return;
    }
    *sloping |= pff_envelope(out, e->fade_level, left, e->fade_ms);
}

static void pff_effect_sample(const pff_effect_t *e, uint32_t u, pff_sample_t *out)
{
    int sloping = 0;

    out->wait = e->length_ms > 0 ? (uint32_t)e->length_ms - u : PFF_NEVER;
    if (e->type == PFF_RUMBLE) {
        out->motor[PFF_MOTOR_STRONG] = e->strong;
        out->motor[PFF_MOTOR_WEAK] = e->weak;
        out->motor[PFF_MOTOR_LEFT_TRIGGER] = e->left_trigger;
        out->motor[PFF_MOTOR_RIGHT_TRIGGER] = e->right_trigger;
    } else {
        out->motor[PFF_MOTOR_STRONG] = pff_base_level(e, u, &sloping);
        out->motor[PFF_MOTOR_WEAK] = out->motor[PFF_MOTOR_STRONG];
        out->motor[PFF_MOTOR_LEFT_TRIGGER] = 0;
        out->motor[PFF_MOTOR_RIGHT_TRIGGER] = 0;
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

    memset(out->motor, 0, sizeof(out->motor));
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

static uint16_t pff_quantize(const pff_mixer_t *m, unsigned motor, uint32_t value)
{
    uint32_t max = motor >= PFF_MOTOR_LEFT_TRIGGER ? m->trigger_max : m->motor_max;

    return (uint16_t)((value * max + PFF_FULL / 2) / PFF_FULL);
}

static void pff_mixer_sample(const pff_mixer_t *m, uint32_t now, pff_sample_t *out)
{
    unsigned i;
    unsigned k;
    pff_sample_t one;

    memset(out->motor, 0, sizeof(out->motor));
    out->wait = PFF_NEVER;
    for (i = 0; i < m->slots; i++) {
        pff_slot_sample(&m->slot[i], now, &one);
        for (k = 0; k < PFF_MOTORS; k++)
            out->motor[k] = pff_mix(m->mix, out->motor[k], one.motor[k] * m->gain / PFF_FULL);
        out->wait = pff_min(out->wait, one.wait);
    }
    for (k = 0; k < PFF_MOTORS; k++)
        out->motor[k] = pff_quantize(m, k, out->motor[k]);
}

static int pff_differs(const pff_mixer_t *m, const uint32_t *a, const uint16_t *b)
{
    unsigned k;

    for (k = 0; k < m->motors; k++) {
        if (a[k] != b[k])
            return 1;
    }
    return 0;
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

#define PFF_CONFIG_SIZE_3 20u

static int pff_config_load(const pff_config_t *c, pff_config_t *out)
{
    if (c->size != sizeof(pff_config_t) && c->size != PFF_CONFIG_SIZE_3)
        return 0;
    memset(out, 0, sizeof(*out));
    memcpy(out, c, c->size);
    if (out->trigger_max > PFF_FULL)
        return 0;
    if (out->mix != PFF_MIX_MAX && out->mix != PFF_MIX_SUM)
        return 0;
    if (out->motors > PFF_MOTORS)
        return 0;
    return out->motor_max <= PFF_FULL && out->interval_ms <= INT32_MAX;
}

static void pff_config_apply(pff_mixer_t *m, const pff_config_t *c)
{
    unsigned i;

    m->mix = (int)c->mix;
    m->motor_max = (uint16_t)(c->motor_max == 0 ? PFF_FULL : c->motor_max);
    m->trigger_max = (uint16_t)(c->trigger_max == 0 ? m->motor_max : c->trigger_max);
    m->interval = c->interval_ms;
    m->motors = c->motors == 0 ? 2u : c->motors;
    for (i = m->motors; i < PFF_MOTORS; i++)
        m->sent[i] = 0;
}

int pff_mixer_init(void *mem, size_t mem_len, unsigned slots, const pff_config_t *config,
                   pff_mixer_t **out_mixer)
{
    pff_config_t loaded;
    uintptr_t addr = (uintptr_t)mem;
    size_t pad = (size_t)((PFF_ALIGN - addr % PFF_ALIGN) % PFF_ALIGN);
    size_t need = offsetof(struct pff_mixer, slot) + (size_t)slots * sizeof(struct pff_slot);
    pff_mixer_t *m;

    if (out_mixer == NULL)
        return PFF_ERR_ARG;
    *out_mixer = NULL;
    if (mem == NULL || slots == 0 || slots > PFF_MAX_SLOTS)
        return PFF_ERR_ARG;
    if (config != NULL && !pff_config_load(config, &loaded))
        return PFF_ERR_ARG;
    if (mem_len < pad || mem_len - pad < need)
        return PFF_ERR_SPACE;
    m = (pff_mixer_t *)(void *)((unsigned char *)mem + pad);
    memset(m, 0, need);
    m->slots = slots;
    m->mix = PFF_MIX_MAX;
    m->gain = PFF_FULL;
    m->motor_max = PFF_FULL;
    m->trigger_max = PFF_FULL;
    m->motors = 2;
    if (config != NULL)
        pff_config_apply(m, &loaded);
    *out_mixer = m;
    return PFF_OK;
}

int pff_mixer_configure(pff_mixer_t *mixer, const pff_config_t *config)
{
    pff_config_t loaded;

    if (mixer == NULL || config == NULL || !pff_config_load(config, &loaded))
        return PFF_ERR_ARG;
    pff_config_apply(mixer, &loaded);
    return PFF_OK;
}

int pff_mixer_set_gain(pff_mixer_t *mixer, uint16_t gain)
{
    if (mixer == NULL)
        return PFF_ERR_ARG;
    mixer->gain = gain;
    return PFF_OK;
}

int pff_mixer_upload(pff_mixer_t *mixer, unsigned slot, const pff_effect_t *effect, uint32_t now_ms)
{
    struct pff_slot *s;

    if (mixer == NULL || effect == NULL)
        return PFF_ERR_ARG;
    if (effect->size != sizeof(pff_effect_t) && effect->size != PFF_EFFECT_SIZE_1)
        return PFF_ERR_ARG;
    s = pff_get_slot(mixer, slot);
    if (s == NULL)
        return PFF_ERR_SLOT;
    if (effect->type < PFF_RUMBLE || effect->type > PFF_RAMP)
        return PFF_ERR_EFFECT;
    pff_slot_rebase(s, now_ms);
    memset(&s->effect, 0, sizeof(s->effect));
    memcpy(&s->effect, effect, effect->size);
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

int pff_mixer_stop(pff_mixer_t *mixer, unsigned slot)
{
    struct pff_slot *s;

    if (mixer == NULL)
        return PFF_ERR_ARG;
    s = pff_get_slot(mixer, slot);
    if (s == NULL)
        return PFF_ERR_SLOT;
    s->playing = 0;
    return PFF_OK;
}

static int pff_slot_active(const struct pff_slot *s, uint32_t now)
{
    const pff_effect_t *e = &s->effect;
    int32_t since = (int32_t)(now - s->start);
    uint32_t period = (uint32_t)e->delay_ms + e->length_ms;

    if (!s->playing)
        return 0;
    if (s->steady || since < 0 || e->length_ms == 0)
        return 1;
    return (uint32_t)since / period < s->count;
}

int pff_mixer_playing(const pff_mixer_t *mixer, unsigned slot, uint32_t now_ms, int *out_playing)
{
    if (mixer == NULL || out_playing == NULL)
        return PFF_ERR_ARG;
    if (slot >= mixer->slots)
        return PFF_ERR_SLOT;
    *out_playing = pff_slot_active(&mixer->slot[slot], now_ms);
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

int pff_mixer_poll(pff_mixer_t *mixer, uint32_t now_ms, uint16_t *out_motors, unsigned count, int *out_send)
{
    pff_sample_t now;
    unsigned i;

    if (mixer == NULL || out_motors == NULL || out_send == NULL || count == 0 || count > PFF_MOTORS)
        return PFF_ERR_ARG;
    for (i = 0; i < mixer->slots; i++)
        pff_slot_rebase(&mixer->slot[i], now_ms);
    if (pff_rate_left(mixer, now_ms) == 0)
        mixer->has_sent = 0;
    pff_mixer_sample(mixer, now_ms, &now);
    *out_send = pff_differs(mixer, now.motor, mixer->sent) && !mixer->has_sent;
    if (*out_send) {
        for (i = 0; i < PFF_MOTORS; i++)
            mixer->sent[i] = (uint16_t)(i < mixer->motors ? now.motor[i] : 0);
        mixer->last_send = now_ms;
        mixer->has_sent = 1;
    }
    memcpy(out_motors, mixer->sent, count * sizeof(*out_motors));
    return PFF_OK;
}

static uint32_t pff_slope_wait(const pff_mixer_t *m, uint32_t now, const pff_sample_t *at)
{
    pff_sample_t later;
    uint32_t k;

    for (k = 1; k < PFF_SLOPE_SCAN; k++) {
        pff_mixer_sample(m, now + k, &later);
        if (memcmp(later.motor, at->motor, m->motors * sizeof(at->motor[0])) != 0 || later.wait != 1)
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
    if (pff_differs(mixer, now.motor, mixer->sent))
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
