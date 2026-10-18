#!/usr/bin/env bash
# final_tests.sh: the short check after a batch of work, on a 4-core / 2 GB card emulation
# (affinity to 4 cores without SMT, SOTC_VRAM_LIMIT_MB=2048 budget): the Shrine at 1080p with a
# screenshot, and colossus 13 (boss jump, damage off, cinematic skipped, running and turning the
# camera) at 810p. Results: <bench dir>/logs/summary_fin_*.txt, gshot_fin_*.png.
set -uo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
python=${PYTHON:-python}
emulate=(--cores 4 --vram 2048 --env SOTC_THREAD_STATS=5)
"$python" "$repo/tools/bench/bench.py" fin_shrine --scenario shrine --seconds 110 --idle 40 \
    --modes af16 --gshot 15 --smi "${emulate[@]}"
"$python" "$repo/tools/bench/bench.py" fin_boss --boss 13 --seconds 150 --idle 80 \
    --modes af16,res810 --gshot 6 --smi "${emulate[@]}"
