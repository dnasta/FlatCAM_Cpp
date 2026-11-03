#include "flatcam/geometry.hpp"

#include <stdexcept>

namespace fc {
namespace {

struct QueryContext {
    std::vector<std::uint32_t>* out;
};

void queryCallback(void* item, void* userdata) {
    auto* ctx = static_cast<QueryContext*>(userdata);
    ctx->out->push_back(static_cast<std::uint32_t>(reinterpret_cast<std::intptr_t>(item)));
}

}  // namespace

SpatialIndex::SpatialIndex() {
    initGeosRuntime();
    tree_ = GEOSSTRtree_create(10);
    if (!tree_) throw std::runtime_error("GEOSSTRtree_create failed");
}

SpatialIndex::~SpatialIndex() {
    if (tree_) GEOSSTRtree_destroy(tree_);
}

void SpatialIndex::insert(const Geom& g, std::uint32_t id) {
    if (!g.valid()) return;
    keep_.push_back(g);
    ids_.push_back(id);
    GEOSSTRtree_insert(tree_, g.raw(), reinterpret_cast<void*>(static_cast<std::intptr_t>(id)));
}

void SpatialIndex::build() {
    if (!built_) {
        GEOSSTRtree_build(tree_);
        built_ = true;
    }
}

std::vector<std::uint32_t> SpatialIndex::query(const BBox& box) const {
    std::vector<std::uint32_t> out;
    if (!box.valid) return out;
    if (!built_) const_cast<SpatialIndex*>(this)->build();
    GEOSGeometry* envelope = GEOSGeom_createRectangle(box.xmin, box.ymin, box.xmax, box.ymax);
    if (!envelope) return out;
    QueryContext ctx{&out};
    GEOSSTRtree_query(tree_, envelope, queryCallback, &ctx);
    GEOSGeom_destroy(envelope);
    return out;
}

}  // namespace fc
