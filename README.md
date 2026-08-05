# padff

Force feedback mixer: turns the vibration effects a game asks for into intensities for the two
motors of a gamepad.

- effects: rumble (strong and weak motor), periodic, constant and ramp, the last three reduced to
  an intensity on both motors
- per effect: start delay, length (or endless), repetitions, attack and fade envelope
- global gain, any number of slots, simultaneous effects mixed by maximum or by clipped sum
- output quantized to the motor scale and reported only when it changes, with an optional
  minimum interval between sends
- tells how long the caller can sleep until the output can change next
- time is an argument: integer milliseconds only, no clock, no allocation, no dependencies
  beyond the C99 standard headers, so it fits microcontrollers

## Build and install

```sh
cmake -S . -B build -DBUILD_SHARED_LIBS=ON
cmake --build build
cmake --install build --prefix /usr/local
```

Use it from CMake:

```cmake
find_package(padff REQUIRED)
target_link_libraries(app PRIVATE padff::padff)
```

Or with pkg-config:

```sh
cc app.c $(pkg-config --cflags --libs padff)
```

## Single-file form

`scripts/amalgamate.sh` writes `dist/padff/padff.h`. Copy it into your project and, in exactly
one `.c` file:

```c
#define PFF_IMPLEMENTATION
#include "padff.h"
```

Define `PFF_STATIC` as well to keep every function private to that file.

## Usage

See [`examples/basic.c`](examples/basic.c). This README is the API reference; the header
`include/padff/padff.h` lists the functions and types.

```c
static unsigned char mem[PFF_MIXER_SIZE(16)];
pff_mixer_t *mixer;

pff_mixer_init(mem, sizeof(mem), 16, &mixer);
pff_mixer_set_scale(mixer, 255);

pff_mixer_upload(mixer, id, &effect, now_ms);
pff_mixer_play(mixer, id, count, now_ms);

pff_mixer_poll(mixer, now_ms, &strong, &weak, &send);
if (send)
    send_to_gamepad(strong, weak);
pff_mixer_next(mixer, now_ms, &wait_ms);
```

Upload and play on each command from the game; poll and send in the event loop, then sleep up
to `wait_ms` (`PFF_NEVER`: until the next command). While an envelope or ramp is changing,
`wait_ms` is 1.

Conventions:

- every function that can fail returns `PFF_OK` or an error code; `pff_strerror` describes it:
  `PFF_ERR_ARG` (null pointer, bad option), `PFF_ERR_SPACE` (memory block too small),
  `PFF_ERR_SLOT` (slot out of range, or play on an empty slot), `PFF_ERR_EFFECT` (unknown type)
- the mixer lives inside the memory block given to `pff_mixer_init`; `PFF_MIXER_SIZE(slots)`
  is always enough, at any alignment. There is nothing to destroy: drop the block
- `now_ms` is any monotonic millisecond counter; it may wrap around `UINT32_MAX`. Call
  `pff_mixer_poll` at least once every 24 days
- defaults: mix `PFF_MIX_MAX`, gain 65535, motor scale 65535, no minimum interval; the
  interval goes up to `INT32_MAX` ms

Effect fields (`pff_effect_t`):

| field | meaning |
| --- | --- |
| `type` | `PFF_RUMBLE`, `PFF_PERIODIC`, `PFF_CONSTANT` or `PFF_RAMP` |
| `strong`, `weak` | rumble: motor intensities, 0 to 65535 |
| `level` | periodic magnitude, constant level, ramp start: -32768 to 32767, intensity is `2 * abs(level)` |
| `level_end` | ramp end |
| `delay_ms` | wait before each repetition |
| `length_ms` | length of each repetition; 0 plays until stopped |
| `attack_ms`, `attack_level` | start from `attack_level` (intensity 0 to 65535) and reach the effect in `attack_ms` |
| `fade_ms`, `fade_level` | go to `fade_level` in the last `fade_ms`; only for effects with a length |

Behavior, modeled on Linux force feedback (`ff-memless`):

- `pff_mixer_play(count)` plays `count` repetitions of delay plus length; `count` 0 stops
- uploading to a slot that is playing replaces the effect and restarts the current repetition
  (its delay included), keeping the repetitions left; uploading to a finished slot does not
  restart it
- gain scales every effect before mixing; `PFF_MIX_SUM` clips at 65535
- the result is scaled to `0..motor_max` with rounding to nearest

Where it differs from `ff-memless`:

- the default mix is the maximum of the effects; `ff-memless` sums and clips, which is
  `PFF_MIX_SUM`
- the attack/fade envelope also applies to rumble effects; `ff-memless` ignores it there
- `PFF_RAMP` has no `ff-memless` counterpart

From Linux `struct ff_effect`: copy `replay.delay` and `replay.length`; rumble uses
`strong_magnitude` and `weak_magnitude`; periodic `magnitude`; constant `level`; ramp
`start_level` and `end_level`; envelope levels are doubled (Linux uses 0 to 32767). `FF_GAIN`
goes to `pff_mixer_set_gain`.

## Validation

- `tests/traces/` holds command sequences recorded from SDL3 on a Linux uinput gamepad
  (`tests/record/`: `SDL_RumbleGamepad` as games call it, and the SDL haptic API with
  envelopes, repetitions and gain); each runs against its expected output
- `tests/oracle.py` is an independent reference model: it computes the expected output of the
  recordings and of random command sequences, which `tests/trace_run.c` replays polling every
  millisecond and sleeping by `pff_mixer_next`, at time 0 and right before the 32-bit wrap
- `fuzz/fuzz_commands.c` fuzzes command sequences and checks that nothing is sent before the
  time `pff_mixer_next` returned

## Versioning

Semantic Versioning. Compatible releases bump the minor version; incompatible ones bump the
major version and the soname. See [CHANGELOG.md](CHANGELOG.md).

## License

MIT, see [LICENSE](LICENSE).
