#ifndef PADFF_H
#define PADFF_H

#include <stddef.h>
#include <stdint.h>

#if defined(PFF_STATIC) && defined(__GNUC__)
#define PFF_API static __attribute__((unused))
#elif defined(PFF_STATIC)
#define PFF_API static
#elif defined(_WIN32) && defined(PFF_BUILD_SHARED)
#define PFF_API __declspec(dllexport)
#elif defined(_WIN32) && defined(PFF_SHARED)
#define PFF_API __declspec(dllimport)
#elif defined(__GNUC__) && defined(PFF_BUILD_SHARED)
#define PFF_API __attribute__((visibility("default")))
#else
#define PFF_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define PFF_OK 0
#define PFF_ERR_ARG 1
#define PFF_ERR_SPACE 2
#define PFF_ERR_SLOT 3
#define PFF_ERR_EFFECT 4

#define PFF_RUMBLE 1
#define PFF_PERIODIC 2
#define PFF_CONSTANT 3
#define PFF_RAMP 4

#define PFF_MIX_MAX 0
#define PFF_MIX_SUM 1

#define PFF_MAX_SLOTS 1024
#define PFF_NEVER UINT32_MAX
#define PFF_MIXER_SIZE(slots) (64 + (size_t)(slots) * 48)

typedef struct pff_effect {
    uint16_t size;
    uint16_t type;
    uint16_t strong;
    uint16_t weak;
    int16_t level;
    int16_t level_end;
    uint16_t delay_ms;
    uint16_t length_ms;
    uint16_t attack_ms;
    uint16_t attack_level;
    uint16_t fade_ms;
    uint16_t fade_level;
} pff_effect_t;

typedef struct pff_mixer pff_mixer_t;

PFF_API int pff_mixer_init(void *mem, size_t mem_len, unsigned slots, pff_mixer_t **out_mixer);
PFF_API int pff_mixer_set_mix(pff_mixer_t *mixer, int mix);
PFF_API int pff_mixer_set_gain(pff_mixer_t *mixer, uint16_t gain);
PFF_API int pff_mixer_set_scale(pff_mixer_t *mixer, uint16_t motor_max);
PFF_API int pff_mixer_set_interval(pff_mixer_t *mixer, uint32_t interval_ms);
PFF_API int pff_mixer_upload(pff_mixer_t *mixer, unsigned slot, const pff_effect_t *effect, uint32_t now_ms);
PFF_API int pff_mixer_erase(pff_mixer_t *mixer, unsigned slot);
PFF_API int pff_mixer_play(pff_mixer_t *mixer, unsigned slot, uint32_t count, uint32_t now_ms);
PFF_API int pff_mixer_stop(pff_mixer_t *mixer, unsigned slot);
PFF_API int pff_mixer_stop_all(pff_mixer_t *mixer);
PFF_API int pff_mixer_playing(const pff_mixer_t *mixer, unsigned slot, uint32_t now_ms, int *out_playing);
PFF_API int pff_mixer_poll(pff_mixer_t *mixer, uint32_t now_ms, uint16_t *out_strong, uint16_t *out_weak,
                           int *out_send);
PFF_API int pff_mixer_next(const pff_mixer_t *mixer, uint32_t now_ms, uint32_t *out_wait_ms);
PFF_API const char *pff_strerror(int status);

#ifdef __cplusplus
}
#endif

#endif
