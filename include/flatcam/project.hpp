#pragma once

#include <functional>
#include <string>
#include <vector>

#include "flatcam/geometry.hpp"
#include "flatcam/units.hpp"

namespace fc {

struct ProjectObject {
    std::string kind = "gerber";  // gerber, excellon, geometry
    std::string name;
    Units units = Units::Mm;
    std::vector<Geom> solids;
    std::vector<Geom> follows;
    std::string sourceFile;
    std::string text;        // document/script/CNC job content
    std::string properties;  // JSON object with per-object properties
};

struct Project {
    std::string version = "2024.4";
    std::vector<ProjectObject> objects;
    std::string options;  // JSON-encoded project/tool options
};

struct ProjectObjectInfo {
    std::string kind;
    std::string name;
    std::string units;
};

struct ProjectInfo {
    std::string version;
    bool compressed = false;
    std::vector<ProjectObjectInfo> objects;
    std::string options;
};

// FlatCAM .FlatPrj files are JSON documents with {objs, options, version}.
// Geometries are stored as {"__class__": "Shply", "__inst__": "<WKT>"}.
// lzma-compressed projects are detected and reported as unsupported for now.
Project loadProject(const std::string& path);
void saveProject(const std::string& path, const Project& project);

// Reads only object metadata. Works on multi-gigabyte lzma-compressed
// projects by streaming the JSON (no geometry is materialised).
ProjectInfo inspectProject(const std::string& path);

// Loads a single object's geometry from a (possibly huge, lzma-compressed)
// project, without materialising the rest of the project.
ProjectObject loadProjectObject(const std::string& path, std::size_t index);

struct ProjectObjectSummary {
    std::string kind;
    std::string name;
    Units units = Units::Mm;
    std::size_t solids = 0;
    std::size_t follows = 0;
    double area = 0.0;
    BBox bounds;
};

// Streams every object of a (possibly huge) project, one at a time. Geometry
// is released after each object; the visitor can consume it (return false to
// stop). Summaries are always returned.
using ProjectObjectVisitor = std::function<bool(ProjectObject&&)>;
std::vector<ProjectObjectSummary> forEachProjectObject(const std::string& path,
                                                       const ProjectObjectVisitor& visitor = {});

}  // namespace fc
