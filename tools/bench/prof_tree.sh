#!/usr/bin/env bash
# prof_tree.sh NAME [bench.py options...]: the Shrine with whole-stack sampling of the GPU threads
# (PROF_THREADS, default the draw thread, the recorder and the command processor), then a call
# tree per thread in <bench dir>/logs/tree_NAME.txt (tools/profile_tree.py). The exe the stacks
# belong to is kept next to them: symbols only match that build.
set -euo pipefail
name=$1; shift
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
work=${SOTC_BENCH_DIR:-$repo/tools/bench/work}
port=${SOTC_BENCH_PORT:-$repo/dist/SotC-PC}
python=${PYTHON:-python}
mkdir -p "$work/logs"
cp -f "$repo/out/gpu/bb-probe.exe" "$work/logs/prof_$name.exe"
"$python" "$repo/tools/bench/bench.py" "$name" --scenario shrine --seconds 100 --idle 40 \
    --env "SOTC_PROFILE=${PROF_THREADS:-GpuDraw|GpuRecorder|GpuCommandProcessor}" \
    --env SOTC_PROFILE_DELAY=58 --env SOTC_PROFILE_SECONDS=25 \
    "--env=SOTC_PROFILE_STACKS=$(cygpath -w "$work/logs/stacks_$name.txt" 2>/dev/null || echo "$work/logs/stacks_$name.txt")" \
    "$@" > "$work/logs/summary_$name.txt" 2>&1 || true
tail -3 "$work/logs/summary_$name.txt"
"$python" "$repo/tools/profile_tree.py" "$work/logs/stacks_$name.txt" "$work/logs/prof_$name.exe" \
    --top 120 > "$work/logs/tree_$name.txt"
echo "tree: $work/logs/tree_$name.txt (bench port: $port)"
