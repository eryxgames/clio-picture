# CLIo Picture

The inner loops of [CLIo](https://github.com/eryxgames/CLIo)'s pictures, in C - as
[Ballistic Engine](https://github.com/eigensoundlab/ballistic-engine) is for its sound.

CLIo draws everything in pure Python, and stays the reference: every function here gives
exactly what CLIo's Python gives, to the last bit, so a game looks and plays the same with
this library or without it - only faster. CLIo checks the library against its own Python
on a few values before it uses it, and keeps to the Python if anything differs.

C99, no dependencies, Apache-2.0.

## What it does (0.2)

| Function | CLIo's Python | Faster by |
| --- | --- | --- |
| `clio_land_draw`: the open land's ground caster | `Land.cast` (`engine/land.py`) | about 20 times; a land frame 4-5 times |
| `clio_sky_draw`: the sky over open cells | `sky_picture` (`engine/firstperson.py`), all but the sun's disc and glow | about 9 times |
| `clio_land_raw`: a block of the land's heights | `Land.raw`, a chunk at a time in `Land.build` | about 4 times a chunk |
| `clio_dots`: braille cells from pixels | `View.braille` (the `dots` and `dots-fill` views) | 4 to 7 times |
| `clio_hash`, `clio_value`, `clio_fbm` | `engine/land.py`: `_h`, `value`, `fbm` | - |

A frame of open land in CLIo (E1M4, 150 columns) goes from 52 ms to 12 ms, in the dots view
from 137 ms to 25 ms.

The caster asks CLIo for each chunk of ground it needs, once a picture (`clio_chunk_fn`), so
CLIo builds chunks exactly when its Python caster would. The haze at each distance needs
`exp`, and the sky's gradient a power, so CLIo works those out and hands them in. The sun's
disc and glow need a hypotenuse that Python works out its own way, so the sky hands back the
pixels the sun may reach and CLIo finishes them.

Next, in the order of the payoff: the rooms (walls, floor, ceiling, fog), triangles for 3D
models, sprites. See `include/clio_picture.h` for the interface.

## Getting it

In CLIo: `python clio.py --get-picture` downloads the release CLIo follows for your
machine, checks its SHA-256, and puts the library in CLIo's data folder.

Releases have a zip for each machine CLIo runs on: `linux-x64`, `linux-arm64`,
`macos-universal`, `windows-x64`, `windows-arm64`, `android-arm64-v8a`,
`android-armeabi-v7a`, `android-x86_64` (Termux). Each holds `lib/` (the library),
`include/clio_picture.h`, and the LICENSE and NOTICE; `checksums.txt` lists their SHA-256.

## Building it

```
make            # build/libclio_picture.so (.dylib on macOS, clio_picture.dll with MinGW)
make test       # and check it against CLIo's values (tests/cases.h)
```

`tests/cases.h` holds what CLIo's Python draws for a made-up land and sky; after a change in
CLIo's pictures, write it again with `python3 tests/make_cases.py PATH/TO/CLIo > tests/cases.h`.

With Microsoft's compiler, from a Developer Command Prompt: `build.bat test`.

The floats must round as Python rounds them, so the build turns off fused multiply-add
(`-ffp-contract=off`, `/fp:strict`); do not add `-ffast-math`. Point CLIo at your build
with `CLIO_PICTURE_LIB=/path/to/libclio_picture.so`, or copy it into CLIo's
`engine/picture/lib/`. CLIo's `tests/test_picture.py` then compares it with the Python
(`CLIO_PICTURE_REQUIRE=1` makes those tests fail instead of skip when it is missing).

## Rules for a new function

- CLIo's Python is the reference: the same operations in the same order, `+ - * /`,
  `floor`, `fmod`, `fabs` and comparisons only - nothing from libm that may round
  differently on another machine (`pow`, `exp`, `sin`, `hypot`). Where the Python needs one
  of those, CLIo works the values out and passes them in (the dots' `steps`, the haze at each
  distance, the sky's gradient), or finishes those pixels itself (the sun).
- Plain arrays in, plain arrays out: no Python objects, no callbacks per pixel.
- A test in `tests/test_picture.c` with values from CLIo's Python, and one in CLIo's
  `tests/test_picture.py` that compares the two on many inputs.

## Releasing

Raise `CLIO_PICTURE_VERSION` in `include/clio_picture.h`, add a `## VERSION` section to
CHANGELOG.md, and push a tag `vVERSION`. The workflow builds every machine, checks the ones
it can run against CLIo, and makes the release. Then copy `checksums.txt` into CLIo's
`engine/picture/fetch.py` (`RELEASE` and `SHA256`).
