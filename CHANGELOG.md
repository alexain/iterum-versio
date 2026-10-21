# Changelog

## 1.1

Thanks to the testers for the feedback.

- **Fixed**: the delay time could keep moving slightly even with the knobs untouched, because the small electrical noise of the pots (and CV) was read as continuous time changes. In Fade mode this meant back-to-back crossfades, a glitchy delay that never settled. TIME and SKEW now have smoothing and a deadband.
- **Fixed**: small jitter on the external clock could retrigger a crossfade on every beat. The clock period is now averaged; real tempo changes are still followed immediately.
- **Changed**: the output limiter now turns the level down instead of distorting, and the mix has more headroom around noon (less clipping with hot signals).
- **Changed**: REVERSE has "always forward" and "always reversed" zones at the ends of its travel (first and last 10 %).
- **New**: when clocked, RANGE shifts the TIME ratios by octaves (÷4, ÷2, ×1, ×2) instead of doing nothing.
- **New**: LED brightness (bright, dim, off), selected by holding FSU at power-up and saved in flash.
- **New**: below 100 ms the repeat LEDs stay steadily lit instead of strobing.

## 1.0

First release.
