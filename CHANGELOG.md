# Changelog

Versions follow [semantic versioning](https://semver.org). Each follows CLIo's Python, which
is the reference.

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
