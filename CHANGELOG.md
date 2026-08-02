# Changelog

Each release lists what changed, what updating means for existing code, and every security fix.

## Unreleased

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
