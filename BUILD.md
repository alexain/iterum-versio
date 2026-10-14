# Building Iterum Versio

A prebuilt binary is already in [`firmware/IterumVersio.bin`](firmware/IterumVersio.bin): you only need this page to change the code or rebuild it yourself.

## Requirements

You need the `arm-none-eabi-gcc` toolchain (the [Daisy Toolchain](https://github.com/electro-smith/DaisyToolchain) installs it on macOS and Windows; on Linux use your distribution's package or the xPack build).

## Compile

```bash
git clone https://github.com/alexain/iterum-versio
cd iterum-versio

# libraries (or add them as submodules, see below)
git clone --recurse-submodules https://github.com/electro-smith/libDaisy lib/libDaisy
git clone https://github.com/electro-smith/DaisySP lib/DaisySP

make -C lib/libDaisy
make -C lib/DaisySP
make                 # -> build/IterumVersio.bin
make program-dfu     # optional: flash over USB
```

## Using git submodules

To track the libraries as submodules instead:

```bash
git submodule add https://github.com/electro-smith/libDaisy lib/libDaisy
git submodule add https://github.com/electro-smith/DaisySP lib/DaisySP
git submodule update --init --recursive
```

## Libraries in another folder

If you keep the libraries elsewhere: `make LIBDAISY_DIR=/path/to/libDaisy DAISYSP_DIR=/path/to/DaisySP`.

## Knob and switch mapping

The mapping assumes panel knob *n* is `KNOB_(n-1)` in libDaisy, and that positions A and X are read as `POS_UP`. If anything is swapped on your unit, edit the `K_...` and `SW_...` constants at the top of `src/IterumVersio.cpp` and rebuild.

## Flashing

1. Connect the Versio via the USB port of the Daisy Seed on the back of the module.
2. Put the Seed in DFU mode: hold **BOOT**, press and release **RESET**, then release **BOOT**.
3. Run `make program-dfu`, or load `build/IterumVersio.bin` with the [Electro-Smith Web Programmer](https://flash.daisy.audio/).

Back to the [README](README.md).
