# Changelog

Each release lists what changed, what updating means for existing code, and every security fix.

## Unreleased

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
