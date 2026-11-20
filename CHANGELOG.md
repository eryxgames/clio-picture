# Changelog

Versions follow [semantic versioning](https://semver.org). Each follows CLIo's Python, which
is the reference.

## 0.4.0

A frame kept in the library from the floor to the cells.

- `clio_frame`: CLIo's picture (its pixels, in open land each pixel's distance, the cells
  covered, a model's own depth), owned by CLIo. Every call keeps CLIo's rule for open land:
  a pixel is written only where the land there is further off than what is drawn now.
- Into it: `clio_frame_front` and `clio_frame_side` (the walls' faces, with bullet-hole
  cover marks), `clio_frame_blit` (pictures, darker with distance or into the haze, a
  monster's flash, tint and dissolving), `clio_frame_darken` (shadows), `clio_frame_model`
  and `clio_frame_triangle` (models' triangles, flat or smooth), `clio_frame_recolour`
  (a blast's flash, sudo, the TTY goggles, whitening, fading), `clio_frame_vignette` (hurt),
  `clio_frame_shake`, `clio_frame_canopy`, `clio_frame_dashboard`, `clio_frame_cage`;
  `clio_frame_land_cover` after the land; `clio_dots32` reads the frame for the braille.
- Frames along the bot's games: rooms 2-2.5 times faster in the pixel view and 6-7 times in
  dots than CLIo's Python; open land 5-11 times.
- `tests/cases.h`: the calls CLIo makes on a room's and an open land's frame, recorded by
  `make_cases.py`, replayed here and compared with what CLIo's Python draws for the same.

## 0.3.0

The rooms' floor and ceiling.

- `clio_room_draw`: CLIo's `View._backdrop` - for each pixel row the distance it sees and
  its fog (worked out by CLIo), for each pixel the cell under or over it: its tile, what is
  set in the floor (pressure plates up or down, sunk crates, lifts, belts each way, oil,
  slick coolant, scorch), toxic water, fire, the marks of fire to come, the plain floor and
  ceiling with their lights, and the sky where a ceiling is open. Cells are asked for
  through a callback, once a picture, in the order CLIo asks; the teleporters' rings are
  handed back for CLIo to finish. A room frame in CLIo 1.5 times faster, in dots 2.5 times.
- `tests/cases.h`: two made-up rooms with every kind of floor, seen ahead and sideways at
  low detail.

## 0.2.0

The open land's picture: the ground and the sky.

- `clio_land_draw`: the ground caster of CLIo's open land (`Land.cast`): rays near to far, a
  column each (or every other at low detail), the haze by distance, water with the sky in it
  and its glints, the near ground's grain, the cells a map stands on with their floor
  textures, and each pixel's distance. Chunks are asked for through a callback, once a
  picture. About 20 times faster than the Python; a frame of open land 4-5 times.
- `clio_sky_draw`: the sky over open cells (`sky_picture`): the gradient (rows' colours from
  CLIo), stars, drifting clouds and the ridges; the pixels the sun may reach are handed back
  for CLIo to finish. About 9 times faster.
- `tests/make_cases.py` writes `tests/cases.h` from CLIo's Python: a made-up land seen two
  ways (with stamps, water and low detail) and two skies (one with a banded planet).
- The Linux builds need nothing newer than glibc 2.17 (checked in the workflow), so they load
  on older distributions too. Builds run on GitHub's current runners (Ubuntu 24.04, macOS 15).

## 0.1.0

The first release: the open land's noise and heights, and the braille of the dots views.

- `clio_hash`, `clio_value`, `clio_fbm`: CLIo's value noise and fractal noise
  (`engine/land.py`).
- `clio_land_raw`: the raw heights of a block of the land at once (`Land.raw`); CLIo builds
  a chunk of ground with it about four times faster.
- `clio_dots`: braille cells from pixels, as the `dots` (dithered dots on black) and
  `dots-fill` (two colours a cell) views make them; four to seven times faster in CLIo.
- `clio_picture_version`.
- Builds for Linux (x64, arm64), macOS (universal), Windows (x64, arm64) and Android
  (arm64-v8a, armeabi-v7a, x86_64), each checked against CLIo's Python where it can run.
