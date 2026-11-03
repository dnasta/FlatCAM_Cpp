#include <gtest/gtest.h>

#include <filesystem>
#include <cmath>
#include <fstream>

#include "flatcam/project.hpp"

using namespace fc;

TEST(Geometry, WktRoundTrip) {
    const Geom original = Geom::box(1, 2, 4, 6);
    const Geom restored = Geom::fromWkt(original.toWkt());
    EXPECT_NEAR(restored.area(), original.area(), 1e-9);
    const BBox b = restored.bounds();
    ASSERT_TRUE(b.valid);
    EXPECT_NEAR(b.xmin, 1.0, 1e-9);
    EXPECT_NEAR(b.ymax, 6.0, 1e-9);
}

TEST(Project, SaveAndLoadRoundTrip) {
    Project project;
    project.version = "2024.4";
    project.options = R"({"iso":[[1,2]]})";

    ProjectObject gerber;
    gerber.kind = "gerber";
    gerber.name = "top_copper";
    gerber.units = Units::Mm;
    gerber.solids = {Geom::box(0, 0, 10, 10)};
    gerber.sourceFile = "top_copper.gbr";

    ProjectObject note;
    note.kind = "document";
    note.name = "nota";
    note.text = "hola mundo";
    note.properties = R"({"Autor":"test"})";

    ProjectObject drill;
    drill.kind = "excellon";
    drill.name = "drill";
    drill.units = Units::Mm;
    drill.solids = {Geom::circle({5, 5}, 0.5, 16)};

    project.objects = {gerber, note, drill};

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "flatcam_cpp_project_test.FlatPrj";
    saveProject(path.string(), project);

    const Project loaded = loadProject(path.string());
    ASSERT_EQ(loaded.objects.size(), std::size_t{3});
    EXPECT_EQ(loaded.objects[0].kind, "gerber");
    EXPECT_EQ(loaded.options, R"({"iso":[[1,2]]})");
    EXPECT_EQ(loaded.objects[0].name, "top_copper");
    EXPECT_NEAR(loaded.objects[0].solids[0].area(), 100.0, 1e-6);
    EXPECT_EQ(loaded.objects[1].kind, "document");
    EXPECT_EQ(loaded.objects[1].text, "hola mundo");
    EXPECT_EQ(loaded.objects[1].properties, R"({"Autor":"test"})");
    EXPECT_EQ(loaded.objects[2].kind, "excellon");
    const double circle = 0.5 * 64 * 0.25 * std::sin(2.0 * M_PI / 64.0);
    EXPECT_NEAR(loaded.objects[2].solids[0].area(), circle, 1e-6);

    std::filesystem::remove(path);
}

TEST(Project, RejectsInvalidJson) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "flatcam_cpp_bad.FlatPrj";
    std::ofstream(path) << "{not json";
    EXPECT_THROW(loadProject(path.string()), std::runtime_error);
    std::filesystem::remove(path);
}

TEST(Project, LoadSingleObject) {
    Project project;
    ProjectObject a;
    a.kind = "gerber";
    a.name = "outline";
    a.solids = {Geom::box(0, 0, 10, 10)};
    ProjectObject b;
    b.kind = "gerber";
    b.name = "copper";
    b.solids = {Geom::box(0, 0, 20, 10)};
    project.objects = {a, b};

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "flatcam_cpp_object_test.FlatPrj";
    saveProject(path.string(), project);

    const ProjectObject loaded = loadProjectObject(path.string(), 1);
    EXPECT_EQ(loaded.name, "copper");
    ASSERT_EQ(loaded.solids.size(), std::size_t{1});
    EXPECT_NEAR(loaded.solids[0].area(), 200.0, 1e-6);

    std::filesystem::remove(path);
}
