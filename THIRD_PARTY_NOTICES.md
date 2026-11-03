# Avisos de terceros — FlatCAM C++

FlatCAM C++ es un port nativo de FlatCAM 2024.4 y se distribuye bajo la
**GNU General Public License v3.0** (ver `LICENSE`). El código fuente está en
<https://github.com/dnasta/FlatCAM_Cpp>.

Este producto incluye o enlaza los siguientes componentes de terceros:

| Componente | Licencia | Uso |
|---|---|---|
| FlatCAM 2024.4 | GPL-3.0 | Proyecto original del que deriva este port |
| Qt 6 (Core, Gui, Widgets, Svg, PrintSupport, Sql, Qml) | LGPL-3.0 / GPL-3.0 / comercial (se usa bajo GPL-3.0) | GUI, gráficos, scripting JS, impresión |
| GEOS | LGPL-2.1 | Operaciones geométricas |
| zlib | zlib License | Descompresión de PDF |
| Tcl/Tk | BSD-style | Consola Tcl embebida (opcional) |
| nlohmann/json | MIT | Lectura/escritura de proyectos |
| QR Code generator (Nayuki) | MIT | Generación de códigos QR |
| XZ Utils / liblzma | Dominio público / 0BSD | Proyectos `.FlatPrj` comprimidos |
| SQLite | Dominio público | Base de datos de herramientas |
| PDFKit / AppKit (macOS) | Frameworks del sistema | Rasterizado de PDF |
| GoogleTest | BSD-3-Clause | Sólo tests; no se distribuye en el binario |
| shapely / rtree / ezdxf | BSD / MIT | Sólo herramientas de verificación; no se distribuyen |

Los textos completos de las licencias están disponibles en los sitios
oficiales de cada componente. La licencia LGPL-2.1 de GEOS se incluye en
`licenses/GEOS-COPYING` y en el bundle de macOS. Este programa se distribuye
bajo GPL-3.0, que cubre también el uso de Qt bajo esa opción de licencia.

FlatCAM original: <http://flatcam.org> — autor original Juan Pablo Caram y
colaboradores.
