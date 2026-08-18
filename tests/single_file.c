#define PFF_IMPLEMENTATION
#include "padff/padff.h"

#include <string.h>

int main(void)
{
    static unsigned char mem[PFF_MIXER_SIZE(1)];
    pff_mixer_t *mixer = NULL;
    pff_effect_t effect;
    uint16_t strong = 0;
    uint16_t weak = 0;
    int send = 0;
    int status;

    memset(&effect, 0, sizeof(effect));
    effect.size = sizeof(effect);
    effect.type = PFF_RUMBLE;
    effect.strong = 65535;
    status = pff_mixer_init(mem, sizeof(mem), 1, &mixer);
    if (status == PFF_OK)
        status = pff_mixer_upload(mixer, 0, &effect, 0);
    if (status == PFF_OK)
        status = pff_mixer_play(mixer, 0, 1, 0);
    if (status == PFF_OK)
        status = pff_mixer_poll(mixer, 0, &strong, &weak, &send);
    if (pff_strerror(status) == NULL)
        status = PFF_ERR_ARG;
    return status == PFF_OK && send && strong == 65535 && weak == 0 ? 0 : 1;
}
