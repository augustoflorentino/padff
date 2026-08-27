# Changelog

Each release lists what changed, what updating means for existing code, and every security fix.

## Unreleased

## 2.4.0 - 2026-08-27

- added: trigger motors (Xbox impulse triggers): `left_trigger` and `right_trigger` at the end
  of `pff_effect_t`, now 28 bytes, for rumble effects; the envelope applies to a trigger only
  when it is not 0
- added: `pff_mixer_poll_motors` reports strong, weak, left and right trigger motors
  (`PFF_MOTOR_*`, `PFF_MOTORS`); `pff_mixer_next` and the rate limit cover all four
- `pff_mixer_upload` accepts `size` 24 (effects written for padff 1.x and 2.0, and vpad 1.x)
  as well as 28, and reads only `size` bytes; the trigger fields of a 24-byte effect are 0
- the fuzz target uploads 24- and 28-byte effects and checks all four motors

Updating: no code changes needed for code that sets `size = sizeof(pff_effect_t)`. Code that
copies a 24-byte effect from another struct must copy `sizeof` of the source, not of
`pff_effect_t`, and keep `size` at 24. `pff_mixer_poll` now also sends when only a trigger
motor changed.

## 2.0.0 - 2026-08-24

- breaking: `pff_mixer_init` takes a `const pff_config_t *` (mix, motor scale, minimum
  interval); `pff_mixer_configure` changes it later. `pff_mixer_set_mix`,
  `pff_mixer_set_scale` and `pff_mixer_set_interval` are gone
- why: the three setters left a window after init where the mixer ran with defaults that did
  not match the device, each failed on its own (a bad scale after a good mix left a half
  applied setup), and every new setting needed a new exported function. The config is checked
  as a whole before anything is applied, and its `size` field lets later versions add settings
  without breaking callers
- `pff_mixer_set_gain` stays: gain is a runtime command from the game (`FF_GAIN`), not a
  device setting
- a motor scale of 0 in the config means 65535, so a zeroed config with `size` set gives the
  defaults
- SONAME is now `libpadff.so.2`

Upgrading from 1.x:

```c
/* 1.x */
pff_mixer_init(mem, sizeof(mem), slots, &mixer);
pff_mixer_set_mix(mixer, PFF_MIX_SUM);
pff_mixer_set_scale(mixer, 255);
pff_mixer_set_interval(mixer, 8);

/* 2.0 */
pff_config_t config = {sizeof(config), PFF_MIX_SUM, 255, 8};
pff_mixer_init(mem, sizeof(mem), slots, &config, &mixer);
```

- pass `NULL` as the config to keep the defaults
- to change a setting at runtime, fill the whole config and call `pff_mixer_configure`
- `pff_effect_t` is unchanged; vpad 1.x effects still copy in with `memcpy`
- relink: the ABI changed

## 1.5.0 - 2026-08-24

- added: `pff_mixer_stop` stops one slot, keeping its effect uploaded
- added: `pff_mixer_playing` tells whether a slot is still playing at a given time
- added: `examples/ff_replay.c`, a Linux-style command sequence (upload, play, stop, gain, erase)
- README: the memory block must be accessed only through padff while the mixer lives
- tests: the reference model and the random traces cover single-slot stops; the fuzz target
  checks that nothing plays when no slot is playing

Updating: no code changes needed.

## 1.0.0 - 2026-08-17

- breaking: `pff_effect_t` starts with `uint16_t size`, which callers set to
  `sizeof(pff_effect_t)`; `pff_mixer_upload` rejects any other value with `PFF_ERR_ARG`. Later
  versions add fields only at the end and keep accepting older sizes
- the struct is 24 bytes with a fixed layout: size, type, strong, weak, level, level_end,
  delay_ms, length_ms, attack_ms, attack_level, fade_ms, fade_level; vpad 1.x `vpad_effect_t`
  uses the same layout, so a vpad effect can be copied into a `pff_effect_t` with `memcpy`
- SONAME is now `libpadff.so.1`

Upgrading from 0.x:

- set `effect.size = sizeof(effect)` before every `pff_mixer_upload`; zero-initialized effects
  without it now fail with `PFF_ERR_ARG`
- code copying `struct ff_effect` fields by name needs only the `size` line
- code that `memcpy`s from vpad needs vpad 1.0 or later (vpad 0.x effects have no `size`)
- relink: the ABI changed

## 0.6.0 - 2026-08-17

- `pff_mixer_next` during an envelope or ramp returns the time until the scaled output next
  changes (looking up to 64 ms ahead) instead of 1, so slow slopes on motors with few steps
  wake the loop far less often; no change is missed

Updating: no code changes needed.

## 0.2.0 - 2026-08-05

- fixed: an endless effect past its attack went silent if `pff_mixer_poll` was not called for
  2^31 ms, which `PFF_NEVER` from `pff_mixer_next` invites
- fixed: `pff_mixer_next` overflowed a signed negation when called 2^31 ms after a play
- fixed: after a send with a minimum interval and nothing playing, `pff_mixer_next` now wakes
  the caller when the interval ends instead of returning `PFF_NEVER`
- `pff_mixer_set_interval` rejects intervals above `INT32_MAX` with `PFF_ERR_ARG`
- README lists where the mixer differs from Linux `ff-memless`

Updating: no code changes needed. Callers passing an interval above `INT32_MAX` now get
`PFF_ERR_ARG`.

## 0.1.0 - 2026-08-03

First release.

- `pff_mixer_init`: mixer with any number of slots inside a caller memory block
  (`PFF_MIXER_SIZE`), no allocation
- `pff_mixer_upload`, `pff_mixer_erase`, `pff_mixer_play`, `pff_mixer_stop_all`: effect commands
  as games send them; rumble, periodic, constant and ramp with delay, length, repetitions and
  attack/fade envelope
- `pff_mixer_set_gain`, `pff_mixer_set_mix`, `pff_mixer_set_scale`, `pff_mixer_set_interval`:
  global gain, max or clipped sum mixing, motor scale, minimum interval between sends
- `pff_mixer_poll`: motor intensities at a given time, flagged only when they change
- `pff_mixer_next`: how long the caller can sleep before the output can change
