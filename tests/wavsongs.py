"""Writes V.2 test songs for recording in WinUAE from the SoundMon 1.1 editor, to check what the
per-tick comparison with the original replayers can't: how Paula sounds what the replayer writes.

Usage: python tests/wavsongs.py <folder>

Every test plays on song voice 1 (Paula channel 3, the left side) only, so a recording fits one
voice. Each section starts on a step boundary (16 rows at speed 6 = 1.92 s) and is described in
the printed plan.

  smtest1.bp  samples: the attack of a looped sample, repeated notes on a looped instrument,
              volumes, and pitches
  smtest2.bp  synths: a fast LFO (depth 0, as Alien Breed's menu synth), a gentle LFO, ADSR,
              and the EG rewriting the waveform as it plays
  smtest3.bp  option 3, the filter, switched on and off under a bright held note
"""
import math
import os
import struct
import sys

V2_FIELDS = dict(adsr_ctl=4, adsr_table=5, adsr_len=(6, 2), adsr_speed=8, lfo_ctl=9, lfo_table=10, lfo_depth=11,
                 lfo_len=(12, 2), lfo_delay=(14, 2), lfo_speed=16, eg_ctl=17, eg_table=18, eg_len=(20, 2),
                 eg_delay=(22, 2), eg_speed=24, volume=25)


def synth(wave, words, volume, **fields):
    b = bytearray(32)
    b[0], b[1] = 0xFF, wave
    struct.pack_into('>H', b, 2, words)
    fields['volume'] = volume
    for name, v in fields.items():
        at = V2_FIELDS[name]
        if isinstance(at, tuple):
            struct.pack_into('>H', b, at[0], v)
        else:
            b[at] = v & 0xFF
    return bytes(b)


def s8(x):
    return int(max(-127, min(127, round(x)))) & 0xFF


def square(n, period, amp):
    return bytes(s8(amp if (i % period) < period // 2 else -amp) for i in range(n))


def sine(n, period, amp):
    return bytes(s8(amp * math.sin(2 * math.pi * i / period)) for i in range(n))


def sample(name, data, loop_bytes, loop_words, volume=64):
    return {'name': name, 'data': data, 'loop_bytes': loop_bytes, 'loop_words': loop_words, 'volume': volume}


def track(rows):
    b = bytearray(48)
    for r, (n, i, o, d) in rows.items():
        b[3 * r:3 * r + 3] = bytes([n & 255, (i << 4) | o, d])
    return b


def song(title, sections, instruments, waves=bytes(64)):
    """sections: one track (dict of rows) per step, on voice 1. Voices 2-4 play an empty track. A song
    without synths still carries one (empty) waveform, as the editor's files do."""
    tracks = [track(rows) for rows in sections] + [track({})]
    empty = len(tracks)
    steps = [[(s + 1, 0, 0), (empty, 0, 0), (empty, 0, 0), (empty, 0, 0)] for s in range(len(sections))]
    h = title.encode('latin-1').ljust(26, b'\0') + b'V.2' + bytes([len(waves) // 64]) + struct.pack('>H', len(steps))
    ins, data = b'', b''
    for i in range(1, 16):
        x = instruments.get(i)
        if isinstance(x, bytes):  # synth
            ins += x
            continue
        if x is None:
            x = sample('EMPTY', b'', 0, 1, 64)  # as the editor writes an unused slot (never played)
        ins += x['name'].encode('latin-1').ljust(24, b'\0') + struct.pack(
            '>HHHH', len(x['data']) // 2, x['loop_bytes'], x['loop_words'], x['volume'])
        data += x['data']
    st = b''.join(struct.pack('>Hbb', *v) for s in steps for v in s)
    return h + ins + st + b''.join(bytes(t) for t in tracks) + waves + data


C1, C2, C3 = 1, 13, 25
OFF = (0, 0, 1, 0)  # option 1, volume 0: silences the voice without a note


def test1():
    # The sound: an attack of 1,024 bytes of square wave (bright), then a 512-byte loop of sine
    # (soft). Played from its start, the attack lasts 1,024 / (3,546,895 / 428) = 0.12 s at C-2.
    attack = square(1024, 16, 110) + sine(512, 64, 70)
    plan = []
    ins = {
        1: sample('LOOPS AT 1024', attack, 1024, 256),  # V.2: Paula is pointed at the loop before DMA starts
        2: sample('ONE SHOT', attack, 0, 1),           # the same sound without a loop: plays from its start
        3: sample('LOOPS WHOLE', attack, 0, 768),      # looped from the start
        4: sample('SINE LOOP', sine(512, 64, 100), 0, 256),
    }
    s = []
    s.append({0: (C2, 1, 0, 0), 6: OFF, 8: (C2, 2, 0, 0), 14: OFF})
    plan.append('step 0: a looped sample whose loop starts at byte 1024, then the same sound as a one-shot. '
                'Question: does the looped one start with its bright attack, or straight on the soft loop?')
    s.append({0: (C2, 3, 0, 0), 6: OFF, 8: (C1, 1, 0, 0), 14: OFF})
    plan.append('step 1: the sound looped from its start; then the loop-at-1024 sample an octave lower')
    s.append({0: (C2, 1, 0, 0), 2: (C3, 1, 0, 0), 4: (C2, 1, 0, 0), 6: (C3, 1, 0, 0), 8: (C2, 2, 0, 0), 10: (C3, 1, 0, 0),
              12: (C2, 2, 0, 0), 14: OFF})
    plan.append('step 2: repeated notes on the loop-at-1024 instrument, 2 rows apart: no DMA restart (only the '
                'period changes), then alternating with the one-shot (which restarts)')
    s.append({0: (C2, 4, 1, 64), 3: (C2, 4, 1, 48), 6: (C2, 4, 1, 32), 9: (C2, 4, 1, 16), 12: (C2, 4, 1, 8), 15: OFF})
    plan.append('step 3: a sine at volumes 64, 48, 32, 16 and 8 (option 1 on each note)')
    s.append({r: (1 + 2 * r, 4, 0, 0) for r in range(15)} | {15: OFF})
    plan.append('step 4: the sine at notes 1, 3, 5 ... 29, a row each: the period table')
    return song('SMTEST1 SAMPLES', s, ins), plan


def test2():
    # Waveform 0: a 32-byte sine (the synths' sound). Waveform 1: a table of large signed values (the
    # fast LFO). Waveform 2: a ramp 0, 4, ..., 252 (ADSR and EG tables). Waveform 3: a gentle LFO.
    w0 = sine(64, 32, 100)
    w1 = bytes(s8(v) for v in [0, 90, -60, 120, -110, 40, -20, 100, -90, 70, -40, 110, -120, 20, -80, 60] * 4)
    w2 = bytes(i * 4 for i in range(64))
    w3 = sine(64, 64, 120)
    waves = w0 + w1 + w2 + w3
    ins = {
        1: synth(0, 16, 40, lfo_ctl=2, lfo_table=1, lfo_depth=0, lfo_len=16, lfo_speed=1),  # as Alien Breed's menu synth
        2: synth(0, 16, 40, lfo_ctl=2, lfo_table=3, lfo_depth=8, lfo_len=64, lfo_speed=1),  # a gentle vibrato
        3: synth(0, 16, 64, adsr_ctl=1, adsr_table=2, adsr_len=64, adsr_speed=1),           # a volume ramp
        4: synth(0, 16, 50, eg_ctl=1, eg_table=2, eg_len=64, eg_speed=2),                   # EG: the waveform negated more and more
        5: synth(0, 16, 50),                                                                # the plain synth
    }
    plan = []
    s = []
    s.append({0: (C2, 5, 0, 0), 6: OFF, 8: (C2, 1, 0, 0), 15: OFF})
    plan.append('step 0: the plain synth, then the fast LFO (depth 0: the period jumps by up to 120 every tick, '
                'as in Alien Breed\'s menu synth)')
    s.append({0: (C1, 1, 0, 0), 8: (C3, 1, 0, 0), 15: OFF})
    plan.append('step 1: the fast LFO an octave lower, then an octave higher')
    s.append({0: (C2, 2, 0, 0), 15: OFF})
    plan.append('step 2: the gentle LFO')
    s.append({0: (C2, 3, 0, 0), 15: OFF})
    plan.append('step 3: the ADSR volume ramp (32 to 63 and round again, at full volume)')
    s.append({0: (C2, 4, 0, 0), 15: OFF})
    plan.append('step 4: the EG negating the waveform\'s first bytes more and more as it plays')
    return song('SMTEST2 SYNTHS', s, ins, waves), plan


def test3():
    bright = square(512, 8, 100)
    ins = {1: sample('BRIGHT', bright, 0, 256)}
    s = [{0: (C2, 1, 3, 0), 4: (0, 0, 3, 1), 8: (0, 0, 3, 0), 12: (0, 0, 3, 1)},
         {0: (0, 0, 3, 0), 15: OFF}]
    plan = ['step 0: a bright held note, with option 3 every 4 rows: filter off, on, off, on',
            'step 1: filter off again, then silence']
    return song('SMTEST3 FILTER', s, ins), plan


def main():
    folder = sys.argv[1]
    os.makedirs(folder, exist_ok=True)
    for name, make in (('smtest1.bp', test1), ('smtest2.bp', test2), ('smtest3.bp', test3)):
        data, plan = make()
        with open(os.path.join(folder, name), 'wb') as f:
            f.write(data)
        print('%s (%d bytes)' % (name, len(data)))
        for line in plan:
            print('  ' + line)


if __name__ == '__main__':
    main()
