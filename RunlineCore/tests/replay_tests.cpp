#include "Trace.h"
#include "Replay.h"
#include <memory>
using namespace runline;

namespace {

/// Replays a trace on the core's own worlds, built once per trace world.
std::string ReplayDesktop(const trace::Trace& t) {
    std::vector<std::unique_ptr<World>> worlds;
    for (const auto& wd : t.worlds) {
        if (wd.tris.empty()) worlds.push_back(std::make_unique<BoxWorld>(trace::Boxes(wd)));
        else worlds.push_back(std::make_unique<MeshWorld>(trace::Mesh(wd)));
    }
    return replay::Replay(t, [&](std::size_t i) -> const World& { return *worlds[i]; });
}

} // namespace

TEST_CASE("replay: every committed trace matches faith_move frame by frame") {
    const auto names = trace::All();
    REQUIRE(names.size() >= 15);
    std::vector<std::string> failures;
    for (const auto& n : names) {
        const std::string r = ReplayDesktop(trace::Load(n));
        if (!r.empty()) failures.push_back(n + ": " + r);
    }
    for (const auto& f : failures) MESSAGE(f);
    CHECK(failures.empty());
}

TEST_CASE("replay: every recorded frame carries the host-facing outputs and the bookkeeping (trace v4)") {
    for (const auto& n : trace::All()) {
        CAPTURE(n);
        for (const auto& f : trace::Load(n).frames) {
            REQUIRE(f.ext.has_value());
            REQUIRE(f.hid.has_value());
        }
    }
}
