# BP SoundMon format and replayer behaviour

The specification SoundMonReplay is written from, clean-room. §7 describes the replayer's tick in
the order the originals do things, which is what makes the library's output exact.

## Sources and rules

Only these sources are used:

1. **Modland's two format documents** (`I:\archive\modland\documents\format_documentation`).
   - Laurent Clevy's "SoundMon 1.1" description (2000, approved by Brian Postma).
   - Asle's "Sound-Monitor 2.0/3.0" description, with Brian Postma's comments from 2000.

   Where these disagree with the files or the traces, the files and traces win (§2.4).
2. **The modland corpus**: 336 files (1 `BPSM`, 214 `V.2`, 121 `V.3`), surveyed by script.
3. **Black-box traces of the original replayers in soundmon-trace.** These are the profiles in
   player.py: Paula writes and the replayer's variables, observed from outside. Two kinds of
   input are used:
   - the songs themselves;
   - small test songs built to use one feature at a time (§7).

Some data tables are read from the replayer's memory, such as the period table. Its code is never
read. Neither are other players' sources (UADE, NostalgicPlayer, Flod, Brian Postma's own
sources).

Status marks used below:
- **[traced]**: seen in the original replayers' output.
- **[corpus]**: checked against every modland file.
- **[doc]**: from the format documents only, not yet checked.
- **[open]**: unknown.

## 1. Versions and variants

| Tag at offset 26 | Editor | Files | Replayers seen (soundmon-trace profiles) |
|---|---|---|---|
| `BPSM` (4 bytes) | 1.0 | 1 | Music Mania (`musicmania`) |
| `V.2` | 1.1 | 214 | two kinds, below |
| `V.3` | 2.0 / 2.2 | 121 | one replayer (`taipan`, `jeah2`, `darkangel2`, all identical) |

The tag-to-editor mapping is Exotica's. The 2.x notes in the effect list therefore describe V.3
behaviour.

**V.2 has one replayer [traced].** The builds traced so far are:
- Plexu (1990);
- the Allister Brimble music disk;
- the SoundMon example-song disk;
- Supersonix;
- Alien Breed (1991);
- the Green Beret player.

From a fresh start they give the same Paula state on 40 random V.2 songs and on the six songs
that once seemed to split them (1,000 ticks each).

The "two kinds" in NOTES.md were an artefact of the profiles. The Plexu-layout profiles started
the arpeggio counter at 0 instead of 1. It counts down from there, so the arpeggio was silent
for the first 256 ticks of every song.

The remaining differences between builds:
- **Volume clamp:** Alien Breed's build clamps volume to 64 and the others don't. Paula plays any
  value of 64 or more at full volume, so this is inaudible.
- **The Commando player** calls the synth part as a separate routine after play, so its synth
  effects come one tick earlier. Its sample playback is identical. This is a difference in how
  the program calls the routines, not in the sequencer; the library follows the games.

**Game variants, not references:**
- Aztec Challenge: V.3 on three channels. It drops song voice 4 and leaves Paula channel 3 to the
  game.
- ProjectX: V.2 on one channel.

**SoundMon 1 [traced]:** the same as the V.2 replayer, comparing the BPSM originals with
modland's V.2 conversions, with one difference. On rows with no note, options 4 and 5 write the
new period in the same tick; V.2 writes it a tick later (§5).

**V.3 differs from V.2 in its options** (§5):
- option 6 is vibrato, not a repeat count;
- option 7 is an immediate jump;
- options E and F don't restart the sound and start an arpeggio;
- the arpeggio is a 4-tick cycle instead of 3, stored in the period variable;
- the order of the tick differs (§7).

The library's variant follows the file tag: `BPSM`, `V.2` or `V.3`. There's no per-song table.

## 2. File layout

All values are big-endian. The file is laid out as follows [corpus]:

| Offset | Size | Content |
|---|---|---|
| 0 | 26 | Title, padded with zeros or spaces |
| 26 | 3 | `V.2` or `V.3`. For SoundMon 1 the tag is 4 bytes, `BPSM`, covering offset 29 too |
| 29 | 1 | Number of waveforms (NW). Not present in `BPSM` files, which have no waveforms |
| 30 | 2 | Number of steps (NS) |
| 32 | 15 × 32 | Instruments 1-15 (§2.1) |
| 512 | NS × 16 | Steps: four voice entries of 4 bytes each (§2.2) |
| 512 + NS×16 | MT × 48 | Tracks 1..MT, where MT is the highest track number used in the steps (§2.3) |
| … | NW × 64 | Waveforms (signed 8-bit), used by synth instruments |
| … | | Sample data, for sample instruments 1-15 in order, each `length × 2` bytes |

**Corpus check.** With this layout, 332 of 336 files end exactly where the last sample ends. The
other four have trailing bytes: jeah2.bp3 +2, hawkeye.bp +224, boeskool.bp +2,140 and
labello.bp +11,776. Track 0 never appears in a step.

### 2.1 Instruments (32 bytes)

An instrument is a synth instrument if byte 0 is $FF; otherwise it's a sample. A `BPSM` file has
only samples.

**Sample** [corpus, traced]:

| Offset | Size | Field |
|---|---|---|
| 0 | 24 | Name |
| 24 | 2 | Length in words (0 = empty) |
| 26 | 2 | Loop start **in bytes** [corpus]. The documents say words, but in 215 looping samples only bytes fit inside the sample, and every value is even. |
| 28 | 2 | Loop length in words. 1 means no loop: after the start, the voice is pointed at an empty sample [traced]. |
| 30 | 2 | Volume, 0-64 (three V.2 files have values above 64) |

Three files have a loop that runs past the end even in bytes. All three loop the whole sample from
an offset of 2 or 4: 1st division manager, karate, power please.

**V.3 synth instrument.** The table below is Brian Postma's description, and the corpus value
ranges fit every field [doc, corpus]:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | $FF |
| 1 | 1 | Waveform number (main table) |
| 2 | 2 | Waveform length in words (16, 32, 64 typical) |
| 4 | 1 | ADSR control: 0 off, 1 once, 2 repeat |
| 5 | 1 | ADSR table (a waveform used as 0..255 values) |
| 6 | 2 | ADSR table length in bytes |
| 8 | 1 | ADSR speed (counter reload, 1 = fastest; pre-increment, so 0 is slowest) |
| 9 | 1 | LFO control |
| 10 | 1 | LFO table |
| 11 | 1 | LFO depth (divisor; 0 is taken as 1) |
| 12 | 2 | LFO table length in bytes |
| 14 | 1 | LFO delay (ticks before the LFO starts) |
| 15 | 1 | LFO speed |
| 16 | 1 | EG control (the "negating" effect) |
| 17 | 1 | EG table |
| 18 | 2 | EG table length in bytes |
| 20 | 1 | EG delay |
| 21 | 1 | EG speed |
| 22 | 1 | FX type: 0 none, 1 averaging, 2 inversion, 3 inverse inversion, 4 transform, 5 inverse transform |
| 23 | 1 | FX speed |
| 24 | 1 | FX delay |
| 25 | 1 | Mod control |
| 26 | 1 | Mod table |
| 27 | 1 | Mod speed |
| 28 | 1 | Mod delay |
| 29 | 1 | Volume (0-64) |
| 30 | 2 | Mod table length in bytes |

**V.2 synth instrument [corpus, traced].** Every field below except bytes 14 and 19 has been
confirmed by a test song on the Plexu build (§6).

- **Brian Postma's notes:** the V.3 synth instrument layout differs from V.2's. Neither document
  describes V.2's layout correctly. Clevy's table is the V.3 one, and it doesn't fit his own
  V.2 example.
- **The layout below** was worked out from the value distributions of the 1,054 V.2 synth
  instruments. It fits Clevy's Green Beret example byte for byte.
- **V.2 has no FX or modulation fields:** bytes 26-31 are zero in every file.

| Offset | Size | Field | Corpus values |
|---|---|---|---|
| 0 | 1 | $FF | |
| 1 | 1 | Waveform number | |
| 2 | 2 | Waveform length in words | 16 (459), 32, 8, 64, 4 |
| 4 | 1 | ADSR control | 1 (977), 0, 2 |
| 5 | 1 | ADSR table | |
| 6 | 2 | ADSR table length in bytes | 64 (938), 32 |
| 8 | 1 | ADSR speed | 1 (868), 2, 3 |
| 9 | 1 | LFO control | 2 (668), 0, 1 |
| 10 | 1 | LFO table | |
| 11 | 1 | LFO depth | 16, 10, 8, 6, 4 |
| 12 | 2 | LFO table length in bytes | 8 (601), 64, 12 |
| 14 | 1 | Probably the high byte of a word LFO delay (14-15), as byte 22 is for the EG; 0 in all but one file [open] | |
| 15 | 1 | LFO delay | 0 (390), 8, 10, 6 |
| 16 | 1 | LFO speed | 1 (905), 2, 0 |
| 17 | 1 | EG control | 0, 1, 2 |
| 18 | 1 | EG table | |
| 19 | 1 | With the EG on, values of 8 and up start the note with the first (byte 19 >> 3) bytes (at most 31) negated, and the EG carries on from there [traced with waveform memory compared]. Values 1-7 do something else, not traced: no file sets them with the EG on. Five files set it (20, 1, 32) | |
| 20 | 2 | EG table length in bytes | 64 (870), 16, 128 |
| 22 | 2 | EG delay, a word. A non-zero high byte (22) delays the EG past the end of the test | 0 (1,038), 6 |
| 24 | 1 | EG speed | 1 (721), 0, 2 |
| 25 | 1 | Volume | 64 (485), 45, 50, 40 |
| 26 | 6 | zero | |

### 2.2 Steps (16 bytes each)

There are four entries per step, one per song voice, each 4 bytes [traced]:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | Track number (1-based) |
| 2 | 1 | **Sound transpose** (signed): added to the instrument number |
| 3 | 1 | **Transpose** (signed): added to the note |

**Traced with a test song** on the Alien Breed, Plexu and Taipan profiles:
- **Byte 2:** +5 turns instrument 1 into instrument 6.
- **Byte 3:** +5 turns C-1 into F-1, and -1 turns it into B-0.
- **Out-of-range instrument numbers** are not clamped: -12 on instrument 1 played garbage
  pointers.

The two documents disagree about which byte is which. Asle's description and Brian's comment
on it even contradict each other. The trace settles it.

### 2.3 Tracks (48 bytes each)

A track is 16 rows of 3 bytes [corpus, doc]:

| Byte | Bits | Field |
|---|---|---|
| 0 | 8 | Note, signed. 0 = no note; 1 = C-1 … 36 = B-3 |
| 1 | 7-4 | Instrument 1-15 (0 = none) |
| 1 | 3-0 | Option (effect) 0-F |
| 2 | 8 | Option data |

**Note range:**
- **V.3 and BPSM:** every note is in 0-36.
- **V.2:** a handful of notes lie outside, from -109 to 113, three of them negative. They're
  probably in unused or damaged tracks [open].

### 2.4 Corrections to the documents

- **Loop start** is in bytes, not words (§2.1).
- **The V.2 synth instrument layout** isn't documented; §2.1 has the corpus-derived layout.
- **Step bytes 2 and 3:** byte 2 is the sound transpose (added to the instrument number) and
  byte 3 the transpose (added to the note), as traced (§2.2).
- **Offset 30** is the step count, not a pattern count.

## 3. Timing and start state

- **One tick per frame** (50 Hz on PAL). The replayer is called once per vertical blank or
  copper interrupt, or once per pass of the game's main loop [traced].
- **Speed** is the number of ticks per row, and it **defaults to 6** [traced]. Only option 2
  changes it. It is observed in the Alien Breed pre-menu savestate, and every V.3 init sets it.
  74 of 335 V.2/V.3 files have no speed command on step 0 row 0, so the default matters.
- **Start state** [traced: Alien Breed pre-menu savestate; Aztec's init]:
  - Every variable is 0 apart from the following.
  - The tick counter is 1: a note is read on the first call.
  - Speed is 6.
  - Each voice points at an empty sample with length 1.
  - The arpeggio counter and the V.2 repeat counter both start at 1. These are the two bytes
    after speed: observed in Commando's capture and Alien Breed's pre-menu savestate.
  - Song position: step 0, row 0.
  - Hardware: the first call writes LC/LEN for voices that aren't playing yet. This is part of
    the verified output.
- **Row advance:** a step is 16 rows. After row 15 the next step begins, and after the last
  step play wraps to step 0 [traced: `probes/testsong.py`, 96 ticks per step at the default
  speed, on all three profiles].
- **Sub-songs:** a song may hold several. One begins at a step right after a jump (option 7),
  e.g. Fantasy World Dizzy at steps 168 and 169 [traced].

## 4. Notes, periods and channels

- **Period table** [traced; read from the replayer's memory]: 84 entries, 7 octaves, the same in
  every build looked at so far:

  ```
  6848 6464 6080 5760 5440 5120 4832 4576 4320 4064 3840 3616
  3424 3232 3040 2880 2720 2560 2416 2288 2160 2032 1920 1808
  1712 1616 1520 1440 1360 1280 1208 1144 1080 1016  960  904
   856  808  760  720  680  640  604  572  540  508  480  452
   428  404  380  360  340  320  302  286  270  254  240  226
   214  202  190  180  170  160  151  143  135  127  120  113
   107  101   95   90   85   80   76   72   68   64   60   57
  ```

- **Note n plays entry n + 35** (zero-based) [traced]:
  - note 1 → 856, 13 → 428, 25 → 214, 36 → 113;
  - a transpose of -1 on note 1 → 904.

  So notes plus transpose can reach -35..48. What happens outside that range is [open].
- **Channels:** song voice 1 plays on Paula channel 3, voice 2 on 2, voice 3 on 1 and voice 4 on
  0. Every build does this [traced].
- **Volume** is written as bytes to $DFF0A9/B9/C9/D9 [traced]. Only Alien Breed's build clamps
  it to 64. Paula plays any value of 64 or more at full volume.
- **Period writes lag:** each tick, every build first writes each channel's period variable,
  then reads the row. A period change made by a row without a note is therefore heard on the next
  tick (SoundMon 1 also writes option 4/5's result at once). §7 gives the full order.

## 5. Options (effects)

The numbers below are file counts from the corpus, as "files with V.2 / V.3".

**How the options were traced.** Each one was traced with `probes/testsong.py` on samples, on
the builds below. "V.2" means every V.2 build agreed.
- `musicmania` (BPSM);
- `plexu`, `supersonix`, `alienbreed`, `greenberet` and `commando` (V.2);
- `taipan` (V.3).

**When options take effect.** Options act on the row's first tick (tick 0 of the row), whether
or not the row has a note, unless the table says otherwise.

**What a note does.**
- **Restart:** a note row normally restarts the sound (DMA off, then on) and resets the period to
  the note. It also stops the slide and the continuous arpeggio (9, D, E, F), but not option 0's
  arpeggio.
- **When there's no restart:** a note on the instrument already playing, or with no instrument
  number, doesn't turn DMA off, so the sound carries on. This applies when the instrument is a
  synth (all versions) or a looped sample (V.2 and SoundMon 1). V.3 restarts samples every time.
- **No instrument number:** the note plays the voice's previous instrument.
- **No note:** an instrument number on its own does nothing.

**Order of the voices.** The voices of a row are read in Paula channel order: song voice 4
(channel 0) first, voice 1 (channel 3) last. So where two voices set the same thing on one row
(speed, a V.3 jump), voice 1 wins.

| Opt | Name | Files (V.2 / V.3) | Behaviour [traced unless marked] |
|---|---|---|---|
| 0 | Arpeggio | 214 / 121 | **Sets the voice's arpeggio slot to the data; data 0 switches it off.** Only options 0, 4 and 5 change the slot: 4 and 5 clear it, and any other row (with or without a note, with any other option) leaves it running. So an empty row (option 0, data 0) ends it. Data $XY offsets the note by Y and by X semitones. It adds to the continuous arpeggio of options 9/D/E/F if both run. The cycle and its phase are in §7: V.2 runs 3 ticks (note, +Y, +X), V.3 4 ticks (+Y, +X, note, note), both from a global counter, so the phase depends on everything before. V.2 arpeggiates even a voice that has never played, from note 0 |
| 1 | Set volume | 196 / 103 | Sets the channel volume to the data byte at once. On a note row it replaces the instrument's volume for that note. The next note goes back to the instrument's volume. V.3 synths take it on the next tick (with ADSR, at the ADSR's next step). Only Alien Breed's build clamps to 64 |
| 2 | Set speed | 181 / 110 | Ticks per row, starting with the current row: speed 2 on row 0 at speed 6 makes row 0 last 2 ticks. 0 is read as 256 (a 256-frame row, seen in Alien Breed) |
| 3 | Filter | 166 / 85 | Data 0: sets CIA-A PRA bit 1 (LED off, filter off). Any other value clears it (filter on). The Plexu build doesn't write the CIA at all. The library will make this a setting, as TFMXreplay does |
| 4 | Period up | 56 / 30 | Subtracts the data from the period once, on the row's first tick, and clears option 0's arpeggio. On a no-note row, V.2 and V.3 make it heard a tick later (through the next per-tick period write); BPSM writes it at once |
| 5 | Period down | 27 / 9 | Adds the data to the period once; the same timing as 4 |
| 6 | **V.2/BPSM: set repeat count. V.3: vibrato** | 44 / 26 | V.2: sets the repeat counter used by option 7 (below); it has no audible effect on its own. V.3: a vibrato, from the tick after the row that sets it. The period offset is T[k] / data (unsigned data, truncated toward zero), where T = 128, 64, 0, −64, −128, −64, 0, 64 and k = (ticks since the song started − 1) mod 8: the phase runs from the start of the song, not from the note. So data 1 is ±128 and $12 is ±7. Data above 128 rounds every value to 0. A note switches it off. On a synth with an LFO the LFO replaces it |
| 7 | **V.2/BPSM: repeat. V.3: jump** | 50 / 23 | V.2: subtracts 1 from the repeat counter. If the result isn't 0, play continues at step *data* **keeping the row**. The step changes at once, so the voices read after this one (§5, order of the voices) take this row from the new step. A command on row 4 carries on in the target step at row 5; one on row 15 goes to the step after the target. The counter is a byte (+$88 from voice block 0 in the Plexu layout; watched counting 5, 4, 3, 2, 1, 0). It starts at 1, so with no option 6 the first pass doesn't jump; the next one wraps it to 255, giving 255 jumps. Option 6 with data n gives n−1 jumps; data 0 gives 255. V.3: a jump to step *data* at row 0, from the next row on, every time |
| 8 | Autoslide | 130 / 53 | Adds the signed data byte to the period on every tick after the row's first, until the next note. Data 0 stops it |
| 9 | Arpeggio, continuous | 74 / 52 | A second arpeggio (data 0 = off) that runs until the next note, through empty rows; it adds to option 0's. It can be started on a row with no note. V.3: on a row with no note it also restarts the ADSR from the next tick |
| A | Transpose off | 58 / 32 | High nibble non-zero: this note ignores the step's transpose (byte 3). Low nibble non-zero: it ignores the sound transpose (byte 2). Both nibbles work independently and on the same row |
| B | Change FX type | 6 / 3 | V.2: nothing (a note row plays as a plain note). V.3: sets the FX type (§6) from the next tick: on a row with no note, B 01 switches a running inversion to averaging and B 00 stops it. With a note, the note restarts everything as usual |
| C | Unused | 2 / 0 | |
| D | Swap FX direction + arpeggio | 2 / 16 | V.3: swaps inversion and inverse inversion (2 and 3), or transform and inverse transform (4 and 5), without restarting them: a waveform that was moving toward its target moves back toward the original. Otherwise as E, ADSR restart included |
| E | Restart ADSR + arpeggio | 24 / 30 | V.2: a plain note (everything restarted, no arpeggio). V.3: a note on this row changes the period with **no** sound restart: Paula hears it on the next tick, from the per-tick period write, and the synth part runs this tick with the old note. The ADSR starts again on the next tick, with or without a note: entry 0 then, entry 1 the tick after. On a synth without ADSR this writes a single volume: entry 0 of its ADSR table at a volume of 255. The EG, FX and waveform state carry on. The data sets the continuous arpeggio as option 9 does (data 0 = none) |
| F | New note, no restart + arpeggio | 31 / 28 | V.2: a plain note. V.3: as E, but the ADSR carries on: only the note (and arpeggio) change |

Not traced yet: what happens when two voices give options 2 or 7 on the same row. In one V.3
test, voices 1 and 4 both jumped on row 15, and voice 1's target won.

## 6. Synth instruments

**How it was traced:** test songs (`probes/testsong.py`, `synth()` and `synth_states()`) on the
Plexu build (V.2) and Taipan's (V.3). They record each tick's period, volume, sample pointer and
length, and the bytes of the song's waveform area. The two versions agree wherever both have the
effect, except where noted. The Commando player runs every synth effect one tick earlier [traced].

### 6.1 Playback

- **The sound:** a synth note plays its waveform straight from the song's waveform area
  (waveform n at waveform base + 64n), with the length from the instrument, looping. It is
  restarted on each note.
- **Volume:** the instrument's, or option 1's, unless ADSR is on.
- **Effect order:** the effects rewrite the waveform in the song's memory. Each tick they work
  from a 32-byte copy of the waveform's first 32 bytes, saved when the note starts. Bytes beyond
  32 are left alone, except for Mod's byte 32.
- **Which notes take a copy, and when** [traced with waveform memory compared]:
  - **V.2:** every synth note, at the note, as its voice reads the row. Another voice's changes
    to a shared waveform are in the copy if they are still in memory then; in Paula channel
    order, a voice read earlier on the row hasn't yet put back the copy of a later one (frantic
    freddie, yes, raider).
  - **V.3:** only an instrument with a waveform effect: the EG on, an FX type or Mod. One with
    none of them takes no copy, never rewrites its waveform, and options B and D can't give it an
    FX later (blockbrain). The copy is taken once every voice has read the row, so the other
    voices' notes on that row have put their own copies back first, but the changes of a voice
    still playing the waveform are in it (frantic freddie, newmod5).
- **When the waveform is restored:** on the voice's next note (any instrument) its copy is
  written back, so the waveform is as it was when the copy was taken. A V.3 voice without a copy
  writes nothing back, and leaves another voice's changes alone (pandora.bs).
- **What persists:** the effects' state isn't reset when a song starts. A song ripped from
  memory can differ from the file by a few bytes. A song that starts on another song's
  rewritten waveform also starts differently: this is history. The library starts every song
  from the file.

### 6.2 Tables and their schedule

**Tables.** ADSR, LFO, EG and Mod each read a table: a run of bytes from the waveform area,
starting at waveform *table* and *length* bytes long. A table can run on into the next waveform
(lengths of 128 work).

**Schedule** (LFO, EG and Mod, both versions):
- Entry 0 applies from the note.
- Entry k ≥ 1 applies from tick 1 + *delay* + k × *speed*.
- ADSR has no delay field and works by a counter (§6.3).

**Speed 0** [traced with waveform memory compared]:
- V.3: the slowest, 256 ticks per entry (ADSR, LFO, EG and Mod).
- V.2: the table never moves on from entry 0, and the LFO never writes at all (labello, gapulus).

**Length 0:** the table doesn't stop: it runs on through the following waveforms, as a length of
65536 would (traced for ADSR and EG; hawkeye.bp).

**Control byte:**
- **0:** off.
- **1:** once.
  - ADSR stops writing after its last entry, so the volume stays.
  - The LFO writes the plain period once when it ends (V.2 ends one entry earlier than V.3),
    then nothing.
  - The EG holds its last entry. Mod's end hasn't been traced.
- **2 (and 3):** repeat from entry 0.

### 6.3 ADSR (volume)

**Volume** = (((*entry* XOR $80) >> 2) × *V*) >> 6, where *V* is the instrument volume or option
1's value. Entry $80 is silence and $7F is full volume.

**Timing:** a counter, reloaded with the speed, and a position in the table.
- Each time the counter runs out, the entry at the position is written and the position moves
  on. Between those ticks no volume is written: an option 1 is held until the next step.
- A note sets the position to 0 and the counter to run out on the next tick: entry 0 on tick 1,
  entry 1 on tick 1 + speed.
- A V.3 restart (option E, D, or 9 on a row with no note) only puts the position back to 0. The
  counter's rhythm carries on, so entry 0 comes at its next step.
- In V.3 the synth part runs before the row is read (§7), so it uses the volume from before the
  row, and anything a row changes is heard from the next tick.

**Order of the voices:** voices are handled in Paula channel order. This matters when one voice's
ADSR table is another voice's waveform that an EG is rewriting.

### 6.4 LFO (period)

**Period** = note period + (signed *entry*) / *depth*, truncated toward zero; depth 0 counts as 1.
A positive entry lowers the pitch.

**When it's written:** only on the ticks the table moves on, from tick 1 + delay (speed 0: never
in V.2, every 256 ticks in V.3).
- **V.2:** the per-tick period write skips a synth with an LFO, so Paula holds the LFO's last
  period between those ticks. The LFO's period is taken before this tick's slide.
- **V.3:** the per-tick period write puts back the plain period between those ticks, and the LFO
  adds to the period from before this tick's row.
- **Played once:** at its end it writes the plain period once, then nothing.

### 6.5 EG (negation)

The EG keeps the first N bytes of the waveform negated, where N = (*entry* XOR $80) >> 3 (0-31). It
works only on the bytes that enter or leave that range [traced with waveform memory compared]:
- a byte entering it becomes the saved copy's byte, negated;
- a byte leaving it becomes the copy's byte;
- the bytes that stay inside keep whatever the FX has made of them since.

On its own, that's the same as negating the copy's first N bytes every tick.
- The first change is on tick 1.
- It runs before the FX in each tick.
- N doesn't depend on the waveform length. A 16-byte waveform with N = 31 has bytes negated past
  its end.
- V.2 byte 19 starts the note with bytes already negated (§2.1).

### 6.6 FX (V.3 only)

The FX change the waveform's first 32 bytes from tick 1 on, after the EG [traced with waveform
memory compared]. Byte 23, *FX speed*, means two different things:
- **Averaging:** it runs on a countdown that only moves while the type is averaging. From tick
  1 + *delay* (byte 24) it runs whenever the count is out, and the count restarts at *speed*. So
  on a note it runs on tick 1 + delay and then every *speed* ticks; at speed 0 it runs once.
  Averaging switched on later by option B or D runs on the next tick, then every *speed* ticks
  from there (ok.bp3).
- **The other four:** they run every tick, and each byte steps *speed* toward its target.
  - A byte that doesn't land exactly on its target swings either side of it: 1, −2, 1, −2 for a
    target of 0 at speed 3.
  - Speed 0 changes nothing.

Byte 24 (*FX delay*) delays only averaging (novel, spacy); the other types ignore it.
- **1, averaging:**
  - Each byte becomes the mean of its new left neighbour and its old right neighbour:
    b'[i] = (b'[i−1] + b[i+1]) >> 1, with arithmetic shift.
  - b'[0] uses the byte just before the waveform in memory, and b'[31] uses byte 32.
  - It repeats on itself, so the waveform smooths further each time.
- **2, inversion:** each byte moves toward the time-reversed waveform (b[31−i] of the copy).
- **3, inverse inversion:** each byte moves back toward the copy. On a fresh note nothing
  changes.
- **4, transform:** each byte moves toward the next waveform (main waveform + 1).
- **5, inverse transform:** moves back toward the copy.

Options B and D change the type from the next tick, and neither changes the copy.
- **Option D** flips the type's lowest bit, without restarting anything. That swaps 2↔3 and 4↔5,
  and switches averaging (1) off.
- **Option B** sets the type, but only on a row without a note. A note that restarts the sound
  sets its instrument's own type.

### 6.7 Mod (V.3 only)

Writes the current table entry into waveform byte 32 (one byte, whatever the waveform length),
on the same schedule as the other tables. The first visible change is entry 1, at tick 2 with
speed 1.

### 6.8 Still open

- V.2 byte 14, and byte 19's values 1-7 (§2.1). No file uses them with the EG on.
- The Commando player's leftover synth period differences: they show up once its one-tick shift
  is allowed for.

## 7. The tick, in order [traced]

Every per-voice step below goes through the voices in Paula channel order: song voice 4
(channel 0) first, voice 1 (channel 3) last. "Per-tick write" means each channel's period, loop
pointer and loop length (LC/LEN); a sample's loop comes into effect when Paula finishes its
current block.

**V.2 and SoundMon 1:**
1. The arpeggio counter steps: 3, 2, 1, 3, … (it starts at 1, so the first tick makes it 3).
2. For each voice that has played:
   - the synth part (ADSR volume, LFO, EG);
   - then the slide is added to the period.
3. The per-tick write. A synth with an LFO gets its period from the LFO instead, written only on
   the LFO's ticks.
4. The arpeggio: for each voice with one, the period of the note plus the counter's offset.
   - Counter 2 gives the note, 1 the low nibble's offset, 3 the high nibble's.
   - The period variable isn't changed, so in V.2 an arpeggio hides the LFO and the slide.
   - This applies even to a voice that has never played (from note 0).
5. The tick counter steps. When it runs out:
   - the row is read, voice by voice;
   - a note turns DMA off, then writes the period, LC/LEN and volume (sample: $FF first, then the
     volume);
   - a V.2 looped sample also writes its loop LC/LEN at once, so Paula starts at the loop;
   - an option 7 changes the step at once;
   - then DMA goes on for all the notes;
   - the counter reloads with the speed (0 = 256).

**V.3:**
1. The arpeggio counter steps: 0, 3, 2, 1, 0, … (it starts at 1).
2. For each voice that has played, the slide is added to the period variable.
3. The per-tick write, the period being the period variable.
4. The vibrato, for each voice that has one: it writes period + vibrato, unless the voice is a
   synth with an LFO.
5. The arpeggio.
   - Counter 1 gives the low nibble's offset, 0 the high nibble's, 3 and 2 the note.
   - It's stored in the period variable, so it overwrites the slide; a stopped arpeggio leaves
     the period where it was, and the next tick's write repeats it.
6. The synth part for each synth voice that has played: ADSR, LFO (period written only on its
   ticks), EG, FX and Mod.
   - It runs before the row, so on a note's tick it carries on with the old note's state.
   - Anything the row changes (options 1, 9, B, D, E, a new note) is taken up on the next tick.
7. The tick counter steps, and the row is read as in V.2.
   - A looped sample starts with LEN = loop start / 2 + loop length, and moves to its loop on the
     next tick.
   - Option 7 takes effect when the next row is read.
   - The synth notes' waveform copies are taken once the whole row has been read (§6.1).

## 8. Method

- **Test songs** use one feature at a time: probes/testsong.py, trace_options.py and
  trace_synth.py in soundmon-trace, and tests/featuresongs.py here.
- **tests/compare.py** compares the library with an original replayer, tick by tick, on those
  songs and on the corpus: Paula's registers, and with `--mem` the waveform area the synth
  effects rewrite.
- **WinUAE recordings** (48 kHz, 100% separation, no interpolation, no filter) check the sound:
  each tick's level and spectrum against a render without filters, which allows for Paula's
  timing within a tick.
- **Each result** goes into this document with its status mark, then into the library.

**Covered so far:** every feature test song; the modland corpus over 2,000 ticks per song (all but
the files listed below), on the V.2, V.3 and SoundMon 1 builds above, plus the Jeah2 and Dark
Angel 2 V.3 builds on their own songs; recordings of the 1.1 editor (V.2: the test songs and
its 13 example songs) and of OrchDelight (V.3: its seven songs, each from its start), which match
in level and tone.

## 9. Open questions

1. V.2 synth byte 14, and byte 19's values 1-7 (§2.1). No file uses them with the EG on.
2. The Commando player's leftover synth differences (§6.8).
3. V.3 option D on a row with no note, on a synth with FX and no ADSR: the single volume it
   writes reads a table byte that the FX has rewritten, and which byte isn't known.
4. Transposes that go out of range, and instrument numbers beyond 15 after the sound transpose.
   The original then reads its instrument from whatever memory follows the table (nr.1.bp).
5. Three V.2 files too large for the reference profile's memory (acid mix, freddy - mix,
   lightstorm) haven't been compared.
6. No recording of a SoundMon 1 song yet (a Music Mania savestate would do).
