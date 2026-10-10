#pragma once
// Replays a recorded trace through RunlineCore and reports the first divergence. Header-only and
// doctest-free: the desktop tests run it on BoxWorld/MeshWorld, Unreal's adapter tests on
// FUnrealWorld.
#include <bit>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include "TraceFormat.h"
#include "runline/Controller.h"
#include <bit>
#include <cstdio>
#include <cstdint>
#include <sstream>
#include "runline/Controller.h"

namespace replay {
using namespace runline;

inline bool Same(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

inline std::string Num(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

/// A recorded state with no payload (a state with one, such as LedgeHang, can't be rebuilt from
/// its name: those traces aren't replayable yet).
inline State StateFromName(const std::string& name) {
    if (name == "Ground") return Ground{};
    if (name == "Air") return Air{};
    throw std::runtime_error("trace sets state " + name + ", which replay can't rebuild yet");
}

/// A starting state rebuilt from its full Rust Debug text (trace `state_debug`), for the states
/// tests start in with a payload. So far: `LedgeHang { normal: Vec3(x, y, z), ledge_y: Y, turned: B }`
/// and `LayOnGround { t: T, getting_up: None | Some(G), back_roll: B }`.
inline std::optional<State> StateFromDebug(const std::string& d) {
    if (d.rfind("LayOnGround {", 0) == 0) {
        float t = 0.0f, g = 0.0f;
        char backRoll[8] = {};
        if (std::sscanf(d.c_str(), "LayOnGround { t: %f, getting_up: Some(%f), back_roll: %7[a-z] }", &t, &g, backRoll) == 3)
            return LayOnGround{t, g, std::string(backRoll) == "true"};
        if (std::sscanf(d.c_str(), "LayOnGround { t: %f, getting_up: None, back_roll: %7[a-z] }", &t, backRoll) == 2)
            return LayOnGround{t, std::nullopt, std::string(backRoll) == "true"};
        throw std::runtime_error("can't read state " + d);
    }
    if (d.rfind("LedgeHang {", 0) != 0) return std::nullopt;
    float x, y, z, ledgeY;
    char turned[8] = {};
    if (std::sscanf(d.c_str(), "LedgeHang { normal: Vec3(%f, %f, %f), ledge_y: %f, turned: %7[a-z] }", &x, &y, &z, &ledgeY, turned) != 5)
        throw std::runtime_error("can't read state " + d);
    return LedgeHang{{x, y, z}, ledgeY, std::string(turned) == "true"};
}

/// What a test set before a step (or before the first): the snapshot's fields. `debug` is the
/// starting state's full text, when there is one.
inline void Apply(Controller& c, const trace::Snapshot& s, const std::string& debug = "") {
    c.feet = s.feet;
    c.vel = s.vel;
    c.yaw = s.yaw;
    c.pitch = s.pitch;
    const auto full = StateFromDebug(debug);
    c.state = full ? *full : StateFromName(s.state);
    c.crouched = s.crouched;
    c.sprintCharge = s.sprintCharge;
}

/// First difference between a recorded snapshot and the controller, or "".
inline std::string Diff(const trace::Snapshot& want, const Controller& c, const std::vector<std::string>& wantEvents) {
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
inline std::string DiffExt(const trace::Ext& want, const Controller& c) {
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

} // namespace replay

namespace runline {
/// The replay's view of the controller's private bookkeeping (friend of Controller).
struct ReplayAccess {
    static std::string Diff(const trace::Hid& want, const Controller& c);
};
} // namespace runline

namespace replay {
using namespace runline;

inline const char* TakeoffName(Takeoff t) {
    switch (t) {
        case Takeoff::Other: return "Other";
        case Takeoff::Jump: return "Jump";
        case Takeoff::WallJump: return "WallJump";
    }
    return "?";
}

inline const char* TurnKindName(TurnKind k) {
    switch (k) {
        case TurnKind::Run: return "Run";
        case TurnKind::Stand: return "Stand";
        case TurnKind::Air: return "Air";
        case TurnKind::WallClimb: return "WallClimb";
        case TurnKind::Swing: return "Swing";
    }
    return "?";
}

} // namespace replay

/// First difference in the bookkeeping (trace v4 `hid`), or "".
inline std::string runline::ReplayAccess::Diff(const trace::Hid& want, const runline::Controller& c) {
    using namespace replay;
    using namespace runline;
    std::ostringstream d;
    const auto cmp = [&](const char* f, float w, float g) { if (d.str().empty() && !Same(w, g)) d << f << ": want " << Num(w) << ", got " << Num(g); };
    const auto flag = [&](const char* f, bool w, bool g) { if (d.str().empty() && w != g) d << f << ": want " << w << ", got " << g; };
    const auto text = [&](const char* f, const std::string& w, const std::string& g) { if (d.str().empty() && w != g) d << f << ": want " << w << ", got " << g; };
    cmp("air_time", want.airTime, c.airTime_);
    flag("jumped_since_ground", want.jumpedSinceGround, c.jumpedSinceGround_);
    cmp("jump_buffer", want.jumpBuffer, c.jumpBuffer_);
    cmp("roll_trigger_age", want.rollTriggerAge, c.rollTriggerAge_);
    text("takeoff", want.takeoff, TakeoffName(c.takeoff_));
    cmp("pre_jump_momentum", want.preJumpMomentum, c.preJumpMomentum_);
    cmp("last_jump_at.x", want.lastJumpAt.x, c.lastJumpAt_.x);
    cmp("last_jump_at.y", want.lastJumpAt.y, c.lastJumpAt_.y);
    cmp("last_jump_at.z", want.lastJumpAt.z, c.lastJumpAt_.z);
    if (d.str().empty() && want.chainWallruns != c.chainWallruns_) d << "chain_wallruns: want " << want.chainWallruns << ", got " << c.chainWallruns_;
    flag("climbed_this_air", want.climbedThisAir, c.climbedThisAir_);
    flag("last_wall_normal set", want.lastWallNormal.has_value(), c.lastWallNormal_.has_value());
    if (want.lastWallNormal && c.lastWallNormal_) {
        cmp("last_wall_normal.x", want.lastWallNormal->x, c.lastWallNormal_->x);
        cmp("last_wall_normal.y", want.lastWallNormal->y, c.lastWallNormal_->y);
        cmp("last_wall_normal.z", want.lastWallNormal->z, c.lastWallNormal_->z);
    }
    cmp("regrab_cooldown", want.regrabCooldown, c.regrabCooldown_);
    flag("turn set", want.turn.has_value(), c.turn_.has_value());
    if (want.turn && c.turn_) {
        cmp("turn.from", want.turn->from, c.turn_->from);
        cmp("turn.t", want.turn->t, c.turn_->t);
        text("turn.kind", want.turn->kind, TurnKindName(c.turn_->kind));
    }
    cmp("yaw_rate", want.yawRate, c.yawRate_);
    cmp("frame_turn", want.frameTurn, c.frameTurn_);
    cmp("eye_height", want.eyeHeight, c.eyeHeight_);
    cmp("fall_peak", want.fallPeak, c.fallPeak_);
    flag("coiled", want.coiled, c.coiled_);
    cmp("low_grav", want.lowGrav, c.lowGrav_);
    flag("turned_in_air", want.turnedInAir, c.turnedInAir);
    cmp("slide_yaw", want.slideYaw, c.slideYaw);
    flag("soft_brace", want.softBrace, c.softBrace);
    flag("wallrun_wall_above", want.wallrunWallAbove, c.wallrunWallAbove);
    cmp("grab_speed", want.grabSpeed, c.grabSpeed);
    cmp("hang_time", want.hangTime, c.hangTime);
    return d.str();
}

namespace replay {
using namespace runline;

/// Replays a trace; returns "" if every frame matches, else where and how it first diverged.
/// `worldFor(i)` gives the World for trace world i (BoxWorld/MeshWorld on the desktop,
/// FUnrealWorld in Unreal); it is called every frame, so build worlds up front.
inline std::string Replay(const trace::Trace& t, const std::function<const World&(std::size_t)>& worldFor) {
    if (!t.defaultTuning) return "uses non-default tuning (not replayable yet)";
    if (t.frames.empty()) return "has no frames";
    // Controller::new at the recorded spawn, then whatever the test set before its first step.
    Controller c(Tuning{}, t.spawn, t.spawnYaw);
    Apply(c, t.init, t.initStateDebug);
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
            c.Step(f.in.dt, in, worldFor(f.world));
        } catch (const NotPorted& e) {
            return "frame " + std::to_string(i) + ": reached " + e.what();
        }
        std::string d = Diff(f.out, c, f.events);
        if (d.empty() && f.ext) d = DiffExt(*f.ext, c);
        if (d.empty() && f.hid) d = ReplayAccess::Diff(*f.hid, c);
        if (!d.empty()) return "frame " + std::to_string(i) + " (of " + std::to_string(t.frames.size()) + "): " + d;
    }
    return "";
}

} // namespace replay

