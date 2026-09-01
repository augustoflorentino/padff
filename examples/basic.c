#include <padff/padff.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
    static unsigned char mem[PFF_MIXER_SIZE(2)];
    pff_mixer_t *mixer = NULL;
    pff_config_t config;
    pff_effect_t hit;
    pff_effect_t engine;
    uint32_t now = 0;
    uint32_t wait = 0;
    uint16_t motors[2] = {0, 0};
    int send;

    memset(&hit, 0, sizeof(hit));
    hit.size = sizeof(hit);
    hit.type = PFF_RUMBLE;
    hit.strong = 60000;
    hit.length_ms = 150;
    memset(&engine, 0, sizeof(engine));
    engine.size = sizeof(engine);
    engine.type = PFF_RUMBLE;
    engine.weak = 20000;
    engine.length_ms = 400;
    engine.attack_ms = 100;

    memset(&config, 0, sizeof(config));
    config.size = sizeof(config);
    config.motor_max = 255;
    if (pff_mixer_init(mem, sizeof(mem), 2, &config, &mixer) != PFF_OK)
        return 1;
    pff_mixer_upload(mixer, 0, &hit, now);
    pff_mixer_upload(mixer, 1, &engine, now);
    pff_mixer_play(mixer, 0, 2, now);
    pff_mixer_play(mixer, 1, 1, now);

    while (wait != PFF_NEVER) {
        pff_mixer_poll(mixer, now, motors, 2, &send);
        if (send)
            printf("%4u ms  strong %3u  weak %3u\n", (unsigned)now, motors[0], motors[1]);
        pff_mixer_next(mixer, now, &wait);
        now += wait < 10 ? 10 : wait;
    }
    return 0;
}
