# Changelog

Versions follow [semantic versioning](https://semver.org). Each follows CLIo's Python, which
is the reference.

## 0.8.0

Surfaces lit by their materials, low detail, terraces, the picture turned over and turned.

- `clio_frame_more`: what 0.8 adds to a frame, handed over beside it for the calls this thread
  makes next - a G-buffer (each pixel's kind: floor, ceiling, a wall turned one of four ways, a
  thing, the sky; its material, glowing or not; the light the player's view gives it) with each
  face's material and each material's glowing colours, and low detail. The `clio_frame`
  structure is unchanged.
- `clio_frame_rooms` with a G-buffer draws the surfaces' own colours and fills it; pictures,
  triangles and shadows drawn after mark it as CLIo's `surface.GRow` does (a thing keeps its
  distance and its kind; a shadow stays floor).
- `clio_frame_surface`: CLIo's `surface.shade` - ambient times the view's own light, lamps by how
  far across and how the surface is turned to them (only where they see), GGX highlights (Smith,
  Schlick; a metal's in its own colour, the player's own light's sheen), glowing texels as they
  are, floors' dim reflections (mirrored about where they meet what stands on them; Fresnel;
  smeared on rough floors; trembling in the rain), the weather's fog and lightning - then
  `surface.splashes`. `clio_ggx`.
- Low detail: the mesh view's floors and ceilings at half width, as `Mesh.flat`.
- `clio_land_terraced`: `clio_land_draw` with terraces - map cells standing higher than the pad,
  their floors drawn that high and the ground blended from them (`Land.ground`).
- `clio_frame_flip` (the picture turned half round, its cover too) and `clio_tilt` (turned about
  its middle, Python's rounding).
- E1M8 at 160 x 45 lit: 46 ms in Python, 8.5 ms here (E1M6 42 -> 6.8).
- `tests/cases.h`: two lit pictures (lamps, a model, chrome, glowing windows, a polished floor; a
  yard in the rain at night, wet, splashing), GGX, the turn and the flip.

## 0.7.0

CLIo's mesh view: rooms as faces in a true perspective, with floors and ceilings at heights.

- `clio_frame_rooms`: the faces CLIo works out (`engine/mesh.py`: each open cell's floor and
  ceiling at its own heights, the walls' faces turned to it, the risers and lintels between
  cells), clipped near the camera, projected and filled row by row - each pixel's ray meets
  its face's plane, which gives the texel, the distance (into the frame's depth: monsters,
  items and models are then hidden by what stands in front of them) and the light (open
  land's fog, faces side on darker, a fade before the far end, 256 steps). Floors as
  `clio_room_draw` has them, plates, belts, oil, toxic water, fire and its marks included;
  teleporters' rings left to the caller. The walls' cover for CLIo's labels.
- At 150 x 34 characters: E1M1 21.7 -> 3.2 ms in half blocks, 52.2 -> 5.0 ms in braille; E1M3
  (sky, models) 38.6 -> 5.4 and 105.2 -> 8.2 ms.
- `room.c`'s tile and fixture lookups shared with it (`src/internal.h`, not exported).
- `tests/cases.h`: a room with every kind of face and floor, from three places.

## 0.6.0

Models kept here, drawn per instance: crowds.

- `clio_mesh_new`, `clio_mesh_free`: a model's faces, materials and own colours handed over
  once and kept.
- `clio_frame_mesh_draw`: one instance of a kept mesh - only its posed corners and where it
  stands and faces are handed over; the library turns them into the world with the same sums
  in the same order as `View.draw_model`, then draws as `clio_frame_model_draw`.
- E1M4 at 150 x 34 with model monsters near: 25 of them 14.6 -> 8.9 ms a frame, 100 of them
  32 -> 15.5 ms (0.5 spent most of that time packing triangles in Python).
- `tests/cases.h`: the models of the 0.5 cases drawn again from kept meshes.

## 0.5.0

Models drawn here.

- `clio_frame_model_draw`: CLIo's `View.draw_model` - each triangle projected (the rooms'
  0.64 ^ z or open land's perspective; near models at the middle's depth), turned-away ones
  dropped, shaded by `model.pbr` (GGX, Smith-Schlick, Fresnel-Schlick, Reinhard, sRGB) at its
  middle or blended from its corners, darker with distance or into the haze, flashed or
  tinted, and drawn with the model's own depth. Shards of a broken model are lit from the
  side that faces the eye.
- `clio_maths`: the `pow` and `exp` to use, handed over by the caller - CLIo gives its
  Python's own. `clio_shade`, `clio_room_scale`, `clio_haze`: to check them against it.
- Frames with models 2-2.5 times faster than with 0.4: E1M3 19 -> 8 ms, E1M4 17 -> 8 ms.
- `tests/cases.h`: models in a room and in open land, recorded from CLIo; compared where the
  test's `pow` and `exp` give what the cases were made with.

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
