# SoundMonReplay

A C library that renders Brian Postma's SoundMon songs (Amiga, 1990-1993): BP SoundMon 1
(`BPSM`), 2 (`V.2`) and 3 (`V.3`) files, as found in modland's `BP SoundMon 1/2/3` folders.

It's a clean-room replayer: written from a description of the format (docs/SoundMon-format.md)
and checked, tick by tick, against the original replayers running in an Amiga emulator, without
reading any replayer's code. Paula's register writes match the originals exactly on the songs
compared so far; see Status.

MIT licence (see LICENSE). Dependency-free C/C++17 with a plain C API.

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

- `tests/compare.py <profile> <ticks> <songs...>` compares the library with an original replayer,
  tick by tick. The originals run in a private harness (soundmon-trace, WinUAE savestates of games
  and music disks), so this test needs that repository next to this one. `--mem` also compares
  the song's waveform area (which the synth effects rewrite) after every tick; `-v` shows the
  first differences.
- `tests/featuresongs.py <folder>` writes about 110 small test songs, each using one feature (an
  option or a synth effect), as V.2 and V.3 files, for compare.py.

## Status

Compared against the original replayers (Plexu's V.2 build, Taipan's V.3 build, Music Mania's
SoundMon 1 build):

- **Test songs:** every feature test song matches exactly, except one V.3 corner: option D on a
  row with no note, on a synth with FX and no ADSR.
- **Real songs:** the whole modland corpus (336 files) matches exactly over 2,000 ticks (40
  seconds) each, in Paula's registers and in the waveforms the synth effects rewrite, with these
  exceptions:
  - Three V.2 files are too large for the reference profile's memory, so they aren't compared.
  - Two V.2 files (chris, nr.1) play an instrument number beyond 15, which can't be reproduced
    (see below).

Known differences from the Amiga:
- A sample instrument of length 0 plays silence here. On the Amiga it plays 65,536 words of
  whatever memory follows the song.
- An instrument number beyond 15 (when a step's sound transpose pushes it there) plays silence
  here. The original reads an instrument from whatever memory follows its table.
- History isn't modelled: every song starts from the replayer's fresh state, whereas a game that
  plays one song after another without resetting carries some state over.
