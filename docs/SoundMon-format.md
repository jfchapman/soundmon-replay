# The BP SoundMon music format

BP SoundMon (Sound Monitor) is Brian Postma's music editor for the Amiga, from the late 1980s (the 1.1 editor is dated 1989) into the early 1990s. Its songs are tracker-like: four voices, each playing a track of notes in a sequence of steps. Its instruments are either samples or *synths*, which play a short waveform and reshape it as it plays, with a volume envelope, an LFO and several effects that rewrite the waveform itself. Songs were used in games (*Alien Breed*, *Plexu*, *Fantasy World Dizzy*), on music disks and in demos, and the replayer was copied into each, so there's no single official player.

This document describes the format as far as it's known: the file layout, what every option and synth effect does, and the order in which a tick does things, which is what makes a player's output match the original's exactly. It's detailed enough to write a player from, and [SoundMonReplay](../README.md) was written from it. Sections 1–9 give the behaviour of the three versions, with the differences noted where they matter. Section 10 covers how the replayer builds differ, section 12 explains how the behaviour was measured, and section 13 lists what's still unknown.

**Conventions.** All values are big-endian. "Word" means 16 bits. `$` means hexadecimal. "Song voice" 1–4 is a voice of the song (a column of the steps); "channel" 0–3 is one of Paula's four hardware channels (§5). Counts of files refer to the 336 files in the modland collection's `BP SoundMon 1`, `2` and `3` folders.

## Contents

1. [Concepts](#1-concepts)
2. [Versions](#2-versions)
3. [File layout](#3-file-layout)
4. [Timing and start state](#4-timing-and-start-state)
5. [Notes, periods and channels](#5-notes-periods-and-channels)
6. [Options](#6-options)
7. [Synth instruments](#7-synth-instruments)
8. [The tick, in order](#8-the-tick-in-order)
9. [Subsongs, ends and loops](#9-subsongs-ends-and-loops)
10. [Replayer builds](#10-replayer-builds)
11. [The songs in modland](#11-the-songs-in-modland)
12. [How this was measured](#12-how-this-was-measured)
13. [Open questions](#13-open-questions)
14. [Earlier descriptions](#14-earlier-descriptions)

---

## 1. Concepts

- **Step:** one line of the song's sequence. It names a track for each of the four song voices, with a transpose for its notes and one for its instrument numbers.
- **Track:** 16 rows, each with an optional note, instrument and option (effect).
- **Row:** one line of a track. A row lasts *speed* ticks (6 by default).
- **Tick:** one call of the replayer, once a frame (about 50 times a second on a PAL Amiga).
- **Instrument:** one of 15. A *sample* instrument plays sample data from the file. A *synth* instrument plays a waveform from the file's waveform area, looping, and its effects rewrite that waveform in memory as it plays (§7).
- **Waveform:** 64 bytes of signed 8-bit data. Synths play them, and their envelopes and tables read them as data.
- **Option:** the row's effect, 0–F (§6).

The replayer's only output is writes to Paula's registers: sample start (LC), length (LEN), period (PER), volume (VOL) and DMA on or off. A player has to reproduce those writes, and the waveforms the synth effects rewrite, and how Paula responds to them (§5).

---

## 2. Versions

The tag at offset 26 says which version wrote the file:

| Tag | Editor | Files in modland | Instruments |
|---|---|---|---|
| `BPSM` (4 bytes) | SoundMon 1.0 | 1 | Samples only |
| `V.2` | SoundMon 1.1 | 214 | Samples and synths (ADSR, LFO, EG) |
| `V.3` | SoundMon 2.0 / 2.2 | 121 | Samples and synths, adding FX and Mod |

The editor names follow Exotica's mapping. **SoundMon 1** files play as V.2 files do, apart from one detail of options 4 and 5 (§6). **V.3** differs from V.2 in its options (6 is a vibrato, 7 an immediate jump, E and F don't restart the sound), in a 4-tick arpeggio cycle, and in the order of the tick (§8). A player can choose the version from the tag alone: every replayer build seen plays the same version the same way (§10).

---

## 3. File layout

| Offset | Size | Content |
|---|---|---|
| 0 | 26 | Title, padded with zeros or spaces |
| 26 | 3 | `V.2` or `V.3`. For SoundMon 1 the tag is 4 bytes, `BPSM`, covering offset 29 too |
| 29 | 1 | Number of waveforms (NW). Not present in `BPSM` files, which have none |
| 30 | 2 | Number of steps (NS) |
| 32 | 15 × 32 | Instruments 1–15 (§3.1) |
| 512 | NS × 16 | Steps: four voice entries of 4 bytes each (§3.2) |
| 512 + NS × 16 | MT × 48 | Tracks 1 to MT, where MT is the highest track number the steps use (§3.3) |
| … | NW × 64 | Waveforms (signed 8-bit), used by synth instruments |
| … | | Sample data for the sample instruments 1–15 in order, each *length* × 2 bytes |

There's no track count in the header: the waveforms start straight after the highest track a step names, so a file with unused tracks after that one can't be read correctly. Track 0 never appears in a step.

### 3.1 Instruments (32 bytes)

An instrument is a synth if byte 0 is $FF, and a sample otherwise. A `BPSM` file has only samples.

**Sample:**

| Offset | Size | Field |
|---|---|---|
| 0 | 24 | Name |
| 24 | 2 | Length in words (0 = empty) |
| 26 | 2 | Loop start, **in bytes** |
| 28 | 2 | Loop length in words. 1 means no loop: when the sample ends, the channel plays silence |
| 30 | 2 | Volume, 0–64 |

The format documents give the loop start in words. In the 215 looping samples in modland, every value is even and only bytes fit inside the sample.

A length of 0 is an empty instrument. Paula reads a length of 0 as 65,536 words, so on the Amiga a note on one plays whatever memory follows the sample's start; a player will want silence instead.

**V.3 synth:**

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | $FF |
| 1 | 1 | Waveform number |
| 2 | 2 | Waveform length in words (16, 32 and 64 are typical) |
| 4 | 1 | ADSR control: 0 off, 1 once, 2 repeat (§7.2) |
| 5 | 1 | ADSR table (a waveform number) |
| 6 | 2 | ADSR table length in bytes |
| 8 | 1 | ADSR speed |
| 9 | 1 | LFO control |
| 10 | 1 | LFO table |
| 11 | 1 | LFO depth (a divisor; 0 counts as 1) |
| 12 | 2 | LFO table length in bytes |
| 14 | 1 | LFO delay |
| 15 | 1 | LFO speed |
| 16 | 1 | EG control |
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
| 29 | 1 | Volume, 0–64 |
| 30 | 2 | Mod table length in bytes |

**V.2 synth.** The V.2 layout differs from V.3's, and neither format document describes it. This one was worked out from the value distributions of modland's 1,054 V.2 synths, fits the example in Laurent Clevy's description byte for byte, and was confirmed field by field with test songs. V.2 has no FX or Mod: bytes 26–31 are zero in every file.

| Offset | Size | Field | Common values in modland |
|---|---|---|---|
| 0 | 1 | $FF | |
| 1 | 1 | Waveform number | |
| 2 | 2 | Waveform length in words | 16, 32, 8, 64, 4 |
| 4 | 1 | ADSR control | 1, 0, 2 |
| 5 | 1 | ADSR table | |
| 6 | 2 | ADSR table length in bytes | 64, 32 |
| 8 | 1 | ADSR speed | 1, 2, 3 |
| 9 | 1 | LFO control | 2, 0, 1 |
| 10 | 1 | LFO table | |
| 11 | 1 | LFO depth | 16, 10, 8, 6, 4 |
| 12 | 2 | LFO table length in bytes | 8, 64, 12 |
| 14 | 1 | Unknown. Probably the high byte of a word LFO delay, as byte 22 is for the EG's. 0 in all but one file | |
| 15 | 1 | LFO delay | 0, 8, 10, 6 |
| 16 | 1 | LFO speed | 1, 2, 0 |
| 17 | 1 | EG control | 0, 1, 2 |
| 18 | 1 | EG table | |
| 19 | 1 | With the EG on, values of 8 and up start each note with the waveform's first (byte 19 >> 3) bytes (at most 31) already negated, and the EG carries on from there (§7.5). Values 1–7 are unknown: no file sets them with the EG on | |
| 20 | 2 | EG table length in bytes | 64, 16, 128 |
| 22 | 2 | EG delay, a word | 0, 6 |
| 24 | 1 | EG speed | 1, 0, 2 |
| 25 | 1 | Volume, 0–64 | 64, 45, 50, 40 |
| 26 | 6 | Zero | |

### 3.2 Steps (16 bytes)

Four entries, one per song voice:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | Track number (from 1) |
| 2 | 1 | **Sound transpose** (signed): added to each instrument number |
| 3 | 1 | **Transpose** (signed): added to each note |

So a sound transpose of +5 turns instrument 1 into instrument 6, and a transpose of −1 turns C-1 into B-0. Neither is clamped. An instrument number pushed outside 1–15 makes the original read an instrument from whatever memory follows its instrument table.

### 3.3 Tracks (48 bytes)

16 rows of 3 bytes:

| Byte | Bits | Field |
|---|---|---|
| 0 | 7–0 | Note, signed. 0 = no note, 1 = C-1 … 36 = B-3 |
| 1 | 7–4 | Instrument 1–15 (0 = none) |
| 1 | 3–0 | Option 0–F (§6) |
| 2 | 7–0 | Option data |

---

## 4. Timing and start state

- **One tick per frame:** the replayer is called once per vertical blank (or copper interrupt, or pass of a game's main loop). On a PAL Amiga a frame is 313 lines of 227 colour clocks, so a tick is 20.03 ms (49.92 Hz).
- **Speed** is the number of ticks per row. It's 6 at the start, and only option 2 changes it. A fifth of modland's files never set it on their first row, so the default matters.
- **Start state:**
  - The song starts at step 0, row 0, and the first tick reads row 0.
  - The arpeggio counter and the V.2 repeat counter both start at 1.
  - Each channel starts on an empty sample of one word, and the first tick writes LC, LEN and period 0 for every channel.
  - Every other variable starts at 0.
- **Rows and steps:** a step is 16 rows. After row 15 comes the next step, and after the last step the song goes back to step 0.

---

## 5. Notes, periods and channels

**Periods.** 84 entries, seven octaves, the same in every V.2 and V.3 build:

```
6848 6464 6080 5760 5440 5120 4832 4576 4320 4064 3840 3616
3424 3232 3040 2880 2720 2560 2416 2288 2160 2032 1920 1808
1712 1616 1520 1440 1360 1280 1208 1144 1080 1016  960  904
 856  808  760  720  680  640  604  572  540  508  480  452
 428  404  380  360  340  320  302  286  270  254  240  226
 214  202  190  180  170  160  151  143  135  127  120  113
 107  101   95   90   85   80   76   72   68   64   60   57
```

Note *n* plays entry *n* + 35 (from 0): note 1 is 856, 13 is 428, 25 is 214 and 36 is 113, and a transpose of −1 on note 1 gives 904. So with its transpose a note can reach −35 to 48. What happens beyond that is unknown.

SoundMon 1's table has no entries past note 36, at least in the one build measured (*Music Mania*). Notes 37–48 read what follows it: period $0002 for odd notes, and around 14,400 for even ones, which changed between reads, so it's probably memory that changes as the replayer runs. Paula can't play faster than period 113 (below), so the odd notes sound the same as the V.2 table's; the even ones give a very low rumble. A player can reasonably use the V.2 table. The only song known to go past note 36 is *Music Mania*'s *The Short One*, which plays note 39 once.

**Channels.** Song voice 1 plays on Paula channel 3, voice 2 on channel 2, voice 3 on channel 1 and voice 4 on channel 0. Channels 0 and 3 are on the left, 1 and 2 on the right.

**Paula.** What a player needs of the hardware:
- **Volume** is a byte written to each channel's volume register. Values of 64 and up play at full volume.
- **Periods below 113** play at the rate of period 113, as fast as Paula can fetch samples.
- **New loop pointers** (LC and LEN written while a sample plays) take effect when the current block ends.
- **The output filters:** the A500's fixed low-pass at about 4.9 kHz, and the switchable "LED" filter at about 3.3 kHz, which option 3 turns on and off.

---

## 6. Options

The file counts are "V.2 files / V.3 files" using the option.

**When options act.** On the row's first tick, whether or not the row has a note, unless the table says otherwise.

**What a note does.**
- **Restart:** a note normally restarts the sound (DMA off, then on) and sets the period to the note's. It also stops the slide and the continuous arpeggio (options 9, D, E and F), but not option 0's arpeggio.
- **No restart:** a note on the instrument already playing doesn't turn DMA off, so the sound carries on with the new note. This applies to synths in all versions, and to looped samples in V.2 and SoundMon 1. V.3 restarts samples every time.
- **No instrument number:** the note plays the voice's previous instrument.
- **No note:** an instrument number on its own does nothing.

**Order of the voices.** The voices of a row are read in channel order: song voice 4 (channel 0) first, voice 1 (channel 3) last. So when two voices set the same thing on one row, such as the speed or a V.3 jump, voice 1 wins.

| Opt | Name | Files | Behaviour |
|---|---|---|---|
| 0 | Arpeggio | 214 / 121 | Sets the voice's arpeggio to the data; data 0 switches it off. Only options 0, 4 and 5 change it: 4 and 5 clear it, and any other row leaves it running, so an empty row (option 0, data 0) ends it. Data $XY offsets the note by Y and by X semitones. It adds to the continuous arpeggio of options 9, D, E and F if both run. The cycle (§8) runs from a counter shared by all voices, so its phase depends on everything before. V.2 arpeggiates even a voice that has never played, from note 0 |
| 1 | Volume | 196 / 103 | Sets the channel volume to the data at once. On a note row it replaces the instrument's volume for that note, and the next note goes back to the instrument's. V.3 synths take it on the next tick (with ADSR, at the ADSR's next step) |
| 2 | Speed | 181 / 110 | Ticks per row, from the current row: speed 2 on a row at speed 6 makes that row last 2 ticks. 0 means 256 |
| 3 | Filter | 166 / 85 | Data 0 turns the LED filter off, any other value on. (The *Plexu* build doesn't touch the filter at all) |
| 4 | Period up | 56 / 30 | Subtracts the data from the period once, on the row's first tick, and clears option 0's arpeggio. On a row with no note, V.2 and V.3 make the change heard on the next tick (through the next tick's period write); SoundMon 1 writes it at once |
| 5 | Period down | 27 / 9 | Adds the data to the period once, with the same timing as 4 |
| 6 | V.2: repeat count. V.3: vibrato | 44 / 26 | **V.2 and SoundMon 1:** sets the repeat counter option 7 uses. Inaudible on its own. **V.3:** a vibrato from the next tick: the period offset is T[k] / data (truncated toward zero), where T = 128, 64, 0, −64, −128, −64, 0, 64 and k = (ticks since the song started − 1) mod 8. The phase runs from the start of the song, not from the note. Data 1 gives ±128, $12 gives ±7, and data above 128 gives 0. A note stops it, and on a synth with an LFO the LFO replaces it |
| 7 | V.2: repeat. V.3: jump | 50 / 23 | **V.2 and SoundMon 1:** subtracts 1 from the repeat counter (a byte). If the result isn't 0, play continues at step *data*, **keeping the row**: a command on row 4 carries on at row 5 of the target step, one on row 15 at the step after it. The step changes at once, so the voices read after this one take this row from the new step. The counter starts at 1, so with no option 6 the first pass doesn't jump and the next wraps it to 255, giving 255 jumps. Option 6 with data *n* gives *n* − 1 jumps; data 0 gives 255. **V.3:** a jump to step *data*, row 0, from the next row, every time |
| 8 | Slide | 130 / 53 | Adds the signed data to the period on every tick after the row's first, until the next note. Data 0 stops it |
| 9 | Continuous arpeggio | 74 / 52 | A second arpeggio (data 0 = off) that runs until the next note, through empty rows, and adds to option 0's. It can start on a row with no note. **V.3:** on a row with no note it also restarts the ADSR from the next tick |
| A | Transpose off | 58 / 32 | High nibble non-zero: the note ignores the step's transpose. Low nibble non-zero: it ignores the sound transpose. The two work independently |
| B | FX type | 6 / 3 | **V.2:** nothing. **V.3:** sets the FX type (§7.6) from the next tick, on a row with no note: B 01 turns a running inversion into averaging, and B 00 stops it. On a note row, the note sets its instrument's own type as usual |
| C | Unused | 2 / 0 | |
| D | FX direction | 2 / 16 | **V.2:** a plain note. **V.3:** flips the FX type's lowest bit from the next tick, without restarting anything: inversion and inverse inversion swap (2 and 3), and so do transform and inverse transform (4 and 5), so a waveform moving toward its target moves back again. Averaging (1) stops. Otherwise as E, with the ADSR restart |
| E | ADSR restart | 24 / 30 | **V.2:** a plain note. **V.3:** a note on this row changes the period with **no** sound restart. Paula hears it on the next tick, from the period write, and the synth part runs this tick with the old note. The ADSR starts again on the next tick, with or without a note: entry 0 then, entry 1 the tick after. On a synth without ADSR it writes a single volume instead, entry 0 of its ADSR table at a volume of 255, and nothing after; if an FX is rewriting that table (the synth's own waveform), the entry is read as it is when the synth part runs on the next tick, before that tick's FX. The EG, FX and waveform carry on. The data sets the continuous arpeggio as option 9 does (0 = none) |
| F | New note | 31 / 28 | **V.2:** a plain note. **V.3:** as E, but the ADSR carries on: only the note and the arpeggio change |

---

## 7. Synth instruments

### 7.1 Playback and the waveform copy

A synth note plays its waveform straight from the song's waveform area (waveform *n* is at the waveform base + 64*n*), for the instrument's length, looping. Its volume is the instrument's, or option 1's, unless the ADSR is on.

The effects rewrite the waveform where it is, in the song's memory, so several voices playing one waveform hear each other's changes. To do that, each note saves a **copy** of the waveform's first 32 bytes, and the effects work from it. Bytes past 32 are never touched, apart from Mod's byte 32.

- **V.2** takes the copy at every synth note, as the voice reads its row. A voice read earlier on the same row (in channel order) may still find a later voice's changes in memory, and copies them.
- **V.3** takes the copy only for an instrument with a waveform effect (the EG on, an FX type or Mod), and only once every voice has read the row, so the other voices' notes on that row have already put their own copies back. The changes of a voice still playing the waveform end up in the copy. An instrument with no waveform effect takes no copy, never rewrites its waveform, and can't be given an FX later by option B or D.
- **The copy is put back** at the voice's next note, whatever its instrument, so the waveform returns to how it was when the copy was taken. A V.3 voice without a copy puts nothing back, and so leaves another voice's changes in place.

Nothing resets the waveforms between songs. A song played after another that rewrote its waveforms starts from the rewritten state, and a song ripped from a game's memory may differ from the original file by a few waveform bytes. Starting every song from its file is the natural choice for a player.

### 7.2 Tables and their schedule

The ADSR, LFO, EG and Mod each read a **table**: a run of bytes from the waveform area, starting at waveform *table* and *length* bytes long. A table can run on into the following waveforms, and a length of 0 means 65,536: the table just keeps running through the waveform area.

**Schedule** (LFO, EG and Mod): entry 0 applies from the note, and entry *k* ≥ 1 from tick 1 + *delay* + *k* × *speed*. The ADSR has no delay and works with a counter instead (§7.3).

**Speed 0:** in V.3, 256 ticks per entry for all four. In V.2 the table stays on entry 0, and the LFO never writes at all.

**Control byte:**
- **0:** off.
- **1:** once. The ADSR stops writing after its last entry, so the volume stays. The LFO writes the plain period once when it ends (V.2 one entry earlier than V.3), then nothing. The EG holds its last entry.
- **2 (and 3):** repeat from entry 0.

### 7.3 ADSR (volume)

**Volume** = (((*entry* XOR $80) >> 2) × *V*) >> 6, where *V* is the instrument's volume or option 1's. Entry $80 is silence and $7F full volume.

**Timing:** a counter, reloaded with the speed, and a position in the table. Each time the counter runs out, the entry at the position is written and the position moves on; between those ticks no volume is written, so an option 1 holds until the ADSR's next step. A note sets the position to 0 and the counter to run out on the next tick: entry 0 comes on tick 1 and entry 1 on tick 1 + speed. A V.3 restart (options 9, D and E on a row with no note) only puts the position back to 0, and the counter keeps its rhythm, so entry 0 comes at its next step.

Voices are handled in channel order, which matters when one voice's ADSR table is a waveform another voice's EG is rewriting.

### 7.4 LFO (period)

**Period** = note period + (signed *entry*) / *depth*, truncated toward zero. A positive entry lowers the pitch.

The LFO writes the period only on the ticks its table moves on, from tick 1 + *delay*.
- **V.2:** the per-tick period write (§8) skips a synth with an LFO, so Paula keeps the LFO's last period between its ticks. The LFO uses the period from before this tick's slide.
- **V.3:** the per-tick write puts the plain period back between the LFO's ticks, and the LFO adds to the period from before this tick's row.
- **Played once:** at its end it writes the plain period once, then nothing.

### 7.5 EG (negation)

The EG keeps the waveform's first *N* bytes negated, where *N* = (*entry* XOR $80) >> 3, so 0–31. It works only on the bytes entering or leaving that range: a byte entering it becomes the copy's byte negated, a byte leaving it becomes the copy's byte, and the bytes that stay inside keep whatever the FX has made of them. On its own, that's the same as negating the copy's first *N* bytes every tick.

The first change is on tick 1, and the EG runs before the FX each tick. *N* doesn't depend on the waveform's length, so a 16-byte waveform with *N* = 31 has bytes negated past its end. In V.2, byte 19 can start a note with bytes already negated (§3.1).

### 7.6 FX (V.3)

The FX change the waveform's first 32 bytes from tick 1, after the EG. Byte 23, the FX speed, means two different things:
- **Averaging** runs on a countdown that only moves while the type is averaging. From tick 1 + *delay* (byte 24) it runs whenever the count is out, and the count starts again at *speed*. So after a note it runs on tick 1 + *delay* and then every *speed* ticks, and at speed 0 only once. Averaging that option B or D switches on later runs on the next tick, then every *speed* ticks from there.
- **The other four types** run every tick from tick 1, ignoring the delay, and each byte steps *speed* toward its target. A byte that doesn't land exactly on its target swings either side of it (1, −2, 1, −2 for a target of 0 at speed 3), and speed 0 changes nothing.

The types:
1. **Averaging:** each byte becomes the mean of its new left neighbour and its old right neighbour, b′[i] = (b′[i − 1] + b[i + 1]) >> 1 (an arithmetic shift). b′[0] uses the byte just before the waveform in memory, and b′[31] uses byte 32. It works on its own result, so the waveform gets smoother each time.
2. **Inversion:** each byte moves toward the copy played backwards (b[31 − i]).
3. **Inverse inversion:** each byte moves back toward the copy. On a fresh note nothing changes.
4. **Transform:** each byte moves toward the next waveform (waveform number + 1).
5. **Inverse transform:** each byte moves back toward the copy.

Options B and D change the type from the next tick (§6), and neither changes the copy.

### 7.7 Mod (V.3)

Writes the current table entry into byte 32 of the waveform (one byte, whatever the waveform's length), on the same schedule as the other tables. The first change you can see is entry 1, on tick 2 at speed 1.

---

## 8. The tick, in order

Every per-voice step below goes through the voices in channel order: song voice 4 (channel 0) first, voice 1 (channel 3) last. "The per-tick write" means writing each channel's period, loop start (LC) and loop length (LEN); a sample's loop takes effect when Paula finishes its current block.

**V.2 and SoundMon 1:**
1. The arpeggio counter steps 3, 2, 1, 3, … (it starts at 1, so the first tick makes it 3).
2. For each voice that has played: the synth part (ADSR, LFO, EG), then the slide is added to the period.
3. The per-tick write. A synth with an LFO gets its period from the LFO instead, only on the LFO's ticks.
4. The arpeggio, for each voice with one: the period of the note plus the counter's offset (counter 2 gives the note, 1 the low nibble's offset, 3 the high nibble's). The period variable isn't changed, so an arpeggio hides the LFO and the slide. This applies even to a voice that has never played (from note 0).
5. The tick counter steps. When it runs out:
   - the row is read, voice by voice;
   - a note turns DMA off, then writes the period, LC, LEN and volume (a sample writes a volume of $FF first, then its own);
   - a looped sample also writes its loop's LC and LEN at once, so Paula starts at the loop and the part before it isn't heard;
   - option 7 changes the step at once;
   - then DMA goes on for all the notes;
   - the counter reloads with the speed (0 = 256).

**V.3:**
1. The arpeggio counter steps 0, 3, 2, 1, 0, … (it starts at 1).
2. For each voice that has played, the slide is added to the period variable.
3. The per-tick write, with the period variable.
4. The vibrato, for each voice with one: it writes period + vibrato, unless the voice is a synth with an LFO.
5. The arpeggio: counter 1 gives the low nibble's offset, 0 the high nibble's, 3 and 2 the note. The result is stored in the period variable, so it replaces the slide; a stopped arpeggio leaves the period where it was, and the next tick's write repeats it.
6. The synth part for each synth voice that has played: ADSR, LFO, EG, FX and Mod. It runs before the row, so on a note's tick it carries on with the old note's state, and anything the row changes (options 1, 9, B, D and E, a new note) is taken up on the next tick.
7. The tick counter steps, and the row is read as in V.2, except that:
   - a looped sample starts with LEN = loop start / 2 + loop length, and moves to its loop on the next tick;
   - option 7 takes effect when the next row is read;
   - the synth notes' waveform copies are taken once the whole row has been read (§7.1).

---

## 9. Subsongs, ends and loops

Songs have no end marker: when the last step ends, play goes back to step 0, and option 7 can send it anywhere. A player has to work out for itself where a song repeats.

**Loops.** A song repeats from the first row that's read a second time in the same state. That state is the step and row, the speed, the LED filter and, for V.2 and SoundMon 1, the repeat counter, but only while it can still change what plays: that is, while the next repeat option ahead (going on through the rows) is option 7, which reads it, rather than option 6, which sets it. Without that last condition, a song such as *Alien Breed*'s long menu theme, which sets the counter again before using it, wouldn't be seen to repeat from its start. Without the speed, a subsong that starts before the song's own speed setting would be matched a row early.

**Subsongs.** A file can hold several songs, each a run of steps that loops on itself, usually split off by option 7. One way to find them, used by SoundMonReplay: play from step 0 until the song repeats, noting the steps it reaches; then every step not reached so far starts another subsong, played the same way, if it plays any notes.

---

## 10. Replayer builds

Each game or disk carried its own copy of the replayer. The builds compared play the same version identically:
- **V.2:** *Plexu* (1990), *Alien Breed* (1991), Allister Brimble's music disk, the SoundMon example-song disk, *Supersonix*, and the *Green Beret* and *Commando* intro players.
- **V.3:** the *Taipan* music disk (*OrchDelight*), and the intros of *Jeah2* and *Dark Angel 2*.
- **SoundMon 1:** the *Music Mania* disk.

The differences found:
- ***Alien Breed*'s build** clamps the volume to 64; the others don't. Paula plays any value of 64 or more at full volume, so it can't be heard.
- **The *Commando* player** calls the synth part as a separate routine after the rest, so its synth effects come one tick earlier. Its samples play identically. This is how that program calls the routines, not a different replayer.
- ***Music Mania*'s SoundMon 1 build** has a shorter period table (§5).
- **Game variants:** *Aztec Challenge* plays V.3 on three channels, leaving channel 3 (song voice 1) to the game, and *ProjectX* plays V.2 on one. Neither is a reference for the format.

---

## 11. The songs in modland

The collection has 336 files: 1 `BPSM`, 214 `V.2` and 121 `V.3`.

- **File ends:** with the layout of §3, 332 files end exactly where their last sample ends. Four have trailing bytes: *jeah2* (2), *hawkeye* (224), *boeskool* (2,140) and *labello* (11,776).
- **Loops past the end:** three samples (in *1st division manager*, *karate* and *power please*) loop past their end even counting in bytes. All three loop the whole sample from an offset of 2 or 4.
- **Volumes:** three V.2 files have sample volumes above 64.
- **Notes:** V.3 and SoundMon 1 notes are all 0–36. A handful of V.2 notes lie outside, from −109 to 113, probably in unused or damaged tracks.
- **Instruments beyond 15:** two V.2 files (*chris*, *nr.1*) push an instrument number past 15 with a sound transpose, so the original plays an instrument from whatever memory follows its table (§3.2).

---

## 12. How this was measured

**Sources.** The work started from the two format descriptions in modland's documents (§14) and a survey of the modland files. Everything else was measured.

**Method.** The original replayers were run in a 68000 emulator from savestates of the games and disks above, once per tick, with every Paula write logged. They were run, not disassembled: the only things read from their memory were data tables, such as the period table, and the variables the replayer keeps. No other player's source was read. Test songs that use one feature at a time established each behaviour, and whole songs were then compared with SoundMonReplay tick by tick:
- **Paula's registers:** per channel, DMA on and off, and the final LC, LEN, PER and VOL of each tick;
- **the waveform area:** its contents after each tick, which shows every byte the synth effects rewrite.

`tests/compare.py` makes these comparisons, using the output of `smontrace`. The savestates hold the games themselves, so they and the test rig aren't distributed, and the comparisons can't be rerun without them. `tests/featuresongs.py` writes the feature test songs.

**What the references can see.** The emulator runs only the replayer, so a match there means SoundMonReplay sends Paula exactly what the original does, and rewrites the waveforms exactly as it does. It says nothing about the sound, which also depends on what Paula does with the writes. For that there are **WinUAE recordings** of the songs playing in the editor or on their disks, with no filter and no interpolation. A plain subtraction of a recording from a render leaves a large residual, because the emulators place a write at slightly different points within a tick (note starts differ by up to about 0.15 ms). So recordings are compared tick by tick, by each tick's level and its spectrum in third-octave bands from 25 Hz to 5 kHz, after lining them up every second.

**Results.**

| Reference | Result |
|---|---|
| Feature test songs | All exact, registers and waveforms, on each version |
| The modland collection | Every file exact over 2,000 ticks (40 seconds), registers and waveforms, on the *Plexu* (V.2), *Taipan* (V.3) and *Music Mania* (SoundMon 1) builds, apart from *chris* and *nr.1* (§11). The three V.2 songs too big for *Plexu*'s memory beside the replayer were run from slow RAM, where the replayer's writes are the same |
| Other builds | *Jeah2* and *Dark Angel 2* exact on their own songs; the V.2 builds identical on 40 random songs from a fresh start |
| *Music Mania*'s eight songs, from its memory | Exact, apart from one note in *The Short One* (§5) |
| The SoundMon 1.1 editor in WinUAE | Its register writes match on 1,300 of 1,300 ticks of *Pulsarmix*. Recordings of its 13 example songs and of three test songs agree in level, and the test songs in every tick's pitch and volume. Five of the example songs compared in full: level within 0.06 dB, median band difference 0.4–1.6 dB |
| Recording of *Alien Breed*'s menu (V.2, in the game) | Level within 0.01 dB, median band difference 0.3 dB, spectra flat within ±0.1 dB, until the game itself fades the music out |
| Recordings of *OrchDelight* (V.3) | All seven songs, each from its start: overall level within 0.04 dB, median tick level within 0.6 dB, median band difference 0.1–1.6 dB, spectra flat within ±0.1 dB on average |
| Recordings of *Music Mania* (SoundMon 1) | Three songs: level within 0.01 dB, median band difference 0.13–0.42 dB |

**Listening.** Whether the remaining differences can be heard was checked with a blind test: for each of six songs (*Future II* and *Blockbrain* from *OrchDelight*, *Alien Breed*'s menu, and the editor's *Greenberet*, *Newdimention* and *Pulsarmix*), one file made of random 3–9 second stretches taken in turn from the WinUAE recording and from SoundMonReplay, each stretch lined up with the recording and joined with a 20 ms crossfade. No transition could be heard at any of the 129 joins.

---

## 13. Open questions

- **V.2 synth byte 14,** and **byte 19's values 1–7** (§3.1). No file uses them with the EG on.
- **The *Commando* player's synth periods:** a few still differ once its one-tick shift is allowed for.
- **Notes and instruments out of range:** a transpose taking a note past −35 or 48, an instrument number past 15, and *Music Mania*'s notes above 36. In each case the original reads whatever memory follows a table, so only the build's own memory can answer.
- **Two voices with option 2 or 7 on the same row:** the later voice in channel order wins in the one V.3 case tested, but this hasn't been traced fully.
- **Mod played once:** what happens at the end of its table.

---

## 14. Earlier descriptions

| Source | Description |
|---|---|
| Clevy | Laurent Clevy, *SoundMon 1.1* (2000), approved by Brian Postma |
| Asle | Asle, *Sound-Monitor 2.0/3.0*, with Brian Postma's comments (2000) |

Both are in modland's documents folder. They were read as descriptions of the format, and the rest was measured.

**Where the measurements settled a question.**

| Question | Measured answer |
|---|---|
| Loop start | In bytes, not words |
| The V.2 synth layout | Not described by either: Clevy's table is the V.3 layout and doesn't fit his own V.2 example. §3.1 has the measured layout |
| Step bytes 2 and 3 | Byte 2 is the sound transpose (added to the instrument number) and byte 3 the transpose (added to the note). The two documents disagree, and Asle's description and Brian Postma's comment on it contradict each other |
| Offset 30 | The number of steps, not of patterns |
| The ADSR, LFO, EG, FX and Mod | The documents name the fields; how each behaves, and the order they run in (§7, §8), was measured |

---

This documentation was generated using AI development tools (Claude Code by Anthropic).
