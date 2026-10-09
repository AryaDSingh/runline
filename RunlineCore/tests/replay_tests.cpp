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

/// First difference in the host-facing outputs (trace v3 `ext`), or "".
std::string DiffExt(const trace::Ext& want, const Controller& c) {
    std::ostringstream d;
    const auto cmp = [&](const std::string& f, float w, float g) { if (d.str().empty() && !Same(w, g)) d << f << ": want " << Num(w) << ", got " << Num(g); };
    const View v = c.GetView();
    cmp("eye.x", want.eye.x, v.eye.x); cmp("eye.y", want.eye.y, v.eye.y); cmp("eye.z", want.eye.z, v.eye.z);
    cmp("mesh_offset", want.meshOffset, c.meshOffset);
    const auto hand = [&](const char* name, const std::optional<Vec3>& w, const std::optional<Vec3>& g) {
        if (!d.str().empty()) return;
        if (w.has_value() != g.has_value()) { d << name << ": want " << (w ? "a hand" : "none") << ", got " << (g ? "a hand" : "none"); return; }
        if (w) { cmp(std::string(name) + ".x", w->x, g->x); cmp(std::string(name) + ".y", w->y, g->y); cmp(std::string(name) + ".z", w->z, g->z); }
    };
    hand("against_wall.left", want.left, c.againstWall.left);
    hand("against_wall.right", want.right, c.againstWall.right);
    std::vector<std::pair<std::string, std::vector<float>>> got;
    for (const Event& e : c.events) {
        if (e.kind == EventKind::Land) got.push_back({"Land", {e.impact, e.fall}});
        if (e.kind == EventKind::Dodge || e.kind == EventKind::WallRunDodge || e.kind == EventKind::WallClimbDodge)
            got.push_back({EventName(e.kind), {e.dir.x, e.dir.y, e.dir.z}});
    }
    if (d.str().empty() && got.size() != want.payloads.size()) d << "event payloads: want " << want.payloads.size() << ", got " << got.size();
    for (size_t k = 0; d.str().empty() && k < got.size(); ++k) {
        if (got[k].first != want.payloads[k].first) { d << "payload " << k << ": want " << want.payloads[k].first << ", got " << got[k].first; break; }
        for (size_t j = 0; j < got[k].second.size() && j < want.payloads[k].second.size(); ++j)
            cmp(got[k].first + "[" + std::to_string(j) + "]", want.payloads[k].second[j], got[k].second[j]);
    }
    return d.str();
}

/// Replays a trace; returns "" if every frame matches, else where and how it first diverged.
std::string Replay(const trace::Trace& t) {
    if (!t.defaultTuning) return "uses non-default tuning (not replayable yet)";
    if (t.frames.empty()) return "has no frames";
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
        std::string d = Diff(f.out, c, f.events);
        if (d.empty() && f.ext) d = DiffExt(*f.ext, c);
        if (!d.empty()) return "frame " + std::to_string(i) + " (of " + std::to_string(t.frames.size()) + "): " + d;
    }
    return "";
}

} // namespace

TEST_CASE("replay: every committed trace matches faith_move frame by frame") {
    const auto names = trace::All();
    REQUIRE(names.size() >= 15);
    std::vector<std::string> failures;
    for (const auto& n : names) {
        const std::string r = Replay(trace::Load(n));
        if (!r.empty()) failures.push_back(n + ": " + r);
    }
    for (const auto& f : failures) MESSAGE(f);
    CHECK(failures.empty());
}

TEST_CASE("replay: every recorded frame carries the host-facing outputs (trace v3)") {
    for (const auto& n : trace::All()) {
        CAPTURE(n);
        for (const auto& f : trace::Load(n).frames) REQUIRE(f.ext.has_value());
    }
}
