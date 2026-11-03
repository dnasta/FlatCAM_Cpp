#!/usr/bin/env bash
# Verifies the port against a real KiCad/FlatCAM project:
#   gerbers + drill vs the original Python parser,
#   cutout and drilling G-code vs the production .nc files,
#   metadata of the lzma-compressed .FlatPrj.
#
# Usage:
#   BOARD_DIR="/path/to/cnc" tools/verify_real_board.sh
#
# The default path is the "Luz de Noche Pascual" board on this machine.
set -euo pipefail
cd "$(dirname "$0")/.."

BOARD_DIR="${BOARD_DIR:-/Volumes/Externo 1TB/Google Drive/dnasta/DSD/proyectos productos/luz de noche Pascual/electronica/kicad/luz de noche Pascual/cnc}"
PY=tools/refenv/bin/python
CLI=./build/flatcam-cli
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [ ! -d "$BOARD_DIR" ]; then
  echo "BOARD_DIR not found: $BOARD_DIR"
  exit 2
fi

echo "== Gerbers vs original FlatCAM parser"
for f in "$BOARD_DIR"/gerber/*.gbr; do
  "$PY" tools/compare_gerber.py "$f" | tail -1
done

echo "== Excellon vs original FlatCAM parser"
"$PY" tools/compare_excellon.py "$BOARD_DIR/gerber/luz de noche Pascual-PTH.drl" | tail -1

echo "== Cutout G-code vs production G-code"
"$CLI" cutout "$BOARD_DIR/gerber/luz de noche Pascual-Edge_Cuts.gbr" \
  --tool-dia 1.2 --margin 0 --tabs 4 --tab-width 1.8 -o "$TMP/cut.nc" 2>/dev/null
"$PY" tools/compare_nc.py --kind=cut "$TMP/cut.nc" \
  "$BOARD_DIR/gcode/luz de noche Pascual-Edge_Cuts.gbr_cutout_cnc.nc" | tail -1

echo "== Drilling G-code vs production G-code"
"$CLI" gcode "$BOARD_DIR/gerber/luz de noche Pascual-PTH.drl" --optimize \
  --z-cut -2.4 -o "$TMP/drill.nc" 2>/dev/null
"$PY" tools/compare_nc.py --kind=drill --mirror-x "$TMP/drill.nc" \
  "$BOARD_DIR/gcode/luz de noche Pascual-PTH.drl_cnc.nc" | tail -1

echo "== Project metadata (lzma)"
"$CLI" project "$BOARD_DIR/Luz de Noche Pascual.FlatPrj" --info | head -20

echo "== All objects streamed from the compressed project"
"$CLI" project "$BOARD_DIR/Luz de Noche Pascual.FlatPrj" --summarize | head -3

echo "== Single object from the compressed project"
"$CLI" project "$BOARD_DIR/Luz de Noche Pascual.FlatPrj" --object 1 | head -6
