#include "Trace.h"
#include <bit>
#include <cstdio>
#include <cstdint>
#include <sstream>
#include "runline/Controller.h"
using namespace runline;

namespace {

bool Same(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

std::string Num(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

/// A recorded state with no payload (a state with one, such as LedgeHang, can't be rebuilt from
/// its name: those traces aren't replayable yet).
State StateFromName(const std::string& name) {
    if (name == "Ground") return Ground{};
    if (name == "Air") return Air{};
    throw std::runtime_error("trace sets state " + name + ", which replay can't rebuild yet");
}

/// What a test set before a step (or before the first): the snapshot's fields.
void Apply(Controller& c, const trace::Snapshot& s) {
    c.feet = s.feet;
    c.vel = s.vel;
    c.yaw = s.yaw;
    c.pitch = s.pitch;
    c.state = StateFromName(s.state);
    c.crouched = s.crouched;
    c.sprintCharge = s.sprintCharge;
}

/// First difference between a recorded snapshot and the controller, or "".
std::string Diff(const trace::Snapshot& want, const Controller& c, const std::vector<std::string>& wantEvents) {
    std::ostringstream d;
    const auto cmp = [&](const char* f, float w, float g) { if (d.str().empty() && !Same(w, g)) d << f << ": want " << Num(w) << ", got " << Num(g); };
    cmp("feet.x", want.feet.x, c.feet.x); cmp("feet.y", want.feet.y, c.feet.y); cmp("feet.z", want.feet.z, c.feet.z);
    cmp("vel.x", want.vel.x, c.vel.x); cmp("vel.y", want.vel.y, c.vel.y); cmp("vel.z", want.vel.z, c.vel.z);
    cmp("yaw", want.yaw, c.yaw); cmp("pitch", want.pitch, c.pitch);
    cmp("sprint_charge", want.sprintCharge, c.sprintCharge);
    if (d.str().empty() && want.state != StateName(c.state)) d << "state: want " << want.state << ", got " << StateName(c.state);
    if (d.str().empty() && want.crouched != c.crouched) d << "crouched: want " << want.crouched << ", got " << c.crouched;
    if (d.str().empty()) {
        std::vector<std::string> got;
        for (const Event& e : c.events) got.push_back(EventName(e.kind));
        if (got != wantEvents) {
            d << "events: want [";
            for (const auto& e : wantEvents) d << e << ' ';
            d << "], got [";
            for (const auto& e : got) d << e << ' ';
            d << ']';
        }
    }
    return d.str();
}

/// Replays a trace; returns "" if every frame matches, else where and how it first diverged.
std::string Replay(const trace::Trace& t) {
    if (!t.defaultTuning) return "uses non-default tuning (not replayable yet)";
    // Controller::new at the recorded spawn, then whatever the test set before its first step.
    Controller c(Tuning{}, t.spawn, t.spawnYaw);
    Apply(c, t.init);
    for (size_t i = 0; i < t.frames.size(); ++i) {
        const trace::Frame& f = t.frames[i];
        if (f.set) Apply(c, *f.set);
        const auto& wd = t.worlds[f.world];
        if (!wd.fixtures.empty()) return "frame " + std::to_string(i) + ": fixtures not ported yet";
        Input in;
        in.moveAxis = {f.in.moveX, f.in.moveY};
        in.look = {f.in.lookX, f.in.lookY};
        in.jumpPressed = f.in.jumpPressed; in.jumpHeld = f.in.jumpHeld;
        in.crouchPressed = f.in.crouchPressed; in.crouchHeld = f.in.crouchHeld;
        in.turnPressed = f.in.turnPressed; in.meleePressed = f.in.meleePressed;
        in.takedownPressed = f.in.takedownPressed; in.strafeRaw = f.in.strafeRaw;
        try {
            if (wd.tris.empty()) c.Step(f.in.dt, in, trace::Boxes(wd));
            else c.Step(f.in.dt, in, trace::Mesh(wd));
        } catch (const NotPorted& e) {
            return "frame " + std::to_string(i) + ": reached " + e.what();
        }
        const std::string d = Diff(f.out, c, f.events);
        if (!d.empty()) return "frame " + std::to_string(i) + " (of " + std::to_string(t.frames.size()) + "): " + d;
    }
    return "";
}

} // namespace

TEST_CASE("replay: falls_and_lands") { CHECK(Replay(trace::Load("tests__falls_and_lands__1")) == ""); }
TEST_CASE("replay: sprint_builds_up") { CHECK(Replay(trace::Load("tests__sprint_builds_up__1")) == ""); }
TEST_CASE("replay: jump_arc_and_land") { CHECK(Replay(trace::Load("tests__jump_arc_and_land__1")) == ""); }
TEST_CASE("replay: jump_landing_hands_back_the_boost") { CHECK(Replay(trace::Load("tests__jump_landing_hands_back_the_boost__1")) == ""); }
TEST_CASE("replay: hard_landing_stops_dead") { CHECK(Replay(trace::Load("tests__hard_landing_stops_dead__1")) == ""); }
TEST_CASE("replay: a_hair_into_the_ground_holds (mesh, buffered jump)") { CHECK(Replay(trace::Load("mesh_tests__a_hair_into_the_ground_holds__2")) == ""); }
