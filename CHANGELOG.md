# Changelog

Each release lists what changed, what updating means for existing code, and every security fix.

## Unreleased

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
