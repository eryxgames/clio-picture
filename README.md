# CLIo Picture

The inner loops of [CLIo](https://github.com/eryxgames/CLIo)'s pictures, in C - as
[Ballistic Engine](https://github.com/eigensoundlab/ballistic-engine) is for its sound.

CLIo draws everything in pure Python, and stays the reference: every function here gives
exactly what CLIo's Python gives, to the last bit, so a game looks and plays the same with
this library or without it - only faster. CLIo checks the library against its own Python
on a few values before it uses it, and keeps to the Python if anything differs.

C99, no dependencies, Apache-2.0.

## What it does (0.8)

| Function | CLIo's Python | Faster by |
| --- | --- | --- |
| `clio_frame_more`, `clio_frame_surface`: surfaces lit by their materials - a G-buffer the rooms fill and the things mark, then ambient, lamps, GGX highlights, glow, floors' dim reflections, wet floors and the rain's splashes, fog and lightning | `surface.shade`, `surface.splashes` (`engine/surface.py`) and the G-buffer of `Mesh.draw` | a lit picture 5-6 times (E1M8 at 160 x 45: 46 -> 8.5 ms) |
| `clio_frame_more`'s `low`: the mesh view's floors and ceilings at half width | `Mesh.flat` in low detail | - |
| `clio_land_terraced`: open land with terraces (map cells standing higher) | `Land.cast`, `Land.ground` | as `clio_land_draw` |
| `clio_frame_flip`, `clio_tilt`: the picture turned over (on the ceiling), and turned (up a wall) | `View.turned_over`, `tilt` | - |
| `clio_frame_rooms`: rooms as faces in a true perspective, floors and ceilings at heights (CLIo's mesh view, `--render mesh`) | `Mesh.draw` (`engine/mesh.py`), all but the teleporters' rings | at 150 x 34, 6-7 times in half blocks, 10-13 times in braille |
| `clio_mesh_new`, `clio_frame_mesh_draw`: a model's faces kept here, each instance drawn from its posed corners | `View.draw_model` | with many models in sight, a frame about twice as fast as with 0.5 (100 near: 32 -> 15.5 ms) |
| `clio_frame_model_draw`: models projected, shaded (GGX, Fresnel, Reinhard) and drawn | `View.draw_model`, `model.pbr` | a frame with models 2-2.5 times faster than with 0.4 |
| `clio_frame_*`: a frame kept here - walls, pictures, shadows, models' triangles, colour changes, hurt, shaking, the cockpit - and `clio_dots32` from it | `View.render` (`engine/firstperson.py`) | a room frame 2-2.5 times, in dots 6-7 times (all of 0.4) |
| `clio_room_draw`: the rooms' floor and ceiling | `View._backdrop` (`engine/firstperson.py`), all but the teleporters' rings | a room frame 1.5 times, in dots 2.5 times |
| `clio_land_draw`: the open land's ground caster | `Land.cast` (`engine/land.py`) | about 20 times; a land frame 4-5 times |
| `clio_sky_draw`: the sky over open cells | `sky_picture` (`engine/firstperson.py`), all but the sun's disc and glow | about 9 times |
| `clio_land_raw`: a block of the land's heights | `Land.raw`, a chunk at a time in `Land.build` | about 4 times a chunk |
| `clio_dots`: braille cells from pixels | `View.braille` (the `dots` and `dots-fill` views) | 4 to 7 times |
| `clio_hash`, `clio_value`, `clio_fbm` | `engine/land.py`: `_h`, `value`, `fbm` | - |

Frames along the bot's own game of each level, 150 x 34 characters, in CLIo (measured on a
cloud x86-64 machine):

| Level | Pixel: Python | Pixel: 0.5 | Dots: Python | Dots: 0.5 |
| --- | --- | --- | --- | --- |
| E1M1 (rooms) | 11.9 ms | 4.4 ms | 40.9 ms | 6.3 ms |
| E1M2 (rooms, a vehicle) | 9.9 ms | 4.3 ms | 37.7 ms | 5.7 ms |
| E1M3 (rooms, sky, models) | 35.5 ms | 7.9 ms | 95.5 ms | 14.9 ms |
| E1M4 (open land, models) | 79.4 ms | 7.7 ms | 141.1 ms | 14.1 ms |
| E1M9 (belts, oil, fire) | 12.2 ms | 5.0 ms | 44.1 ms | 7.4 ms |
| SIM-1 (a land without end) | 76.6 ms | 7.1 ms | 132.0 ms | 10.4 ms |
| SIM-4 (squads) | 11.5 ms | 5.0 ms | 43.0 ms | 6.3 ms |

Models need `pow`, and open land's haze `exp`. The library has none of its own: CLIo hands it the
ones its Python uses (`clio_maths`, the process's maths library; on Windows the Universal C Runtime's)
and checks `clio_shade` against `model.pbr` on a few hundred values before it lets the library draw
models. So the library links no `pow` (glibc versions it anew now and then) and gives what that
Python gives.

The caster asks CLIo for each chunk of ground it needs, once a picture (`clio_chunk_fn`), so
CLIo builds chunks exactly when its Python caster would. The rooms ask for each cell's floor
and ceiling the same way (`clio_room_cell_fn`), in the order the Python asks, since in a world
without end looking at a cell can load it. The haze at each distance needs
`exp`, and the sky's gradient a power, so CLIo works those out and hands them in. The sun's
disc and glow need a hypotenuse that Python works out its own way, so the sky hands back the
pixels the sun may reach and CLIo finishes them; the rooms do the same with the teleporters'
rings.

From 0.4 the picture stays in the library from the floor to the cells (`clio_frame`, owned by
CLIo): CLIo still decides what is drawn where, nearest last, and asks the library to draw
each wall, picture and triangle into the frame; what the library has no call for (a glow, a
blast) CLIo draws through rows that read and write the frame's pixels.

From 0.8 a frame can keep a G-buffer beside its pixels - what each pixel shows, its material, the
light the player's view gives it - handed over with `clio_frame_more` for the calls that thread makes
next (the frame itself is as 0.4 made it, so an older CLIo is not upset by it). The rooms then draw the
surfaces' own colours, the things drawn after mark it as they land, and `clio_frame_surface` lights
the picture by it - lamps, highlights, reflections, the rain's splashes - to the same pixel as CLIo's
Python. It needs `pow` and `exp` as the models do, and `sqrt`, which C and Python both round exactly.

Next: what is left in CLIo's Python - glows and blasts (they need a hypotenuse), the rain in the air,
the order of things in a picture, and the cells' tuples. See `include/clio_picture.h` for the
interface.

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

`tests/cases.h` holds what CLIo's Python draws for a made-up land, sky and rooms, and the
calls CLIo makes on a frame (recorded) with what its Python draws for the same; after a change in
CLIo's pictures, write it again with `python3 tests/make_cases.py PATH/TO/CLIo > tests/cases.h`.

With Microsoft's compiler, from a Developer Command Prompt: `build.bat test`.

The floats must round as Python rounds them, so the build turns off fused multiply-add
(`-ffp-contract=off`, `/fp:strict`); do not add `-ffast-math`. Point CLIo at your build
with `CLIO_PICTURE_LIB=/path/to/libclio_picture.so`, or copy it into CLIo's
`engine/picture/lib/`. CLIo's `tests/test_picture.py` then compares it with the Python
(`CLIO_PICTURE_REQUIRE=1` makes those tests fail instead of skip when it is missing).

## Rules for a new function

- CLIo's Python is the reference: the same operations in the same order, `+ - * /`,
  `floor`, `fabs`, `sqrt` and comparisons - nothing from libm that may round differently on
  another machine (`exp`, `sin`, `hypot`), but the `pow` and `exp` CLIo hands over, and no `fmod` (glibc 2.38 gave it a new
  symbol version, so a library built on a new Linux would not load on an older one; the
  workflow checks that the Linux builds need nothing newer than glibc 2.17). Where the Python needs one
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
