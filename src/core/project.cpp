/**
 * @file project.cpp
 * @brief Lectura y escritura del proyecto .FlatPrj (JSON, opcionalmente xz).
 *
 * Ofrece cuatro modos de acceso: carga completa en memoria (limitada a 512
 * MiB), carga de un solo objeto, recorrido secuencial con visitante e
 * inspección de metadatos. Los proyectos grandes se leen por streaming: el
 * descompresor lzma y los handlers SAX de nlohmann entregan objeto por objeto
 * sin materializar el archivo, que en placas reales supera el GiB. Las
 * geometrías se guardan como WKT dentro de {"__class__": "Shply"}.
 */
#include "flatcam/project.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "json.hpp"

#ifdef FC_HAVE_LZMA
#include <lzma.h>
#endif

namespace fc {
namespace {

using json = nlohmann::json;

/// Tope de memoria de loadProject(); por encima se sugiere usar --info.
constexpr std::uint64_t kMaxLoadBytes = 512ull * 1024ull * 1024ull;

/// Convierte el enum de unidades al texto del formato ("MM" o "IN").
std::string unitsToString(Units u) { return u == Units::Mm ? "MM" : "IN"; }

/**
 * @brief Interpreta el texto de unidades del proyecto.
 * @return Inch para "IN", "in" o "INCH"; Mm para cualquier otro valor.
 */
Units unitsFromString(const std::string& s) {
    return (s == "IN" || s == "in" || s == "INCH") ? Units::Inch : Units::Mm;
}

/**
 * @brief Detecta la firma xz (FD 37 7A 58 5A 00) al inicio del archivo.
 * @param path Archivo a inspeccionar.
 * @return true si el proyecto está comprimido con lzma/xz.
 */
bool isXz(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    unsigned char magic[6] = {};
    in.read(reinterpret_cast<char*>(magic), sizeof(magic));
    return in.gcount() == 6 && magic[0] == 0xFD && magic[1] == '7' && magic[2] == 'z' &&
           magic[3] == 'X' && magic[4] == 'Z' && magic[5] == 0;
}

#ifdef FC_HAVE_LZMA
// Streaming decompressor: lets the SAX parser walk a multi-gigabyte .FlatPrj
// without ever materialising it on disk or in memory.
/**
 * @brief streambuf que descomprime un .xz en streaming.
 *
 * Lee el archivo por bloques de 1 MiB, alimenta lzma_stream y expone el texto
 * descomprimido a través de underflow(), de modo que nlohmann puede parsear
 * proyectos de varios GiB con memoria acotada. Acepta streams concatenados
 * (LZMA_CONCATENATED).
 */
class LzmaStreamBuf : public std::streambuf {
public:
    /**
     * @brief Abre el archivo y prepara el decodificador lzma.
     * @param path Ruta del .FlatPrj comprimido.
     * @throw std::runtime_error si el archivo no se abre o lzma no inicializa.
     */
    explicit LzmaStreamBuf(const std::string& path) : in_(path, std::ios::binary) {
        if (!in_) throw std::runtime_error("cannot open project: " + path);
        stream_ = LZMA_STREAM_INIT;
        if (lzma_stream_decoder(&stream_, UINT64_MAX, LZMA_CONCATENATED) != LZMA_OK)
            throw std::runtime_error("lzma decoder initialisation failed");
        setg(buffer_, buffer_, buffer_);
    }

    /// Libera el estado interno de liblzma.
    ~LzmaStreamBuf() override { lzma_end(&stream_); }

protected:
    /**
     * @brief Produce el siguiente bloque de bytes descomprimidos.
     *
     * Refleja la mecánica clásica de streambuf: si el área de lectura tiene
     * datos los devuelve; si no, bombea entrada al decodificador hasta obtener
     * al menos un byte, EOF o error. Al agotarse la entrada se usa LZMA_FINISH
     * para cerrar el stream.
     * @return Siguiente carácter, o traits_type::eof() al terminar.
     * @throw std::runtime_error si lzma devuelve un código distinto de OK.
     */
    int_type underflow() override {
        if (gptr() < egptr()) return traits_type::to_int_type(*gptr());

        while (true) {
            stream_.next_out = reinterpret_cast<uint8_t*>(buffer_);
            stream_.avail_out = sizeof(buffer_);

            if (stream_.avail_in == 0 && !inputEof_) {
                in_.read(input_, sizeof(input_));
                stream_.next_in = reinterpret_cast<const uint8_t*>(input_);
                stream_.avail_in = static_cast<std::size_t>(in_.gcount());
                if (in_.gcount() == 0) inputEof_ = true;
            }

            const lzma_ret ret = lzma_code(&stream_, inputEof_ ? LZMA_FINISH : LZMA_RUN);
            const std::size_t produced = sizeof(buffer_) - stream_.avail_out;
            if (produced > 0) {
                setg(buffer_, buffer_, buffer_ + produced);
                return traits_type::to_int_type(*gptr());
            }
            if (ret == LZMA_STREAM_END) return traits_type::eof();
            if (ret != LZMA_OK)
                throw std::runtime_error("lzma decompression failed (code " +
                                         std::to_string(ret) + ")");
        }
    }

private:
    std::ifstream in_;
    lzma_stream stream_{};
    bool inputEof_ = false;
    char input_[1 << 20];
    char buffer_[1 << 20];
};
#endif

/**
 * @brief Abre el proyecto como istream, descomprimiendo si hace falta.
 * @param path Ruta del proyecto.
 * @param[out] compressed Se marca si el archivo venía comprimido en xz.
 * @return Stream listo para SAX o para lectura completa.
 * @throw std::runtime_error si el archivo no se puede abrir, o si es xz y
 *        esta compilación no define FC_HAVE_LZMA.
 */
std::unique_ptr<std::istream> openProjectStream(const std::string& path, bool& compressed) {
    compressed = isXz(path);
    if (!compressed) {
        auto stream = std::make_unique<std::ifstream>(path, std::ios::binary);
        if (!*stream) throw std::runtime_error("cannot open project: " + path);
        return stream;
    }
#ifdef FC_HAVE_LZMA
    return std::make_unique<std::istream>(new LzmaStreamBuf(path));
#else
    throw std::runtime_error(
        "this project is lzma-compressed and this build has no liblzma support");
#endif
}

/**
 * @brief Extrae geometrías de un campo solid_geometry o follow_geometry.
 *
 * Acepta las tres formas que aparecen en los proyectos FlatCAM: un string WKT
 * suelto, un objeto con "__inst__" (Shply) y un "__inst__" que es a su vez una
 * lista anidada, que se recorre recursivamente. Los elementos no reconocidos
 * se omiten.
 * @param value Valor JSON del campo.
 * @return Geometrías parseadas, en el orden encontrado.
 * @throw std::runtime_error si algún WKT es inválido (lo lanza Geom::fromWkt).
 */
std::vector<Geom> readGeometryField(const json& value) {
    std::vector<Geom> out;

    std::function<void(const json&)> readOne = [&out, &readOne](const json& item) {
        if (item.is_string()) {
            out.push_back(Geom::fromWkt(item.get<std::string>()));
            return;
        }
        if (!item.is_object()) return;
        const auto inst = item.find("__inst__");
        if (inst == item.end()) return;
        if (inst->is_string()) {
            out.push_back(Geom::fromWkt(inst->get<std::string>()));
        } else if (inst->is_array()) {
            for (const json& sub : *inst) readOne(sub);
        }
    };

    if (value.is_array()) {
        for (const json& item : value) readOne(item);
    } else {
        readOne(value);
    }
    return out;
}

/// Envuelve una geometría en el objeto Shply con WKT de 9 decimales.
json writeGeom(const Geom& geom) {
    return json{{"__class__", "Shply"}, {"__inst__", geom.toWkt(9)}};
}

/**
 * @brief Serializa un vector de geometrías al array JSON del proyecto.
 * @note Descarta geometrías nulas o vacías para no guardar ruido.
 */
json writeGeometryField(const std::vector<Geom>& geoms) {
    json list = json::array();
    for (const Geom& g : geoms) {
        if (g.valid() && !g.empty()) list.push_back(writeGeom(g));
    }
    return list;
}

/**
 * @brief Handler SAX que extrae sólo los metadatos del proyecto.
 *
 * No construye ninguna geometría: recorre el JSON y guarda versión, opciones
 * de proyecto y, por cada objeto, clase, unidades y nombre. La profundidad se
 * cuenta a mano porque nlohmann no la expone; los objetos cuelgan de objs en
 * el nivel 3.
 */
struct InfoSax : json::json_sax_t {
    ProjectInfo info;
    int depth = 0;
    bool inObjects = false;
    bool collecting = false;
    std::string currentKey;
    ProjectObjectInfo current;

    bool null() override { return true; }
    bool boolean(bool) override { return true; }
    bool number_integer(number_integer_t) override { return true; }
    bool number_unsigned(number_unsigned_t) override { return true; }
    bool number_float(number_float_t, const string_t&) override { return true; }
    bool string(string_t& value) override {
        if (depth == 1 && currentKey == "version") info.version = value;
        if (depth == 1 && currentKey == "project_options") info.options = value;
        if (collecting) {
            if (depth == 3 && currentKey == "kind") current.kind = value;
            else if (depth == 3 && currentKey == "units") current.units = value;
            else if (currentKey == "name") current.name = value;
        }
        return true;
    }
    bool binary(binary_t&) override { return true; }
    bool start_object(std::size_t) override {
        ++depth;
        if (inObjects && depth == 3) {
            collecting = true;
            current = ProjectObjectInfo{};
        }
        return true;
    }
    bool key(string_t& value) override {
        currentKey = value;
        return true;
    }
    bool end_object() override {
        if (collecting && depth == 3) {
            info.objects.push_back(current);
            collecting = false;
        }
        --depth;
        return true;
    }
    bool start_array(std::size_t) override {
        ++depth;
        if (depth == 2 && currentKey == "objs") inObjects = true;
        return true;
    }
    bool end_array() override {
        if (inObjects && depth == 2) inObjects = false;
        --depth;
        return true;
    }
    bool parse_error(std::size_t position, const std::string&,
                     const nlohmann::detail::exception& exception) override {
        throw std::runtime_error("invalid project JSON at " + std::to_string(position) + ": " +
                                 exception.what());
    }
};

/// Excepción centinela para abortar el SAX cuando ya se obtuvo el objeto.
struct StopParsing {};

// SAX handler that materialises only the geometry of one selected object.
/**
 * @brief Handler SAX que materializa un único objeto del proyecto.
 *
 * Sigue el mismo conteo de profundidad que InfoSax y, al llegar al objeto de
 * índice target, activa la recolección de solid_geometry y follow_geometry; al
 * cerrarlo lanza StopParsing para no descomprimir el resto del archivo.
 */
struct ObjectSax : json::json_sax_t {
    std::size_t target = 0;
    std::size_t index = 0;
    int depth = 0;
    bool inObjects = false;
    bool collecting = false;
    std::string currentKey;

    enum class GeomMode { None, Solid, Follow } mode = GeomMode::None;
    int geomDepth = 0;

    ProjectObject object;
    std::vector<Geom> solids;
    std::vector<Geom> follows;

    bool null() override { return true; }
    bool boolean(bool) override { return true; }
    bool number_integer(number_integer_t) override { return true; }
    bool number_unsigned(number_unsigned_t) override { return true; }
    bool number_float(number_float_t, const string_t&) override { return true; }
    bool binary(binary_t&) override { return true; }

    bool string(string_t& value) override {
        if (collecting) {
            if (depth == 3 && currentKey == "kind") object.kind = value;
            else if (depth == 3 && currentKey == "units") object.units = unitsFromString(value);
            else if (depth == 3 && currentKey == "source_file") object.sourceFile = value;
            else if (depth == 3 && currentKey == "text") object.text = value;
            else if (depth == 3 && currentKey == "properties") object.properties = value;
            else if (currentKey == "name") object.name = value;
        }
        if (mode != GeomMode::None && currentKey == "__inst__") {
            Geom geom = Geom::fromWkt(value);
            if (mode == GeomMode::Solid) solids.push_back(std::move(geom));
            else follows.push_back(std::move(geom));
        }
        return true;
    }

    bool start_object(std::size_t) override {
        ++depth;
        if (inObjects && depth == 3) {
            if (index == target) {
                collecting = true;
                object = ProjectObject{};
                solids.clear();
                follows.clear();
            }
        } else if (collecting && mode == GeomMode::None && currentKey == "solid_geometry") {
            mode = GeomMode::Solid;
            geomDepth = depth;
        } else if (collecting && mode == GeomMode::None && currentKey == "follow_geometry") {
            mode = GeomMode::Follow;
            geomDepth = depth;
        }
        return true;
    }

    bool key(string_t& value) override {
        currentKey = value;
        return true;
    }

    bool end_object() override {
        if (mode != GeomMode::None && depth == geomDepth) mode = GeomMode::None;
        if (inObjects && depth == 3) {
            if (index == target) {
                object.solids = std::move(solids);
                object.follows = std::move(follows);
                throw StopParsing{};
            }
            ++index;
        }
        --depth;
        return true;
    }

    bool start_array(std::size_t) override {
        ++depth;
        if (depth == 2 && currentKey == "objs") inObjects = true;
        else if (collecting && mode == GeomMode::None && currentKey == "solid_geometry") {
            mode = GeomMode::Solid;
            geomDepth = depth;
        } else if (collecting && mode == GeomMode::None && currentKey == "follow_geometry") {
            mode = GeomMode::Follow;
            geomDepth = depth;
        }
        return true;
    }

    bool end_array() override {
        if (mode != GeomMode::None && depth == geomDepth) mode = GeomMode::None;
        if (inObjects && depth == 2) inObjects = false;
        --depth;
        return true;
    }

    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override {
        return false;
    }
};

}  // namespace

/**
 * @brief Carga un proyecto completo en memoria.
 *
 * Abre el stream (descomprimiendo si es xz), lee por bloques controlando el
 * tope kMaxLoadBytes y parsea el JSON completo. Luego recorre objs tomando la
 * clase (por defecto "gerber"), el nombre (options.name o "<kind>_<i>"), las
 * unidades, source_file, text, properties y ambas colecciones de geometría.
 * @param path Ruta del .FlatPrj.
 * @return Proyecto con todos sus objetos.
 * @throw std::runtime_error si el archivo no se abre, supera el tope de
 *        memoria, el JSON es inválido o falta el array "objs".
 * @note Pensado para proyectos chicos; los grandes deben usar
 *       inspectProject, forEachProjectObject o loadProjectObject.
 */
Project loadProject(const std::string& path) {
    bool compressed = false;
    std::unique_ptr<std::istream> in = openProjectStream(path, compressed);

    std::string content;
    char chunk[1 << 20];
    while (in->good()) {
        in->read(chunk, sizeof(chunk));
        content.append(chunk, static_cast<std::size_t>(in->gcount()));
        if (content.size() > kMaxLoadBytes) {
            throw std::runtime_error(
                "project is too large to load in memory; use "
                "'flatcam-cli project --info' to inspect it");
        }
    }

    json doc;
    try {
        doc = json::parse(content);
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("invalid project JSON: ") + e.what());
    }

    Project project;
    if (doc.contains("version") && doc["version"].is_string())
        project.version = doc["version"].get<std::string>();

    if (doc.contains("project_options") && doc["project_options"].is_string())
        project.options = doc["project_options"].get<std::string>();
    if (!doc.contains("objs") || !doc["objs"].is_array())
        throw std::runtime_error("project has no 'objs' array");

    int index = 0;
    for (const json& obj : doc["objs"]) {
        if (!obj.is_object()) continue;
        ProjectObject out;
        if (obj.contains("kind") && obj["kind"].is_string())
            out.kind = obj["kind"].get<std::string>();
        else
            out.kind = "gerber";

        if (obj.contains("options") && obj["options"].is_object()) {
            const json& options = obj["options"];
            if (options.contains("name") && options["name"].is_string())
                out.name = options["name"].get<std::string>();
        }
        if (out.name.empty()) out.name = out.kind + "_" + std::to_string(index);

        if (obj.contains("units") && obj["units"].is_string())
            out.units = unitsFromString(obj["units"].get<std::string>());

        if (obj.contains("source_file") && obj["source_file"].is_string())
            out.sourceFile = obj["source_file"].get<std::string>();
        if (obj.contains("text") && obj["text"].is_string())
            out.text = obj["text"].get<std::string>();
        if (obj.contains("properties") && obj["properties"].is_string())
            out.properties = obj["properties"].get<std::string>();

        if (obj.contains("solid_geometry")) out.solids = readGeometryField(obj["solid_geometry"]);
        if (obj.contains("follow_geometry")) out.follows = readGeometryField(obj["follow_geometry"]);

        project.objects.push_back(std::move(out));
        ++index;
    }
    return project;
}

/**
 * @brief Escribe un proyecto en JSON legible, indentado con 2 espacios.
 *
 * Guarda versión, project_options y, por objeto, clase, unidades, archivo
 * fuente, texto, propiedades, nombre y las geometrías en WKT. Los caracteres
 * inválidos de UTF-8 se reemplazan para que el archivo siempre sea válido.
 * @param path Destino.
 * @param project Datos a serializar.
 * @throw std::runtime_error si no se puede abrir el archivo.
 * @note No comprime: el .xz lo produce otra herramienta.
 */
void saveProject(const std::string& path, const Project& project) {
    json doc;
    doc["version"] = project.version;
    doc["options"] = json::object();
    doc["project_options"] = project.options;
    doc["objs"] = json::array();

    for (const ProjectObject& obj : project.objects) {
        json item;
        item["kind"] = obj.kind;
        item["units"] = unitsToString(obj.units);
        item["source_file"] = obj.sourceFile;
        item["text"] = obj.text;
        item["properties"] = obj.properties;
        item["options"] = json{{"name", obj.name}, {"plot", true}};
        item["solid_geometry"] = writeGeometryField(obj.solids);
        item["follow_geometry"] = writeGeometryField(obj.follows);
        doc["objs"].push_back(std::move(item));
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write project: " + path);
    out << doc.dump(2, ' ', false, json::error_handler_t::replace);
}

/**
 * @brief Carga un solo objeto del proyecto sin leer el resto.
 *
 * Usa ObjectSax y corta la descompresión en cuanto el objeto está completo; en
 * proyectos de cientos de MiB esto reduce el tiempo a fracciones de segundo.
 * @param path Ruta del proyecto.
 * @param index Índice del objeto dentro de "objs".
 * @return Objeto con metadatos y geometrías; la clase y el nombre se completan
 *         con valores por defecto si faltaban.
 * @throw std::runtime_error si el índice no existe o el archivo no se abre.
 */
ProjectObject loadProjectObject(const std::string& path, std::size_t index) {
    bool compressed = false;
    std::unique_ptr<std::istream> in = openProjectStream(path, compressed);

    ObjectSax handler;
    handler.target = index;
    try {
        json::sax_parse(*in, &handler);
    } catch (const StopParsing&) {
        // Expected: we stop as soon as the requested object is complete.
    }

    if (handler.object.kind.empty() && handler.object.solids.empty() &&
        handler.object.follows.empty()) {
        throw std::runtime_error("project object " + std::to_string(index) + " not found");
    }
    if (handler.object.kind.empty()) handler.object.kind = "object";
    if (handler.object.name.empty())
        handler.object.name = handler.object.kind + "_" + std::to_string(index);
    return handler.object;
}

/**
 * @brief Handler SAX que entrega cada objeto al visitante y lo libera.
 *
 * Es el modo de carga secuencial: mantiene un solo objeto en memoria, calcula
 * un resumen (clase, nombre, unidades, cantidad de geometrías, área y caja) y
 * llama al visitante; si éste devuelve false se aborta el parseo.
 */
struct AllObjectsSax : json::json_sax_t {
    ProjectObjectVisitor visitor;
    std::vector<ProjectObjectSummary> summaries;
    int depth = 0;
    bool inObjects = false;
    bool collecting = false;
    std::string currentKey;

    enum class GeomMode { None, Solid, Follow } mode = GeomMode::None;
    int geomDepth = 0;

    ProjectObject object;
    std::vector<Geom> solids;
    std::vector<Geom> follows;

    bool null() override { return true; }
    bool boolean(bool) override { return true; }
    bool number_integer(number_integer_t) override { return true; }
    bool number_unsigned(number_unsigned_t) override { return true; }
    bool number_float(number_float_t, const string_t&) override { return true; }
    bool binary(binary_t&) override { return true; }

    bool string(string_t& value) override {
        if (collecting) {
            if (depth == 3 && currentKey == "kind") object.kind = value;
            else if (depth == 3 && currentKey == "units") object.units = unitsFromString(value);
            else if (depth == 3 && currentKey == "source_file") object.sourceFile = value;
            else if (depth == 3 && currentKey == "text") object.text = value;
            else if (depth == 3 && currentKey == "properties") object.properties = value;
            else if (currentKey == "name") object.name = value;
        }
        if (mode != GeomMode::None && currentKey == "__inst__") {
            Geom geom = Geom::fromWkt(value);
            if (mode == GeomMode::Solid) solids.push_back(std::move(geom));
            else follows.push_back(std::move(geom));
        }
        return true;
    }

    bool start_object(std::size_t) override {
        ++depth;
        if (inObjects && depth == 3) {
            collecting = true;
            object = ProjectObject{};
            solids.clear();
            follows.clear();
        } else if (collecting && mode == GeomMode::None && currentKey == "solid_geometry") {
            mode = GeomMode::Solid;
            geomDepth = depth;
        } else if (collecting && mode == GeomMode::None && currentKey == "follow_geometry") {
            mode = GeomMode::Follow;
            geomDepth = depth;
        }
        return true;
    }

    bool key(string_t& value) override {
        currentKey = value;
        return true;
    }

    /**
     * @brief Completa y entrega el objeto recién leído.
     *
     * Rellena clase y nombre faltantes, mueve las geometrías acumuladas, calcula
     * el resumen (suma de áreas y unión de cajas de los sólidos) y llama al
     * visitante.
     * @return false si el visitante pidió detener el recorrido.
     */
    bool finishObject() {
        if (object.kind.empty()) object.kind = "object";
        if (object.name.empty())
            object.name = object.kind + "_" + std::to_string(summaries.size());
        object.solids = std::move(solids);
        object.follows = std::move(follows);

        ProjectObjectSummary summary;
        summary.kind = object.kind;
        summary.name = object.name;
        summary.units = object.units;
        summary.solids = object.solids.size();
        summary.follows = object.follows.size();
        for (const Geom& g : object.solids) {
            summary.area += g.area();
            const BBox b = g.bounds();
            if (!b.valid) continue;
            if (!summary.bounds.valid) {
                summary.bounds = b;
            } else {
                summary.bounds.xmin = std::min(summary.bounds.xmin, b.xmin);
                summary.bounds.ymin = std::min(summary.bounds.ymin, b.ymin);
                summary.bounds.xmax = std::max(summary.bounds.xmax, b.xmax);
                summary.bounds.ymax = std::max(summary.bounds.ymax, b.ymax);
            }
        }
        summaries.push_back(summary);
        const bool keepGoing = visitor ? visitor(std::move(object)) : true;
        object = ProjectObject{};
        return keepGoing;
    }

    bool end_object() override {
        if (mode != GeomMode::None && depth == geomDepth) mode = GeomMode::None;
        if (inObjects && depth == 3) {
            collecting = false;
            if (!finishObject()) throw StopParsing{};
        }
        --depth;
        return true;
    }

    bool start_array(std::size_t) override {
        ++depth;
        if (depth == 2 && currentKey == "objs") inObjects = true;
        else if (collecting && mode == GeomMode::None && currentKey == "solid_geometry") {
            mode = GeomMode::Solid;
            geomDepth = depth;
        } else if (collecting && mode == GeomMode::None && currentKey == "follow_geometry") {
            mode = GeomMode::Follow;
            geomDepth = depth;
        }
        return true;
    }

    bool end_array() override {
        if (mode != GeomMode::None && depth == geomDepth) mode = GeomMode::None;
        if (inObjects && depth == 2) inObjects = false;
        --depth;
        return true;
    }

    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override {
        return false;
    }
};

/**
 * @brief Recorre todos los objetos de un proyecto en un solo pase SAX.
 * @param path Ruta del .FlatPrj.
 * @param visitor Callable que recibe ProjectObject por valor y devuelve false
 *        para cortar el recorrido.
 * @return Resumen de cada objeto procesado, con área y límites.
 * @throw std::runtime_error si el archivo no se abre o el JSON es inválido.
 */
std::vector<ProjectObjectSummary> forEachProjectObject(const std::string& path,
                                                       const ProjectObjectVisitor& visitor) {
    bool compressed = false;
    std::unique_ptr<std::istream> in = openProjectStream(path, compressed);

    AllObjectsSax handler;
    handler.visitor = visitor;
    try {
        json::sax_parse(*in, &handler);
    } catch (const StopParsing&) {
        // Visitor asked to stop.
    }
    return handler.summaries;
}

/**
 * @brief Lee sólo los metadatos del proyecto, sin su geometría.
 *
 * Pensado para proyectos enormes: no materializa WKT ni polígonos, así que
 * responde en segundos incluso con archivos de varios GiB.
 * @param path Ruta del .FlatPrj, comprimido o no.
 * @return Versión, opciones de proyecto, indicador de compresión y objetos.
 * @throw std::runtime_error si el archivo no se puede abrir o parsear.
 */
ProjectInfo inspectProject(const std::string& path) {
    bool compressed = false;
    std::unique_ptr<std::istream> in = openProjectStream(path, compressed);

    InfoSax handler;
    handler.info.compressed = compressed;
    if (!json::sax_parse(*in, &handler))
        throw std::runtime_error("could not parse project JSON");

    for (ProjectObjectInfo& obj : handler.info.objects) {
        if (obj.kind.empty()) obj.kind = "object";
        if (obj.name.empty()) obj.name = obj.kind;
    }
    return handler.info;
}

}  // namespace fc
