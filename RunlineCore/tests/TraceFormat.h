#pragma once
// The per-frame trace format tools/trace/make-traces.sh records from faith-runner's own tests
// (the geometry, the state before the first step, each frame's input and resulting state) and
// its parser. No doctest here, so Unreal's adapter tests can read traces too (Trace.h adds the
// doctest-side loading).
#include <algorithm>
#include <cmath>
#include <istream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include "runline/BoxWorld.h"
#include "runline/MeshWorld.h"

namespace trace {

struct Snapshot {
    runline::Vec3 feet{}, vel{};
    float yaw = 0, pitch = 0;
    std::string state;
    bool crouched = false;
    float sprintCharge = 0;
};

struct FrameInput {
    float dt = 0, moveX = 0, moveY = 0, lookX = 0, lookY = 0;
    bool jumpPressed = false, jumpHeld = false, crouchPressed = false, crouchHeld = false;
    bool turnPressed = false, meleePressed = false, takedownPressed = false;
    float strafeRaw = 0;
};

struct WorldDump {
    std::vector<runline::Aabb> boxes;
    std::vector<runline::Tri> tris;
    std::vector<std::string> fixtures;   // Rust Debug text; parsed when fixtures are ported (M4)
};

/// The host-facing outputs after a step (trace v3 `ext` record): what the camera and body use,
/// none of which feeds back into the snapshot, so it needs its own check.
struct Ext {
    runline::Vec3 eye{};
    float meshOffset = 0;
    std::optional<runline::Vec3> left, right;   // AgainstWall hands
    /// Events with payloads, in order: Land (impact, fall), Dodge / WallRunDodge / WallClimbDodge (dir).
    std::vector<std::pair<std::string, std::vector<float>>> payloads;
};

/// The controller's bookkeeping after a step (trace v4 `hid` record): fields the snapshot
/// doesn't show, so a replay catches a wrong one on the frame it goes wrong.
struct Hid {
    float airTime = 0;
    bool jumpedSinceGround = false;
    float jumpBuffer = 0, rollTriggerAge = 0;
    std::string takeoff;   // Rust Debug: Other / Jump / WallJump
    float preJumpMomentum = 0;
    runline::Vec3 lastJumpAt{};
    unsigned chainWallruns = 0;
    bool climbedThisAir = false;
    std::optional<runline::Vec3> lastWallNormal;
    float regrabCooldown = 0;
    struct Turn { float from = 0, t = 0; std::string kind; };
    std::optional<Turn> turn;
    float yawRate = 0, frameTurn = 0, eyeHeight = 0, fallPeak = 0;
    bool coiled = false;
    float lowGrav = 0;
    bool turnedInAir = false;
    float slideYaw = 0;
    bool softBrace = false, wallrunWallAbove = false;
    float grabSpeed = 0, hangTime = 0;
};

/// The camera after a step (trace v5 `fx` record): faith-runner's CameraFx shot and swan neck, run
/// over the traced controller as its app and rig do.
struct Fx {
    runline::Vec3 eye{};
    float yaw = 0, pitch = 0, roll = 0, fov = 0;
    float stepPhase = 0, gait = 0, shake = 0;
    float swanForward = 0, swanDown = 0;
};

struct Frame {
    size_t world = 0;   // index into Trace::worlds
    FrameInput in;
    Snapshot out;
    std::vector<std::string> events;
    bool hasOut = false;
    /// What a test set the controller to just before this step, if it changed it between steps.
    std::optional<Snapshot> set;
    std::optional<Ext> ext;
    std::optional<Hid> hid;
    std::optional<Fx> fx;
};

struct Trace {
    std::string name;
    bool defaultTuning = false;
    bool hasInit = false;
    Snapshot init;
    /// Controller::new's spawn point and yaw.
    runline::Vec3 spawn{};
    float spawnYaw = 0;
    /// The starting state in full (Rust's Debug), for states with a payload.
    std::string initStateDebug;
    std::vector<WorldDump> worlds;
    std::vector<Frame> frames;
};

/// Split on `sep`, keeping empty fields (a trailing separator gives a trailing "").
inline std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out(1);
    for (char c : s) {
        if (c == sep) out.emplace_back();
        else out.back() += c;
    }
    return out;
}

/// Throws unless the record has at least `n` fields.
inline void Need(const std::vector<std::string>& f, size_t n, const std::string& name) {
    if (f.size() < n) throw std::runtime_error(name + ": " + f[0] + " record has " + std::to_string(f.size()) + " fields, needs " + std::to_string(n));
}

inline Snapshot ParseSnapshot(const std::vector<std::string>& f, size_t at) {
    Snapshot s;
    s.feet = {std::stof(f[at]), std::stof(f[at + 1]), std::stof(f[at + 2])};
    s.vel = {std::stof(f[at + 3]), std::stof(f[at + 4]), std::stof(f[at + 5])};
    s.yaw = std::stof(f[at + 6]);
    s.pitch = std::stof(f[at + 7]);
    s.state = f[at + 8];
    s.crouched = f[at + 9] == "1";
    s.sprintCharge = std::stof(f[at + 10]);
    return s;
}

inline Trace Parse(std::istream& in, const std::string& name) {
    Trace t;
    t.name = name;
    std::string line;
    bool inWorld = false;
    std::optional<Snapshot> pendingSet;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (inWorld) {
            if (line == "end_world") { inWorld = false; continue; }
            if (line.rfind("fixture,", 0) == 0) { t.worlds.back().fixtures.push_back(line.substr(8)); continue; }
            const auto f = Split(line, ',');
            if (f[0] == "box") {
                Need(f, 7, name);
                t.worlds.back().boxes.push_back(runline::Aabb::New({std::stof(f[1]), std::stof(f[2]), std::stof(f[3])}, {std::stof(f[4]), std::stof(f[5]), std::stof(f[6])}));
            } else if (f[0] == "tri") {
                Need(f, 10, name);
                runline::Tri tri;
                for (int k = 0; k < 3; ++k) tri[k] = {std::stof(f[1 + 3 * k]), std::stof(f[2 + 3 * k]), std::stof(f[3 + 3 * k])};
                t.worlds.back().tris.push_back(tri);
            } else {
                throw std::runtime_error(name + ": unknown world record " + f[0]);
            }
            continue;
        }
        const auto f = Split(line, ',');
        if (f[0] == "trace") {
            if (f.size() < 2 || f[1] != "5") throw std::runtime_error(name + ": unsupported trace version (re-run make-traces.sh)");
        } else if (f[0] == "tuning_default") {
            Need(f, 2, name);
            t.defaultTuning = f[1] == "1";
        } else if (f[0] == "spawn") {
            Need(f, 5, name);
            t.spawn = {std::stof(f[1]), std::stof(f[2]), std::stof(f[3])};
            t.spawnYaw = std::stof(f[4]);
        } else if (f[0] == "state_debug") {
            t.initStateDebug = line.substr(12);
        } else if (f[0] == "ext") {
            if (t.frames.empty() || !t.frames.back().hasOut || t.frames.back().ext) throw std::runtime_error(name + ": ext record not after an out record");
            Need(f, 14, name);
            Ext x;
            x.eye = {std::stof(f[1]), std::stof(f[2]), std::stof(f[3])};
            x.meshOffset = std::stof(f[4]);
            if (f[5] == "1") x.left = runline::Vec3{std::stof(f[6]), std::stof(f[7]), std::stof(f[8])};
            if (f[9] == "1") x.right = runline::Vec3{std::stof(f[10]), std::stof(f[11]), std::stof(f[12])};
            if (!f[13].empty()) {
                for (const std::string& item : Split(f[13], ';')) {
                    const auto parts = Split(item, ':');
                    std::vector<float> values;
                    for (size_t k = 1; k < parts.size(); ++k) values.push_back(std::stof(parts[k]));
                    x.payloads.push_back({parts[0], values});
                }
            }
            t.frames.back().ext = x;
        } else if (f[0] == "fx") {
            if (t.frames.empty() || !t.frames.back().hid || t.frames.back().fx) throw std::runtime_error(name + ": fx record not after a hid record");
            Need(f, 13, name);
            Fx x;
            x.eye = {std::stof(f[1]), std::stof(f[2]), std::stof(f[3])};
            x.yaw = std::stof(f[4]);
            x.pitch = std::stof(f[5]);
            x.roll = std::stof(f[6]);
            x.fov = std::stof(f[7]);
            x.stepPhase = std::stof(f[8]);
            x.gait = std::stof(f[9]);
            x.shake = std::stof(f[10]);
            x.swanForward = std::stof(f[11]);
            x.swanDown = std::stof(f[12]);
            t.frames.back().fx = x;
        } else if (f[0] == "hid") {
            if (t.frames.empty() || !t.frames.back().ext || t.frames.back().hid) throw std::runtime_error(name + ": hid record not after an ext record");
            Need(f, 33, name);
            Hid h;
            const auto num = [&](size_t k) { return std::stof(f[k]); };
            const auto vec = [&](size_t k) { return runline::Vec3{num(k), num(k + 1), num(k + 2)}; };
            h.airTime = num(1);
            h.jumpedSinceGround = f[2] == "1";
            h.jumpBuffer = num(3);
            h.rollTriggerAge = num(4);
            h.takeoff = f[5];
            h.preJumpMomentum = num(6);
            h.lastJumpAt = vec(7);
            h.chainWallruns = static_cast<unsigned>(std::stoul(f[10]));
            h.climbedThisAir = f[11] == "1";
            if (f[12] == "1") h.lastWallNormal = vec(13);
            h.regrabCooldown = num(16);
            if (f[17] == "1") h.turn = Hid::Turn{num(18), num(19), f[20]};
            h.yawRate = num(21);
            h.frameTurn = num(22);
            h.eyeHeight = num(23);
            h.fallPeak = num(24);
            h.coiled = f[25] == "1";
            h.lowGrav = num(26);
            h.turnedInAir = f[27] == "1";
            h.slideYaw = num(28);
            h.softBrace = f[29] == "1";
            h.wallrunWallAbove = f[30] == "1";
            h.grabSpeed = num(31);
            h.hangTime = num(32);
            t.frames.back().hid = h;
        } else if (f[0] == "set") {
            Need(f, 12, name);
            pendingSet = ParseSnapshot(f, 1);
        } else if (f[0] == "init") {
            if (t.hasInit) throw std::runtime_error(name + ": two controllers in one trace");
            Need(f, 12, name);
            t.init = ParseSnapshot(f, 1);
            t.hasInit = true;
        } else if (f[0] == "world") {
            t.worlds.emplace_back();
            inWorld = true;
        } else if (f[0] == "in") {
            if (!t.hasInit || t.worlds.empty()) throw std::runtime_error(name + ": input before init/world");
            if (!t.frames.empty() && !t.frames.back().hasOut) throw std::runtime_error(name + ": frame with no output");
            Need(f, 14, name);
            Frame fr;
            fr.world = t.worlds.size() - 1;
            fr.set = pendingSet;
            pendingSet.reset();
            FrameInput& i = fr.in;
            i.dt = std::stof(f[1]);
            i.moveX = std::stof(f[2]);
            i.moveY = std::stof(f[3]);
            i.lookX = std::stof(f[4]);
            i.lookY = std::stof(f[5]);
            i.jumpPressed = f[6] == "1";
            i.jumpHeld = f[7] == "1";
            i.crouchPressed = f[8] == "1";
            i.crouchHeld = f[9] == "1";
            i.turnPressed = f[10] == "1";
            i.meleePressed = f[11] == "1";
            i.takedownPressed = f[12] == "1";
            i.strafeRaw = std::stof(f[13]);
            t.frames.push_back(fr);
        } else if (f[0] == "out") {
            if (t.frames.empty() || t.frames.back().hasOut) throw std::runtime_error(name + ": output with no input");
            Need(f, 12, name);
            Frame& fr = t.frames.back();
            fr.out = ParseSnapshot(f, 1);
            if (f.size() > 12 && !f[12].empty()) fr.events = Split(f[12], ';');
            fr.hasOut = true;
        } else {
            throw std::runtime_error(name + ": unknown record " + f[0]);
        }
    }
    if (!t.hasInit) throw std::runtime_error(name + ": no init record");
    if (!t.frames.empty() && !t.frames.back().hasOut) throw std::runtime_error(name + ": frame with no output");
    return t;
}

} // namespace trace
