#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

static void pump(Uint32 ms)
{
    Uint64 end = SDL_GetTicks() + ms;

    while (SDL_GetTicks() < end) {
        SDL_PumpEvents();
        SDL_UpdateGamepads();
        SDL_Delay(1);
    }
}

static SDL_Gamepad *open_pad(void)
{
    int count = 0;
    SDL_JoystickID *ids;
    SDL_Gamepad *pad = NULL;
    int i;

    for (i = 0; i < 50 && pad == NULL; i++) {
        ids = SDL_GetGamepads(&count);
        if (ids != NULL && count > 0)
            pad = SDL_OpenGamepad(ids[count - 1]);
        SDL_free(ids);
        if (pad == NULL)
            pump(100);
    }
    return pad;
}

static void scenario_shots(SDL_Gamepad *pad)
{
    int i;

    for (i = 0; i < 5; i++) {
        SDL_RumbleGamepad(pad, 0xffff, 0x8000, 100);
        pump(150);
    }
    pump(200);
}

static void scenario_engine(SDL_Gamepad *pad)
{
    int frame;

    for (frame = 0; frame < 60; frame++) {
        Uint16 rpm = (Uint16)(frame * 1000);

        SDL_RumbleGamepad(pad, rpm, (Uint16)(0xffff - rpm), 1000);
        pump(16);
    }
    SDL_RumbleGamepad(pad, 0, 0, 0);
    pump(100);
}

static void scenario_expire(SDL_Gamepad *pad)
{
    SDL_RumbleGamepad(pad, 0x4000, 0xc000, 300);
    pump(500);
}

static int run_effect(SDL_Haptic *haptic, SDL_HapticEffect *effect, int iterations, Uint32 wait)
{
    int id = SDL_CreateHapticEffect(haptic, effect);

    if (id < 0)
        return id;
    SDL_RunHapticEffect(haptic, id, iterations);
    pump(wait);
    return id;
}

static void scenario_haptic(SDL_Gamepad *pad)
{
    SDL_Haptic *haptic = SDL_OpenHapticFromJoystick(SDL_GetGamepadJoystick(pad));
    SDL_HapticID *list;
    int count = 0;

    if (haptic == NULL) {
        list = SDL_GetHaptics(&count);
        if (list != NULL && count > 0)
            haptic = SDL_OpenHaptic(list[count - 1]);
        SDL_free(list);
    }
    SDL_HapticEffect e;
    int ids[5];

    if (haptic == NULL) {
        fprintf(stderr, "haptic: %s\n", SDL_GetError());
        return;
    }
    memset(&e, 0, sizeof(e));
    e.type = SDL_HAPTIC_CONSTANT;
    e.constant.length = 400;
    e.constant.level = 0x5000;
    e.constant.attack_length = 100;
    e.constant.attack_level = 0x1000;
    e.constant.fade_length = 150;
    e.constant.fade_level = 0;
    ids[0] = run_effect(haptic, &e, 1, 500);

    memset(&e, 0, sizeof(e));
    e.type = SDL_HAPTIC_SINE;
    e.periodic.period = 50;
    e.periodic.magnitude = 0x6000;
    e.periodic.length = 120;
    e.periodic.delay = 40;
    ids[1] = run_effect(haptic, &e, 3, 600);

    memset(&e, 0, sizeof(e));
    e.type = SDL_HAPTIC_RAMP;
    e.ramp.length = 300;
    e.ramp.start = -0x4000;
    e.ramp.end = 0x7fff;
    ids[2] = run_effect(haptic, &e, 1, 50);

    memset(&e, 0, sizeof(e));
    e.type = SDL_HAPTIC_LEFTRIGHT;
    e.leftright.length = 200;
    e.leftright.large_magnitude = 0x3000;
    e.leftright.small_magnitude = 0xe000;
    ids[3] = run_effect(haptic, &e, 2, 500);

    SDL_SetHapticGain(haptic, 50);
    memset(&e, 0, sizeof(e));
    e.type = SDL_HAPTIC_CONSTANT;
    e.constant.length = SDL_HAPTIC_INFINITY;
    e.constant.level = 0x7fff;
    ids[4] = run_effect(haptic, &e, 1, 200);
    SDL_StopHapticEffect(haptic, ids[4]);
    pump(50);
    SDL_SetHapticGain(haptic, 100);
    SDL_RunHapticEffect(haptic, ids[0], 1);
    pump(100);
    SDL_DestroyHapticEffect(haptic, ids[0]);
    pump(100);
    SDL_DestroyHapticEffect(haptic, ids[1]);
    SDL_DestroyHapticEffect(haptic, ids[2]);
    SDL_DestroyHapticEffect(haptic, ids[3]);
    SDL_DestroyHapticEffect(haptic, ids[4]);
    SDL_CloseHaptic(haptic);
}

int main(int argc, char **argv)
{
    SDL_Gamepad *pad;
    const char *which = argc > 1 ? argv[1] : "";

    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC)) {
        fprintf(stderr, "init: %s\n", SDL_GetError());
        return 1;
    }
    pad = open_pad();
    if (pad == NULL) {
        fprintf(stderr, "no gamepad: %s\n", SDL_GetError());
        return 1;
    }
    pump(200);
    if (strcmp(which, "shots") == 0)
        scenario_shots(pad);
    else if (strcmp(which, "engine") == 0)
        scenario_engine(pad);
    else if (strcmp(which, "expire") == 0)
        scenario_expire(pad);
    else if (strcmp(which, "haptic") == 0)
        scenario_haptic(pad);
    SDL_CloseGamepad(pad);
    SDL_Quit();
    return 0;
}
