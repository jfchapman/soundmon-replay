"""Writes small test songs, each using one feature (an option or a synth effect), as V.2 and V.3
files, for tests/compare.py.

Usage: python tests/featuresongs.py <folder>
Files are named <feature>-V2.bp / -V3.bp (FX and Mod are V.3 only).
"""
import os
import struct
import sys

V3_FIELDS = dict(adsr_ctl=4, adsr_table=5, adsr_len=(6, 2), adsr_speed=8, lfo_ctl=9, lfo_table=10, lfo_depth=11,
                 lfo_len=(12, 2), lfo_delay=14, lfo_speed=15, eg_ctl=16, eg_table=17, eg_len=(18, 2), eg_delay=20,
                 eg_speed=21, fx=22, fx_speed=23, fx_delay=24, mod_ctl=25, mod_table=26, mod_speed=27, mod_delay=28,
                 volume=29, mod_len=(30, 2))
V2_FIELDS = dict(adsr_ctl=4, adsr_table=5, adsr_len=(6, 2), adsr_speed=8, lfo_ctl=9, lfo_table=10, lfo_depth=11,
                 lfo_len=(12, 2), lfo_delay=(14, 2), lfo_speed=16, eg_ctl=17, eg_table=18, eg_len=(20, 2),
                 eg_delay=(22, 2), eg_speed=24, volume=25)


def synth(tag, wave=0, words=16, volume=64, **fields):
    layout = V3_FIELDS if tag == b'V.3' else V2_FIELDS
    b = bytearray(32)
    b[0], b[1] = 0xFF, wave
    struct.pack_into('>H', b, 2, words)
    fields['volume'] = volume
    for name, v in fields.items():
        if name not in layout:  # V.3-only fields (FX, Mod) on a V.2 instrument
            continue
        at = layout[name]
        if isinstance(at, tuple):
            struct.pack_into('>H', b, at[0], v)
        else:
            b[at] = v & 0xFF
    return bytes(b)


def sample(words, loop_bytes, loop_words, volume=64):
    return b'S'.ljust(24, b'\0') + struct.pack('>HHHH', words, loop_bytes, loop_words, volume)


def track(rows):
    b = bytearray(48)
    for r, (n, i, o, d) in rows.items():
        b[3 * r:3 * r + 3] = bytes([n & 255, (i << 4) | o, d])
    return b


RAMP = bytes(i * 4 for i in range(64))
PATTERN = bytes([0, 100, 0, 100, 20, 60, 0x80, 0x90, 0, 100, 0, 100, 0, 100, 0, 100] * 4)
WAVES = PATTERN + RAMP + bytes([50] * 64) + bytes(range(0, 256, 4))


def song(steps, tracks, tag, instruments):
    """Instrument n defaults to a silent sample of 8n words; instruments overrides some."""
    h = b'TEST'.ljust(26, b'\0') + tag + bytes([len(WAVES) // 64]) + struct.pack('>H', len(steps))
    ins, smp = b'', b''
    for i in range(1, 16):
        if i in instruments:
            data = instruments[i]
        else:
            data = sample(8 * i, 0, 1)
        ins += data
        if data[0] != 0xFF:
            smp += bytes(2 * struct.unpack_from('>H', data, 24)[0])
    st = b''.join(struct.pack('>Hbb', *v) for s in steps for v in s)
    return h + ins + st + b''.join(bytes(t) for t in tracks) + WAVES + smp


EMPTY = (20, 0, 0)


def write_all(folder):
    os.makedirs(folder, exist_ok=True)
    count = 0

    def save(name, rows=None, tags=(b'V.2', b'V.3'), ins=None, steps=None, tracks=None):
        nonlocal count
        for tag in tags:
            tr = tracks or [track(rows)]
            st = steps or [[(1, 0, 0), EMPTY, EMPTY, EMPTY]]
            tr = tr + [bytearray(48)] * (20 - len(tr))
            data = song(st, tr, tag, ins(tag) if ins else {})
            with open(os.path.join(folder, '%s-%s.bp' % (name, tag.decode().replace('.', ''))), 'wb') as f:
                f.write(data)
            count += 1

    samples = lambda t: {1: sample(32, 16, 8, 50), 2: sample(16, 0, 1, 40), 3: sample(32, 0, 32, 64)}
    lead = lambda t: {4: synth(t, 0, 16, 50, adsr_ctl=1, adsr_table=1, adsr_len=64, adsr_speed=1, lfo_ctl=2,
                               lfo_table=3, lfo_depth=8, lfo_len=32, lfo_speed=1, eg_ctl=1, eg_table=1, eg_len=32,
                               eg_speed=2, fx=2, fx_speed=1)}
    save('notes', {0: (13, 1, 0, 0), 2: (25, 2, 0, 0), 4: (13, 3, 0, 0), 6: (1, 0, 0, 0), 8: (13, 1, 0, 0),
                   9: (13, 1, 0, 0), 10: (20, 3, 0, 0), 11: (20, 3, 0, 0), 12: (13, 2, 0, 0), 13: (15, 0, 0, 0),
                   14: (13, 1, 0, 0), 15: (15, 0, 0, 0)}, ins=samples)
    for o, d in ((0, 0x37), (1, 20), (2, 2), (3, 1), (4, 5), (5, 5), (6, 0x12), (8, 3), (9, 0x47), (10, 0x11),
                 (11, 1), (13, 0x25), (14, 0x25), (15, 0x25)):
        save('opt%X-note' % o, {0: (13, 1, o, d), 4: (0, 0, 0, 0), 6: (25, 1, o, d), 9: (13, 3, 0, 0)}, ins=samples)
        save('opt%X-nonote' % o, {0: (13, 1, 0, 0), 2: (0, 0, o, d), 5: (0, 0, o, d), 8: (13, 3, 0, 0),
                                  10: (0, 0, o, d)}, ins=samples)
        save('opt%X-synth' % o, {0: (13, 4, o, d), 3: (0, 0, o, d), 6: (25, 4, 0, 0), 8: (20, 4, o, d)}, ins=lead)
    save('opt0-keep', {0: (0, 0, 0, 0x37), 1: (0, 0, 1, 30), 2: (13, 1, 1, 40), 3: (0, 0, 2, 6), 4: (0, 0, 0, 0),
                       5: (13, 1, 0, 0x25), 6: (0, 0, 9, 0x47), 7: (0, 0, 0, 0x37), 8: (0, 0, 0, 0), 9: (0, 0, 1, 50),
                       10: (13, 1, 8, 1), 11: (0, 0, 0, 0x12), 12: (25, 1, 5, 1), 13: (0, 0, 0, 0)}, ins=samples)
    plain = lambda t: {4: synth(t, 0, 16, 50)}
    save('opt6-plainsynth', {0: (13, 4, 6, 0x12), 4: (0, 0, 6, 0x24), 8: (20, 4, 0, 0), 10: (0, 0, 6, 0x08)}, ins=plain)
    save('opt8-plainsynth', {0: (13, 4, 8, 2), 4: (0, 0, 8, 0xFE), 8: (20, 4, 0, 0)}, ins=plain)
    four = [[(1 + s, 0, 0), EMPTY, EMPTY, EMPTY] for s in range(4)]
    save('jump', steps=four, ins=samples,
         tracks=[track({0: (13 + s, 1, 0, 0), 8: (0, 0, 7, 2) if s == 3 else (0, 0, 0, 0)}) for s in range(4)])
    save('repeat', steps=four, ins=samples,
         tracks=[track({0: (13 + s, 1, 6 if s == 0 else 0, 3), 15: (0, 0, 7, 1) if s == 2 else (0, 0, 0, 0)})
                 for s in range(4)])
    effects = (('adsr', dict(adsr_ctl=1, adsr_table=1, adsr_len=64, adsr_speed=2)),
               ('adsr-rep', dict(adsr_ctl=2, adsr_table=1, adsr_len=16, adsr_speed=1)),
               ('lfo', dict(lfo_ctl=1, lfo_table=3, lfo_depth=4, lfo_len=16, lfo_speed=2, lfo_delay=3)),
               ('lfo-rep', dict(lfo_ctl=2, lfo_table=3, lfo_depth=0, lfo_len=8, lfo_speed=1)),
               ('eg', dict(eg_ctl=1, eg_table=1, eg_len=32, eg_speed=1, eg_delay=2)),
               ('eg-rep', dict(eg_ctl=2, eg_table=3, eg_len=16, eg_speed=3)))
    for name, kw in effects:
        save('syn-' + name, {0: (13, 4, 0, 0), 5: (20, 4, 0, 0), 9: (20, 2, 0, 0), 11: (13, 4, 0, 0)},
             ins=lambda t, kw=kw: {4: synth(t, 0, 16, 50, **kw), 2: sample(16, 0, 1, 40)})
    for fx in (1, 2, 3, 4, 5):
        save('fx%d' % fx, {0: (13, 4, 0, 0), 10: (20, 4, 0, 0)}, tags=(b'V.3',),
             ins=lambda t, fx=fx: {4: synth(t, 0, 16, 50, fx=fx, fx_speed=2)})
        save('fx%d-swap' % fx, {0: (13, 4, 0, 0), 6: (0, 0, 13, 0), 10: (20, 4, 0, 0)}, tags=(b'V.3',),
             ins=lambda t, fx=fx: {4: synth(t, 0, 16, 50, fx=fx, fx_speed=2)})
    save('fx-type', {0: (13, 4, 0, 0), 4: (0, 0, 11, 1), 8: (0, 0, 11, 0), 10: (20, 4, 11, 4)}, tags=(b'V.3',),
         ins=lambda t: {4: synth(t, 0, 16, 50, fx=2, fx_speed=1)})
    save('mod', {0: (13, 4, 0, 0), 8: (20, 4, 0, 0)}, tags=(b'V.3',),
         ins=lambda t: {4: synth(t, 0, 16, 50, mod_ctl=2, mod_table=1, mod_len=8, mod_speed=1, mod_delay=1)})
    return count


if __name__ == '__main__':
    print('%d songs written' % write_all(sys.argv[1]))
