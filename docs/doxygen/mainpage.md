# FlatCAM C++ — Manual del código

Port nativo de **FlatCAM 2024.4** a **C++20**, **Qt 6** y **GEOS**.

Este manual documenta la arquitectura interna, las clases, los módulos y la API
pública del proyecto. Está generado con **Doxygen** a partir de los comentarios
del código fuente.

## Enlaces

- [Manual de usuario](../manual_html/index.html)
- [Repositorio](https://github.com/dnasta/FlatCAM_Cpp)
- [Descargar v0.3.1](https://github.com/dnasta/FlatCAM_Cpp/releases/tag/v0.3.1)
- [Avisos de terceros](https://github.com/dnasta/FlatCAM_Cpp/blob/main/THIRD_PARTY_NOTICES.md)

## Arquitectura en capas

```text
┌──────────────────────────────────────────────────────────────┐
│ GUI Qt 6 (src/gui)                                           │
│ MainWindow · Canvas · View3D · editores · paneles             │
├──────────────────────────────────────────────────────────────┤
│ Scripting (src/script, src/cli)                              │
│ QJSEngine · consola Tcl · CLI flatcam-cli                    │
├──────────────────────────────────────────────────────────────┤
│ Herramientas CAM (src/cam)                                    │
│ G-code · aislamiento · corte · NCC · pintura · thieving       │
├──────────────────────────────────────────────────────────────┤
│ Núcleo (src/core, src/parsers)                                │
│ Geometría GEOS · Gerber · Excellon · proyecto · importadores  │
└──────────────────────────────────────────────────────────────┘
```

## Módulos principales

| Módulo | Archivos | Responsabilidad |
|---|---|---|
| Geometría | `include/flatcam/geometry.hpp`, `src/core/geometry.cpp` | `Geom`, `BBox`, `SpatialIndex` sobre GEOS |
| Unidades | `units.hpp`, `src/core/units.cpp` | mm/in y coordenadas Gerber |
| Gerber | `gerber.hpp`, `src/parsers/gerber.cpp` | RS-274X, aperturas, macros, regiones |
| Excellon | `excellon.hpp`, `src/parsers/excellon.cpp` | Taladros, ranuras, ruteo |
| Importadores | `importers.hpp`, `importers.cpp`, `pdf.cpp` | SVG, DXF, HPGL2, PDF |
| Proyecto | `project.hpp`, `src/core/project.cpp` | `.FlatPrj`, JSON, lzma, streaming |
| G-code | `gcode.hpp`, `src/cam/gcode.cpp` | 18 postprocesadores, taladrado, multiherramienta |
| Aislamiento | `isolation.hpp`, `isolation.cpp` | Offsets, V-shape, rest machining |
| Corte | `cutout.hpp`, `cutout.cpp` | Margen, puentes, multipasada |
| NCC / pintura | `ncc.hpp`, `paint.hpp` | Hatch, contornos, despeje |
| Utilidades CAM | `tools.hpp`, `tools.cpp` | Fuerza óptima, punch, taladros, reglas |
| GUI | `src/gui/*` | Ventana, canvas, 3D, editores |
| Scripting | `src/script/*`, `src/cli/*` | API JS, Tcl, CLI |

## Flujo de datos

1. **Importación**: el parser convierte el archivo a `Geom` en milímetros.
2. **Modelo**: `MainWindow` guarda objetos, capas y tablas en memoria.
3. **CAM**: una herramienta recibe `Geom` y devuelve recorridos (`PathJob`).
4. **G-code**: `pathsGCode`/`drillingGCode` generan el programa.
5. **Persistencia**: `Project` serializa geometría y opciones a `.FlatPrj`.

## Convenciones

- Todas las coordenadas internas están en **milímetros**.
- Las geometrías son **inmutables**; cada operación devuelve una copia nueva.
- Los errores de E/S se informan con `std::runtime_error`.
- El código se documenta con Doxygen (`@brief`, `@param`, `@return`).
- La nomenclatura sigue el estilo del original (`isolate`, `cutout`, `ncc`).

## Compilar y generar este manual

```bash
cmake -S . -B build-gui -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/macos"
cmake --build build-gui -j8

doxygen Doxyfile
open docs/manual_cpp/index.html
```

## Tests

```bash
./build-gui/flatcam_tests
./build-gui/flatcam_gui_tests
```

## Licencia

GPLv3. FlatCAM original: Juan Pablo Caram y colaboradores. Ver
`THIRD_PARTY_NOTICES.md` y el README para la cadena de autoría.
