# SoundMonReplay

A library for playing BP SoundMon music: the format Brian Postma wrote for the Amiga, used from 1989
in games such as *Alien Breed*, *Plexu* and *Fantasy World Dizzy*, and on music disks and in demos.

- Plain C API (`include/soundmonreplay.h`), static or shared library, no dependencies.
- Builds with CMake; tested with MSVC (x64 and 32-bit).
- Also builds for the web as WebAssembly (wasm). For an example web-based player, please visit
  [modplayer](https://mods.chapzero.com).
- All three versions: BP SoundMon 1 (`BPSM`), 2 (`V.2`) and 3 (`V.3`) files, with V.2 and V.3 synth
  instruments and the effects that rewrite their waveforms.
- Subsongs, with lengths and loop points, seeking, the song's title and its instrument names. Tells
  songs that loop from ones that stop.
- Output options: sample rate, stereo separation, interpolation (none, linear, cubic),
  Amiga output filters (A500, A1200 or none, plus the LED filter songs can switch), anti-click
  volume ramping, loop count, fade-out, gain.
- A clean-room replayer: written from a description of the format, and checked tick by tick
  against the original replayers running in an Amiga emulator, without reading any replayer's
  code. It matches them exactly on all but two of modland's files; see Status.

The format is described in [docs/SoundMon-format.md](docs/SoundMon-format.md).

## Building

```
cmake -S . -B build
cmake --build build --config Release
```

Options: `-DSMON_BUILD_SHARED=ON` builds a shared library (DLL), `-DSMON_BUILD_TOOLS=OFF` leaves
out the tools. `cmake --install build` installs the header, library and a CMake package
(`find_package(SoundMonReplay)`, target `SoundMonReplay::soundmonreplay`).

## Using it

```c
#include "soundmonreplay.h"

smon_settings settings;
smon_default_settings(&settings);
settings.sample_rate = 48000;

smon_error error;
smon_module *module = smon_open(data, size, &settings, &error);
if (!module) { /* smon_error_string(error) */ }

int16_t buffer[2 * 1024];
size_t frames;
while ((frames = smon_render_s16(module, buffer, 1024)) > 0) {
	/* 'frames' stereo frames */
}
smon_close(module);
```

The header documents the rest:
- **Subsongs:** a SoundMon song can hold several. `smon_subsong_count` and `smon_get_subsong_info`
  give each one's start step, length and loop. `smon_select_subsong` picks one.
- **Settings:** loops and fade-out, interpolation, stereo separation, the Amiga's output filters,
  and the LED filter, which songs switch with option 3.
- **Playback control:** seeking, and muting voices.

## Tools

- `smonrender [options] song.bp out.wav` renders a WAV file using the public API only. `-d`
  lists the subsongs and their lengths.
- `smontrace song.bp [ticks]` prints the replayer's Paula register writes per tick, for
  comparison with the original replayers.

## Tests

- `tests/featuresongs.py <folder>` writes about 110 small test songs, each using one feature (an
  option or a synth effect), as V.2 and V.3 files.
- `tests/wavsongs.py <folder>` writes test songs for checking the sound against recordings from
  an Amiga emulator, with matching renders from the library.
- `tests/compare.py` compares the library with the original replayers, tick by tick: Paula's
  registers, and with `--mem` the waveforms the synth effects rewrite. It needs the original
  replayers running from game and music-disk memory, which can't be distributed as they come from
  the games and disks themselves. The format document's section 12 describes the method.

## Status

Compared against the original replayers (Plexu's V.2 build, Taipan's V.3 build, Music Mania's
SoundMon 1 build):

- **Test songs:** every feature test song matches exactly.
- **Real songs:** the whole modland corpus (336 files) matches exactly over 2,000 ticks (40
  seconds) each, in Paula's registers and in the waveforms the synth effects rewrite, except two
  V.2 files (chris, nr.1) that play an instrument number beyond 15, which can't be reproduced
  (see below). Music Mania's eight SoundMon 1 songs match too, except for the period written for
  one note in The Short One, which plays the same (see below).
- **Recordings:** WinUAE recordings of V.2, V.3 and SoundMon 1 songs match in level and tone, and
  a blind listening test, splicing recordings and the library's output together, found no
  audible difference.

Known differences from the Amiga:
- A sample instrument of length 0 plays silence here. On the Amiga it plays 65,536 words of
  whatever memory follows the song.
- An instrument number beyond 15 (when a step's sound transpose pushes it there) plays silence
  here. The original reads an instrument from whatever memory follows its table.
- In SoundMon 1 songs, a note above 36 (after the transpose) plays from the later versions' period
  table here. Music Mania's build reads past the end of its table instead; odd notes sound the
  same (Paula's fastest rate), even notes play a very low rumble there.
- History isn't modelled: every song starts from the replayer's fresh state, whereas a game that
  plays one song after another without resetting carries some state over.

## AI Disclaimer

This project uses AI development tools (Claude Code by Anthropic).

## Licence

MIT: see [LICENSE](LICENSE).
