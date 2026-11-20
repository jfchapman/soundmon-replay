"""Compare the library's replayer with an original one, tick by tick.

Usage: python tests/compare.py <profile> <ticks> <song.bp>... [-v] [--start N] [--writes] [--mem]
           [--trace-dir DIR] [--tool smontrace.exe]

The original replayer runs in soundmon-trace's harness (a private repository; --trace-dir,
default ../soundmon-trace). Profiles: see soundmon-trace's player.py (plexu, alienbreed, taipan,
musicmania, ...). The library's replayer runs through tools/smontrace.

Each tick, Paula's resulting state is compared: per channel the period, volume (bits 0-6, 64 and
above as 64), sample pointer and length after the tick, plus each DMA on/off with the pointer and
length it starts from. --writes compares the register writes themselves, in order.
--mem also compares the song's waveform area after each tick (by CRC-32), which the synth effects
(EG, V.3 FX and Mod) rewrite in memory; -v then shows the first bytes that differ.
Exit status 1 if any song differs.
"""
import os
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def parse_args(argv):
    opts = {'-v': False, '--writes': False, '--mem': False, '--start': 0, '--trace-dir': os.path.join(os.path.dirname(ROOT), 'soundmon-trace'),
            '--tool': os.path.join(ROOT, 'build', 'x64', 'Release', 'smontrace.exe')}
    rest = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ('-v', '--writes', '--mem'):
            opts[a] = True
        elif a in ('--start', '--trace-dir', '--tool'):
            opts[a] = argv[i + 1]
            i += 1
        else:
            rest.append(a)
        i += 1
    opts['--start'] = int(opts['--start'])
    return opts, rest


def wave_area(song):
    """The waveform area's offset and size in the song file, as far as the file has it."""
    steps, = struct.unpack_from('>H', song, 30)
    top = max(struct.unpack_from('>H', song, 512 + 16 * s + 4 * v)[0] for s in range(steps) for v in range(4))
    at = min(512 + steps * 16 + top * 48, len(song))
    count = 0 if song[26:30] == b'BPSM' else song[29]
    return at, min(count * 64, len(song) - at)


def reference(trace_dir, prof, song, ticks, start, mem=False, dump_tick=None):
    sys.path.insert(0, trace_dir)
    from player import Player, PROFILES  # noqa: E402
    from trace import Normaliser  # noqa: E402
    p = Player(song, prof, start_step=start)
    empty = PROFILES[prof].get('empty')
    norm = Normaliser(p.base, len(song))
    at, size = wave_area(song)
    out = []
    for t in range(ticks):
        writes = []
        for line in norm.lines(p.tick()):
            ch, reg, val = line.split()
            if reg == 'LC' and val.startswith('$'):
                addr = int(val[1:], 16)
                if addr == empty:
                    val = 'EMPTY'
                elif addr == p.base + len(song):  # an empty instrument: the end of the file
                    val = '%X' % len(song)
            writes.append((int(ch), reg, val))
        if mem:
            area = p.read(p.base + at, size)
            if t == dump_tick:
                return area
            writes.append((-1, 'MEM', '%08X' % zlib.crc32(area)))
        out.append(writes)
    return out


def library(tool, path, ticks, start, mem=False):
    args = [tool, path, str(ticks), '--start', str(start)] + (['--mem'] if mem else [])
    text = subprocess.run(args, capture_output=True, text=True, check=True).stdout
    out = []
    for line in text.splitlines():
        if line.startswith('tick'):
            out.append([])
        elif line.startswith(' mem '):
            out[-1].append((-1, 'MEM', line.split()[1]))
        else:
            ch, reg, val = line.split()
            out[-1].append((int(ch), reg, 'EMPTY' if val == '$EMPTY' else val))
    return out


def library_area(tool, path, tick, start):
    """The library's waveform area after a tick, as bytes."""
    text = subprocess.run([tool, path, str(tick + 1), '--start', str(start), '--memdump'], capture_output=True, text=True, check=True).stdout
    return bytes.fromhex([l for l in text.splitlines() if l.startswith(' mem ')][-1].split()[1])


def state(ticks):
    regs = [{'PER': '0', 'VOL': 0, 'LC': '0', 'LEN': '0'} for _ in range(4)]
    out = []
    for writes in ticks:
        events = []
        mem = None
        for ch, reg, val in writes:
            if reg == 'MEM':
                mem = val
            elif reg in ('DMA_ON', 'DMA_OFF'):
                events.append((ch, reg, regs[ch]['LC'], regs[ch]['LEN']))
            elif reg == 'VOL':
                regs[ch]['VOL'] = min(int(val, 16) & 0x7F, 64)
            elif reg == 'PER' or reg == 'LEN':
                regs[ch][reg] = '%X' % (int(val, 16) & 0xFFFF)
            else:
                regs[ch][reg] = val
        # The order of DMA events within a tick isn't heard; what each one starts from is.
        out.append((tuple(tuple(sorted(r.items())) for r in regs), tuple(sorted(events)), mem))
    return out


def main():
    opts, args = parse_args(sys.argv[1:])
    prof, ticks, files = args[0], int(args[1]), args[2:]
    bad = 0
    for path in files:
        song = open(path, 'rb').read()
        name = os.path.basename(path)
        try:
            ref = reference(opts['--trace-dir'], prof, song, ticks, opts['--start'], opts['--mem'])
        except ValueError as e:
            print('%-45s skipped: %s' % (name[:45], e))
            continue
        lib = library(opts['--tool'], path, ticks, opts['--start'], opts['--mem'])
        if not opts['--writes']:
            ref, lib = state(ref), state(lib)
        diff = [t for t in range(ticks) if ref[t] != lib[t]]
        bad += bool(diff)
        print('%-45s %5d of %d ticks differ%s' % (name[:45], len(diff), ticks, ', first %d' % diff[0] if diff else ''))
        if diff and opts['-v']:
            t = diff[0]
            if opts['--writes']:
                print('    reference %s' % ref[t])
                print('    library   %s' % lib[t])
            else:
                for ch in range(4):
                    if ref[t][0][ch] != lib[t][0][ch]:
                        print('    ch%d reference %s\n        library   %s' % (ch, dict(ref[t][0][ch]), dict(lib[t][0][ch])))
                if ref[t][1] != lib[t][1]:
                    print('    DMA reference %s\n        library   %s' % (ref[t][1], lib[t][1]))
                if ref[t][2] != lib[t][2]:
                    a = reference(opts['--trace-dir'], prof, song, ticks, opts['--start'], True, t)
                    b = library_area(opts['--tool'], path, t, opts['--start'])
                    at, size = wave_area(song)
                    off = [i for i in range(min(len(a), len(b))) if a[i] != b[i]]
                    print('    tick %d, waveform area (file offset $%X, %d bytes): %d bytes differ, first at +$%X'
                          ' (waveform %d byte %d)' % (t, at, size, len(off), off[0], off[0] // 64, off[0] % 64))
                    for i in off[:8]:
                        print('      +$%X reference %02X library %02X' % (i, a[i], b[i]))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
