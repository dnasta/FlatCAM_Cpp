#!/usr/bin/env bash
# Synthetic Gerber benchmark: N x N flashes of 0.5 mm.
# Usage: tools/benchmark.sh [grid_size]
set -euo pipefail
cd "$(dirname "$0")/.."

N="${1:-200}"
FILE="$(mktemp -t flatcam_bench).gbr"

python3 - "$FILE" "$N" <<'PY'
import sys
path, n = sys.argv[1], int(sys.argv[2])
lines = ["%FSLAX34Y34*%", "%MOMM*%", "%ADD10C,0.5*%", "D10*"]
for i in range(n):
    for j in range(n):
        lines.append(f"X{i*3000+5000:09d}Y{j*3000+5000:09d}D03*")
lines.append("M02*")
open(path, "w").write("\n".join(lines))
PY

echo "flashes: $((N * N))"
echo "--- C++"
/usr/bin/time -p ./build/flatcam-cli gerber "$FILE" >/dev/null

if [ -x tools/refenv/bin/python ]; then
  echo "--- FlatCAM Python"
  /usr/bin/time -p tools/refenv/bin/python tools/flatcam_reference.py "$FILE" >/dev/null
else
  echo "(reference environment not installed)"
fi

rm -f "$FILE"
