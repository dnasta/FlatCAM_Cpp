/**
 * @file project.hpp
 * @brief Modelo y persistencia de proyectos `.FlatPrj`.
 *
 * Un proyecto FlatCAM es un JSON con `version`, `options` y `objs`. La
 * geometría se guarda como WKT dentro de objetos `{"__class__": "Shply"}`.
 * El archivo puede estar comprimido con lzma; la lectura usa streaming para
 * proyectos de varios gigabytes.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "flatcam/geometry.hpp"
#include "flatcam/units.hpp"

namespace fc {

/**
 * @brief Objeto de proyecto (Gerber, Excellon, geometría, documento, script...).
 */
struct ProjectObject {
    std::string kind = "gerber"; ///< Tipo: gerber, excellon, geometry, document...
    std::string name;            ///< Nombre visible.
    Units units = Units::Mm;     ///< Unidades del objeto.
    std::vector<Geom> solids;    ///< Polígonos rellenos.
    std::vector<Geom> follows;   ///< Trazos/recorridos.
    std::string sourceFile;      ///< Archivo fuente para reeditar.
    std::string text;            ///< Texto de documentos/scripts/CNCJob.
    std::string properties;      ///< JSON con propiedades por objeto.
};

/**
 * @brief Proyecto completo.
 */
struct Project {
    std::string version = "2024.4";       ///< Versión del formato.
    std::vector<ProjectObject> objects;   ///< Objetos.
    std::string options;                  ///< JSON con opciones y tablas.
};

/**
 * @brief Metadatos mínimos de un objeto (sin geometría).
 */
struct ProjectObjectInfo {
    std::string kind;  ///< Tipo.
    std::string name;  ///< Nombre.
    std::string units; ///< Unidades (`MM`/`IN`).
};

/**
 * @brief Información de un proyecto leída en streaming.
 */
struct ProjectInfo {
    std::string version;                  ///< Versión del formato.
    bool compressed = false;              ///< `true` si el archivo usa lzma.
    std::vector<ProjectObjectInfo> objects;///< Metadatos de objetos.
    std::string options;                  ///< Opciones del proyecto (JSON).
};

/**
 * @brief Carga un proyecto completo.
 * @param path Archivo `.FlatPrj` (JSON o lzma).
 * @return Proyecto con objetos y opciones.
 * @throw std::runtime_error si el formato es inválido o supera el límite.
 */
Project loadProject(const std::string& path);
/**
 * @brief Guarda un proyecto en JSON.
 * @param path Archivo de salida.
 * @param project Proyecto a guardar.
 */
void saveProject(const std::string& path, const Project& project);

/**
 * @brief Lee sólo metadatos de un proyecto.
 *
 * Funciona con proyectos lzma de varios GB: descomprime en streaming y no
 * materializa geometría.
 *
 * @param path Archivo `.FlatPrj`.
 */
ProjectInfo inspectProject(const std::string& path);

/**
 * @brief Carga un único objeto de un proyecto grande.
 * @param path Archivo `.FlatPrj`.
 * @param index Índice basado en cero.
 * @return Objeto con su geometría.
 */
ProjectObject loadProjectObject(const std::string& path, std::size_t index);

/**
 * @brief Resumen de un objeto durante el recorrido en streaming.
 */
struct ProjectObjectSummary {
    std::string kind;       ///< Tipo.
    std::string name;       ///< Nombre.
    Units units = Units::Mm;///< Unidades.
    std::size_t solids = 0; ///< Cantidad de sólidos.
    std::size_t follows = 0;///< Cantidad de recorridos.
    double area = 0.0;      ///< Área total.
    BBox bounds;            ///< Límites.
};

/// Visitante que consume un objeto y devuelve `false` para detener el recorrido.
using ProjectObjectVisitor = std::function<bool(ProjectObject&&)>;
/**
 * @brief Recorre todos los objetos de un proyecto en streaming.
 *
 * La geometría de cada objeto se libera al terminar el visitante, por lo que
 * el consumo de memoria queda acotado a un objeto.
 *
 * @param path Archivo `.FlatPrj`.
 * @param visitor Función opcional que recibe cada objeto.
 * @return Resumen de todos los objetos visitados.
 */
std::vector<ProjectObjectSummary> forEachProjectObject(const std::string& path,
                                                       const ProjectObjectVisitor& visitor = {});

}  // namespace fc
