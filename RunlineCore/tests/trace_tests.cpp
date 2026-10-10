#include "Trace.h"
#include <sstream>
using namespace runline;

TEST_CASE("Parse reads every record kind, empty events and fixture text with commas") {   // Review Focus 3
    std::istringstream in(
        "trace,5\n"
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
        "out,0,0,0,0,0,0,1.5,-0.2,Ground,0,0,Land;Jump\n"
        "ext,0,1.66,0,-0.05,1,0.1,1,-0.2,0,,,,Land:3.5:0.6\n"
        "hid,0.5,1,0.1,inf,WallJump,6.5,1,2,3,2,1,1,0,0,0,0.25,1,0.5,0.25,Air,0.75,0.01,1.6,2.5,1,0,1,0.3,0,1,-4,0.2\n"
        "fx,0,1.7,0.5,0.1,-0.2,0.05,95,1.5,0.8,0.3,0.2,0.1\n");
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
    // The host-facing outputs: the eye, the mesh offset, the hands on a wall, event payloads.
    CHECK_FALSE(t.frames[0].ext.has_value());
    REQUIRE(t.frames[1].ext.has_value());
    const trace::Ext& x = *t.frames[1].ext;
    CHECK(x.eye == Vec3{0, 1.66f, 0});
    CHECK(x.meshOffset == -0.05f);
    REQUIRE(x.left.has_value());
    CHECK(*x.left == Vec3{0.1f, 1, -0.2f});
    CHECK_FALSE(x.right.has_value());
    REQUIRE(x.payloads.size() == 1);
    CHECK(x.payloads[0].first == "Land");
    CHECK(x.payloads[0].second == std::vector<float>{3.5f, 0.6f});
    // The bookkeeping the snapshot doesn't show.
    CHECK_FALSE(t.frames[0].hid.has_value());
    REQUIRE(t.frames[1].hid.has_value());
    const trace::Hid& h = *t.frames[1].hid;
    CHECK(h.airTime == 0.5f);
    CHECK(h.jumpedSinceGround);
    CHECK(h.jumpBuffer == 0.1f);
    CHECK(std::isinf(h.rollTriggerAge));
    CHECK(h.takeoff == "WallJump");
    CHECK(h.preJumpMomentum == 6.5f);
    CHECK(h.lastJumpAt == Vec3{1, 2, 3});
    CHECK(h.chainWallruns == 2u);
    CHECK(h.climbedThisAir);
    REQUIRE(h.lastWallNormal.has_value());
    CHECK(*h.lastWallNormal == Vec3{0, 0, 0});
    CHECK(h.regrabCooldown == 0.25f);
    REQUIRE(h.turn.has_value());
    CHECK(h.turn->from == 0.5f);
    CHECK(h.turn->t == 0.25f);
    CHECK(h.turn->kind == "Air");
    CHECK(h.yawRate == 0.75f);
    CHECK(h.frameTurn == 0.01f);
    CHECK(h.eyeHeight == 1.6f);
    CHECK(h.fallPeak == 2.5f);
    CHECK(h.coiled);
    CHECK(h.lowGrav == 0.0f);
    CHECK(h.turnedInAir);
    CHECK(h.slideYaw == 0.3f);
    CHECK_FALSE(h.softBrace);
    CHECK(h.wallrunWallAbove);
    CHECK(h.grabSpeed == -4.0f);
    CHECK(h.hangTime == 0.2f);
    // The camera.
    CHECK_FALSE(t.frames[0].fx.has_value());
    REQUIRE(t.frames[1].fx.has_value());
    const trace::Fx& fx = *t.frames[1].fx;
    CHECK(fx.eye == Vec3{0, 1.7f, 0.5f});
    CHECK(fx.yaw == 0.1f);
    CHECK(fx.pitch == -0.2f);
    CHECK(fx.roll == 0.05f);
    CHECK(fx.fov == 95.0f);
    CHECK(fx.stepPhase == 1.5f);
    CHECK(fx.gait == 0.8f);
    CHECK(fx.shake == 0.3f);
    CHECK(fx.swanForward == 0.2f);
    CHECK(fx.swanDown == 0.1f);
}

TEST_CASE("Parse rejects a file with no init, a frame with no output, an old version or a short record") {
    std::istringstream noInit("trace,5\ntuning_default,1\nin,0.1,0,0,0,0,0,0,0,0,0,0,0,0\n");
    CHECK_THROWS(trace::Parse(noInit, "bad"));
    std::istringstream noOut("trace,5\ntuning_default,1\nspawn,0,0,0,0\ninit,0,0,0,0,0,0,0,0,Air,0,0\nworld\nend_world\nin,0.1,0,0,0,0,0,0,0,0,0,0,0,0\n");
    CHECK_THROWS(trace::Parse(noOut, "bad"));
    std::istringstream v1("trace,4\ntuning_default,1\n");
    CHECK_THROWS(trace::Parse(v1, "old"));   // older versions lack spawn/set (v1), ext (v2), hid (v3) or fx (v4) records
    std::istringstream shortIn("trace,5\ntuning_default,1\nspawn,0,0,0,0\ninit,0,0,0,0,0,0,0,0,Air,0,0\nworld\nend_world\nin,0.1,0,0\n");
    CHECK_THROWS(trace::Parse(shortIn, "short"));   // a truncated record throws instead of reading past the end
    std::istringstream hidAlone("trace,5\ntuning_default,1\nspawn,0,0,0,0\ninit,0,0,0,0,0,0,0,0,Air,0,0\nworld\nend_world\nin,0.1,0,0,0,0,0,0,0,0,0,0,0,0\nout,0,0,0,0,0,0,0,0,Air,0,0,\nhid,0\n");
    CHECK_THROWS(trace::Parse(hidAlone, "short"));   // hid must follow an ext record, and be whole
}

TEST_CASE("every committed trace loads, and was recorded from the golden reference commit and the committed patch") {
    const auto names = trace::All();
    REQUIRE(names.size() >= 5);
    for (const auto& n : names) {
        CAPTURE(n);
        const trace::Trace t = trace::Load(n);
        CHECK_FALSE(t.frames.empty());   // some faith-runner tests are only a few frames long
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
