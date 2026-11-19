"""Compare the library's replayer with an original one, tick by tick.

Usage: python tests/compare.py <profile> <ticks> <song.bp>... [-v] [--start N] [--writes]
           [--trace-dir DIR] [--tool smontrace.exe]

The original replayer runs in soundmon-trace's harness (a private repository; --trace-dir,
default ../soundmon-trace). Profiles: see soundmon-trace's player.py (plexu, alienbreed, taipan,
musicmania, ...). The library's replayer runs through tools/smontrace.

Each tick, Paula's resulting state is compared: per channel the period, volume (bits 0-6, 64 and
above as 64), sample pointer and length after the tick, plus each DMA on/off with the pointer and
length it starts from. --writes compares the register writes themselves, in order.
Exit status 1 if any song differs.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def parse_args(argv):
    opts = {'-v': False, '--writes': False, '--start': 0, '--trace-dir': os.path.join(os.path.dirname(ROOT), 'soundmon-trace'),
            '--tool': os.path.join(ROOT, 'build', 'x64', 'Release', 'smontrace.exe')}
    rest = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ('-v', '--writes'):
            opts[a] = True
        elif a in ('--start', '--trace-dir', '--tool'):
            opts[a] = argv[i + 1]
            i += 1
        else:
            rest.append(a)
        i += 1
    opts['--start'] = int(opts['--start'])
    return opts, rest


def reference(trace_dir, prof, song, ticks, start):
    sys.path.insert(0, trace_dir)
    from player import Player, PROFILES  # noqa: E402
    from trace import Normaliser  # noqa: E402
    p = Player(song, prof, start_step=start)
    empty = PROFILES[prof].get('empty')
    norm = Normaliser(p.base, len(song))
    out = []
    for _ in range(ticks):
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
        out.append(writes)
    return out


def library(tool, path, ticks, start):
    text = subprocess.run([tool, path, str(ticks), '--start', str(start)], capture_output=True, text=True, check=True).stdout
    out = []
    for line in text.splitlines():
        if line.startswith('tick'):
            out.append([])
        else:
            ch, reg, val = line.split()
            out[-1].append((int(ch), reg, 'EMPTY' if val == '$EMPTY' else val))
    return out


def state(ticks):
    regs = [{'PER': '0', 'VOL': 0, 'LC': '0', 'LEN': '0'} for _ in range(4)]
    out = []
    for writes in ticks:
        events = []
        for ch, reg, val in writes:
            if reg in ('DMA_ON', 'DMA_OFF'):
                events.append((ch, reg, regs[ch]['LC'], regs[ch]['LEN']))
            elif reg == 'VOL':
                regs[ch]['VOL'] = min(int(val, 16) & 0x7F, 64)
            elif reg == 'PER' or reg == 'LEN':
                regs[ch][reg] = '%X' % (int(val, 16) & 0xFFFF)
            else:
                regs[ch][reg] = val
        # The order of DMA events within a tick isn't heard; what each one starts from is.
        out.append((tuple(tuple(sorted(r.items())) for r in regs), tuple(sorted(events))))
    return out


def main():
    opts, args = parse_args(sys.argv[1:])
    prof, ticks, files = args[0], int(args[1]), args[2:]
    bad = 0
    for path in files:
        song = open(path, 'rb').read()
        name = os.path.basename(path)
        try:
            ref = reference(opts['--trace-dir'], prof, song, ticks, opts['--start'])
        except ValueError as e:
            print('%-45s skipped: %s' % (name[:45], e))
            continue
        lib = library(opts['--tool'], path, ticks, opts['--start'])
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
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
