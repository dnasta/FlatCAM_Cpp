#!/usr/bin/env bash
# Runs the C++ unit tests and checks the parsers against the original
# FlatCAM 2024.4 Python implementation (via tools/refenv).
set -euo pipefail
cd "$(dirname "$0")/.."

cmake --build build -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" >/dev/null
./build/flatcam_tests

PY=tools/refenv/bin/python
if [ ! -x "$PY" ]; then
  echo "reference environment missing: create it with"
  echo "  python3 -m venv tools/refenv && tools/refenv/bin/pip install shapely numpy"
  exit 1
fi

for f in tests/data/*.gbr; do
  echo
  "$PY" tools/compare_gerber.py "$f"
  echo
  "$PY" tools/compare_isolation.py "$f" --tool-dia=0.2 --passes=1
done

echo
"$PY" tools/compare_isolation.py tests/data/sample.gbr --tool-dia=0.3 --passes=2 --overlap=10

for f in tests/data/*.gbr; do
  echo
  "$PY" tools/compare_cutout.py "$f" --tool-dia=1.0 --margin=0.5
done

echo
"$PY" tools/compare_cutout.py tests/data/sample.gbr --tool-dia=2.0 --margin=0.5 --internal

for f in tests/data/*.drl; do
  echo
  "$PY" tools/compare_excellon.py "$f"
done

echo
"$PY" tools/compare_gcode.py tests/data/drill_only.drl

# Demo board
echo
"$PY" tools/compare_gerber.py examples/board_copper.gbr
echo
"$PY" tools/compare_gerber.py examples/board_outline.gbr
echo
"$PY" tools/compare_excellon.py examples/board.drl
echo
"$PY" tools/compare_isolation.py examples/board_copper.gbr --tool-dia=0.2 --passes=2 --overlap=10
echo
"$PY" tools/compare_cutout.py examples/board_outline.gbr --tool-dia=2.0 --margin=0.5
echo
"$PY" tools/compare_gcode.py examples/board.drl
