# Placa de demostración

Archivos listos para usar en los ejemplos del README:

| Archivo | Contenido |
|---|---|
| `board_outline.gbr` | Contorno de placa de 40 × 30 mm (región Gerber) |
| `board_copper.gbr` | Cobre: pads QFP, pasivos, pistas, vías, header y un plano de cobre |
| `board.drl` | Taladros Excellon: 4 vías Ø0,4, 4 montajes Ø3,0 y 3 pines Ø0,8 |
| `demo.FlatPrj` | Proyecto FlatCAM con el cobre y el taladrado |

Se regeneraron con `flatcam-cli` y se verifican contra el parser Python original
en `tools/verify_all.sh`.

Ejemplos:

```bash
./build/flatcam-cli gerber examples/board_copper.gbr
./build/flatcam-cli isolate examples/board_copper.gbr --tool-dia 0.2 --passes 2 --overlap 10 -o iso.nc
./build/flatcam-cli cutout examples/board_outline.gbr --tool-dia 2.0 --margin 0.5 --tabs 4 --tab-width 3 -o cut.nc
./build/flatcam-cli ncc examples/board_copper.gbr --tool-dia 0.5 --step-over 0.5 --clearance 0.1 --angle 45 --area=0,0,40,30 -o ncc.nc
./build/flatcam-cli gcode examples/board.drl --optimize -o drill.nc
./build/flatcam-cli project examples/demo.FlatPrj
```
