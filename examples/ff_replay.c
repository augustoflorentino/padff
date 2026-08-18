#include "padff/padff.h"

#include <stdio.h>
#include <string.h>

enum { CMD_UPLOAD, CMD_PLAY, CMD_STOP, CMD_GAIN, CMD_ERASE };

typedef struct {
    uint32_t ms;
    int cmd;
    unsigned id;
    uint32_t value;
    uint16_t strong;
    uint16_t weak;
    uint16_t length_ms;
} command_t;

static const command_t commands[] = {
    {0, CMD_UPLOAD, 0, 0, 0xc000, 0x4000, 300},
    {0, CMD_UPLOAD, 1, 0, 0x0000, 0x8000, 0},
    {0, CMD_PLAY, 0, 2, 0, 0, 0},
    {120, CMD_PLAY, 1, 1, 0, 0, 0},
    {400, CMD_GAIN, 0, 0x8000, 0, 0, 0},
    {700, CMD_STOP, 1, 0, 0, 0, 0},
    {900, CMD_ERASE, 0, 0, 0, 0, 0},
};

static void apply(pff_mixer_t *mixer, const command_t *c)
{
    pff_effect_t effect;

    switch (c->cmd) {
    case CMD_UPLOAD:
        memset(&effect, 0, sizeof(effect));
        effect.size = sizeof(effect);
        effect.type = PFF_RUMBLE;
        effect.strong = c->strong;
        effect.weak = c->weak;
        effect.length_ms = c->length_ms;
        pff_mixer_upload(mixer, c->id, &effect, c->ms);
        break;
    case CMD_PLAY:
        pff_mixer_play(mixer, c->id, c->value, c->ms);
        break;
    case CMD_STOP:
        pff_mixer_stop(mixer, c->id);
        break;
    case CMD_GAIN:
        pff_mixer_set_gain(mixer, (uint16_t)c->value);
        break;
    default:
        pff_mixer_erase(mixer, c->id);
        break;
    }
}

int main(void)
{
    static unsigned char mem[PFF_MIXER_SIZE(4)];
    const size_t count = sizeof(commands) / sizeof(commands[0]);
    pff_mixer_t *mixer = NULL;
    uint32_t now = 0;
    uint32_t wait = 0;
    size_t next = 0;
    uint16_t strong;
    uint16_t weak;
    int send;

    if (pff_mixer_init(mem, sizeof(mem), 4, &mixer) != PFF_OK)
        return 1;
    pff_mixer_set_mix(mixer, PFF_MIX_SUM);
    while (next < count || wait != PFF_NEVER) {
        while (next < count && commands[next].ms <= now)
            apply(mixer, &commands[next++]);
        pff_mixer_poll(mixer, now, &strong, &weak, &send);
        if (send)
            printf("%4u ms  strong %5u  weak %5u\n", (unsigned)now, strong, weak);
        pff_mixer_next(mixer, now, &wait);
        if (next < count && (wait == PFF_NEVER || commands[next].ms - now < wait))
            wait = commands[next].ms - now;
        if (wait != PFF_NEVER)
            now += wait;
    }
    return 0;
}
