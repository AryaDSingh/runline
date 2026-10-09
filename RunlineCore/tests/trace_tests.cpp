#include "Trace.h"
#include <sstream>
using namespace runline;

TEST_CASE("Parse reads every record kind, empty events and fixture text with commas") {   // Review Focus 3
    std::istringstream in(
        "trace,2\n"
        "tuning_default,1\n"
        "spawn,0,1,0,0.25\n"
        "init,0,1,0,0,0,0,0,0,Air,0,0\n"
        "state_debug,Air\n"
        "world\n"
        "box,-50,-1,-50,50,0,50\n"
        "tri,0,0,0,1,0,0,0,0,1\n"
        "fixture,ZipLine { a: Vec3(0.0, 5.0, 0.0), b: Vec3(9.0, 1.0, 0.0) }\n"
        "end_world\n"
        "in,0.016666668,0,1,0.1,-0.2,1,1,0,0,0,0,0,0\n"
        "out,0,0.9,0,0,-0.27,0,0.1,-0.2,Air,0,0,\n"
        "set,0,0,0,0,0,0,1.5,-0.2,Ground,0,0\n"
        "in,0.016666668,0,0,0,0,0,0,0,0,0,0,0,0\n"
        "out,0,0,0,0,0,0,1.5,-0.2,Ground,0,0,Land;Jump\n");
    const trace::Trace t = trace::Parse(in, "mini");
    CHECK(t.defaultTuning);
    CHECK(t.init.state == "Air");
    CHECK(t.init.feet == Vec3{0, 1, 0});
    CHECK(t.spawn == Vec3{0, 1, 0});
    CHECK(t.spawnYaw == 0.25f);
    CHECK(t.initStateDebug == "Air");
    REQUIRE(t.worlds.size() == 1);
    CHECK(t.worlds[0].boxes.size() == 1);
    CHECK(t.worlds[0].tris.size() == 1);
    REQUIRE(t.worlds[0].fixtures.size() == 1);
    CHECK(t.worlds[0].fixtures[0] == "ZipLine { a: Vec3(0.0, 5.0, 0.0), b: Vec3(9.0, 1.0, 0.0) }");
    REQUIRE(t.frames.size() == 2);
    CHECK(t.frames[0].world == 0);
    CHECK(t.frames[0].in.moveY == 1.0f);
    CHECK(t.frames[0].in.jumpPressed);
    CHECK_FALSE(t.frames[0].in.crouchHeld);
    CHECK(t.frames[0].out.vel.y == doctest::Approx(-0.27f));
    CHECK(t.frames[0].events.empty());
    CHECK(t.frames[1].out.state == "Ground");
    CHECK(t.frames[1].events == std::vector<std::string>{"Land", "Jump"});
    // A test changed the controller between steps: the frame carries what it was set to.
    CHECK_FALSE(t.frames[0].set.has_value());
    REQUIRE(t.frames[1].set.has_value());
    CHECK(t.frames[1].set->yaw == 1.5f);
}

TEST_CASE("Parse rejects a file with no init, a frame with no output, an old version or a short record") {
    std::istringstream noInit("trace,2\ntuning_default,1\nin,0.1,0,0,0,0,0,0,0,0,0,0,0,0\n");
    CHECK_THROWS(trace::Parse(noInit, "bad"));
    std::istringstream noOut("trace,2\ntuning_default,1\nspawn,0,0,0,0\ninit,0,0,0,0,0,0,0,0,Air,0,0\nworld\nend_world\nin,0.1,0,0,0,0,0,0,0,0,0,0,0,0\n");
    CHECK_THROWS(trace::Parse(noOut, "bad"));
    std::istringstream v1("trace,1\ntuning_default,1\n");
    CHECK_THROWS(trace::Parse(v1, "old"));   // version 1 has no spawn or set records
    std::istringstream shortIn("trace,2\ntuning_default,1\nspawn,0,0,0,0\ninit,0,0,0,0,0,0,0,0,Air,0,0\nworld\nend_world\nin,0.1,0,0\n");
    CHECK_THROWS(trace::Parse(shortIn, "short"));   // a truncated record throws instead of reading past the end
}

TEST_CASE("every committed trace loads, and was recorded from the golden reference commit and the committed patch") {
    const auto names = trace::All();
    REQUIRE(names.size() >= 5);
    for (const auto& n : names) {
        CAPTURE(n);
        const trace::Trace t = trace::Load(n);
        CHECK(t.frames.size() >= 10);
        CHECK_FALSE(t.worlds.empty());
        CHECK_FALSE(t.init.state.empty());
    }
    std::ifstream ref(std::string(RUNLINE_GOLDEN_DIR) + "/REFERENCE"), tref(std::string(RUNLINE_GOLDEN_DIR) + "/traces/TRACE_REFERENCE");
    std::string a, b;
    std::getline(ref, a);
    std::getline(tref, b);
    REQUIRE(a.size() >= 53);
    CHECK(b.rfind(a.substr(0, 53), 0) == 0);   // "faith-runner <40-hex sha>" is the same commit
}

TEST_CASE("falls_and_lands: the floor rebuilds, and the trace ends standing on it") {
    const trace::Trace t = trace::Load("tests__falls_and_lands__1");
    const BoxWorld w = trace::Boxes(t.worlds[t.frames.back().world]);
    CHECK(w.Overlaps(Aabb::New({-1, -0.5f, -1}, {1, -0.1f, 1})));   // the floor is there
    CHECK(t.frames.back().out.state == "Ground");
    CHECK(std::fabs(t.frames.back().out.feet.y) < 1e-3f);
}
