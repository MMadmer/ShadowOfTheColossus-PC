#!/usr/bin/env python3
"""sotc: call trees from a SOTC_PROFILE_STACKS dump (see src/probe.c):
profile_tree.py stacks.txt [exe] [--top N] [--focus NAME]... [--depth N]
Per sampled thread: inclusive and self time by function (inline frames expanded; return
addresses are symbolized at address - 1, the call instruction, so a call is charged to the inline
scope that makes it), and for every --focus function (substring of the name) its callees and
callers by inclusive time."""
import argparse
import collections
import os
import subprocess

ap = argparse.ArgumentParser()
ap.add_argument('stacks')
ap.add_argument('exe', nargs='?', default='out/gpu/bb-probe.exe')
ap.add_argument('--top', type=int, default=40)
ap.add_argument('--focus', action='append', default=[])
ap.add_argument('--depth', type=int, default=1, help='callee levels shown under a focus')
ap.add_argument('--up', type=int, default=1, help='caller levels shown above a focus')
ap.add_argument('--symbolizer', default=os.environ.get('LLVM_SYMBOLIZER', 'llvm-symbolizer'))
args = ap.parse_args()

base = 0
names = {}
samples = []  # (thread, [addresses leaf first])
for line in open(args.stacks, encoding='utf-8', errors='replace'):
    parts = line.split()
    if not parts:
        continue
    if parts[0] == 'base':
        base = int(parts[1], 16)
    elif parts[0] == 'thread':
        names[int(parts[1])] = ' '.join(parts[2:])
    else:
        samples.append((int(parts[0]), [int(p, 16) for p in parts[1:]]))

# Exe addresses only (module offsets below 64 MiB); a call site is the byte before its return.
wanted = set()
for _thread, frames in samples:
    for i, a in enumerate(frames):
        if 0 <= a - base < 0x4000000:
            wanted.add(a - base - (1 if i else 0))
order = sorted(wanted)
chains = {}
for i in range(0, len(order), 2000):
    chunk = order[i:i + 2000]
    out = subprocess.run([args.symbolizer, f'--obj={args.exe}', '--functions=short', '--inlines', '--demangle'] +
                         [hex(0x140000000 + o) for o in chunk],
                         capture_output=True, text=True).stdout.strip().split('\n\n')
    for offset, block in zip(chunk, out):
        lines = block.split('\n')
        chain = [lines[j] for j in range(0, len(lines), 2)]
        chains[offset] = list(reversed(chain)) or ['??']  # outermost first


def expand(frames):
    """Function names root first, inline scopes expanded; other modules as one frame each."""
    path = []
    for i in reversed(range(len(frames))):
        a = frames[i]
        if 0 <= a - base < 0x4000000:
            path.extend(chains.get(a - base - (1 if i else 0), ['??']))
        else:
            path.append('<system>')
    return path


by_thread = collections.defaultdict(list)
for thread, frames in samples:
    by_thread[thread].append(expand(frames))

for thread, paths in sorted(by_thread.items()):
    total = len(paths)
    incl = collections.Counter()
    self_time = collections.Counter()
    for path in paths:
        for name in set(path):
            incl[name] += 1
        self_time[path[-1]] += 1
    print(f'== {names.get(thread, thread)}: {total} samples')
    print('-- inclusive')
    for name, n in incl.most_common(args.top):
        print(f'  {100.0 * n / total:5.1f}%  {name[:110]}')
    print('-- self')
    for name, n in self_time.most_common(args.top // 2):
        print(f'  {100.0 * n / total:5.1f}%  {name[:110]}')
    # System leaves by the innermost own scope that called into them (driver, kernel, waits).
    system = collections.Counter()
    for path in paths:
        if path[-1] == '<system>':
            own = [n for n in path if n != '<system>']
            system[' > '.join(own[-3:]) if own else '<none>'] += 1
    print('-- system leaf, by caller')
    for name, n in system.most_common(args.top // 2):
        print(f'  {100.0 * n / total:5.1f}%  {name[:160]}')
    for focus in args.focus:
        callees = collections.Counter()
        callers = collections.Counter()
        hits = 0
        for path in paths:
            idx = [i for i, n in enumerate(path) if focus in n]
            if not idx:
                continue
            hits += 1
            first, last = idx[0], idx[-1]
            callers[' < '.join(reversed(path[max(0, first - args.up):first])) or '<root>'] += 1
            below = path[last + 1:last + 1 + args.depth]
            callees[' > '.join(below) if below else '<self>'] += 1
        if not hits:
            continue
        print(f'-- {focus}: {100.0 * hits / total:.1f}%')
        for name, n in callees.most_common(args.top):
            print(f'    {100.0 * n / total:5.1f}%  > {name[:150]}')
        print('   callers:')
        for name, n in callers.most_common(8):
            print(f'    {100.0 * n / total:5.1f}%  < {name[:200]}')
    print()
