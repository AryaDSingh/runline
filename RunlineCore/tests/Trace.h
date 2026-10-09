#pragma once
// Reads the per-frame traces tools/trace/make-traces.sh records from faith-runner's own tests:
// the geometry, the state before the first step, and each frame's input and resulting state.
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <fstream>
#include <istream>
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

struct Frame {
    size_t world = 0;   // index into Trace::worlds
    FrameInput in;
    Snapshot out;
    std::vector<std::string> events;
    bool hasOut = false;
    /// What a test set the controller to just before this step, if it changed it between steps.
    std::optional<Snapshot> set;
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
            if (f.size() < 2 || f[1] != "2") throw std::runtime_error(name + ": unsupported trace version (re-run make-traces.sh)");
        } else if (f[0] == "tuning_default") {
            Need(f, 2, name);
            t.defaultTuning = f[1] == "1";
        } else if (f[0] == "spawn") {
            Need(f, 5, name);
            t.spawn = {std::stof(f[1]), std::stof(f[2]), std::stof(f[3])};
            t.spawnYaw = std::stof(f[4]);
        } else if (f[0] == "state_debug") {
            t.initStateDebug = line.substr(12);
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

inline std::string Dir() { return std::string(RUNLINE_GOLDEN_DIR) + "/traces"; }

inline Trace Load(const std::string& name) {
    std::ifstream in(Dir() + "/" + name + ".trace");
    REQUIRE_MESSAGE(in.good(), name << ".trace missing; run bash tools/trace/make-traces.sh");
    return Parse(in, name);
}

/// The names of every committed trace, sorted.
inline std::vector<std::string> All() {
    std::vector<std::string> names;
    for (const auto& e : std::filesystem::directory_iterator(Dir()))
        if (e.path().extension() == ".trace") names.push_back(e.path().stem().string());
    std::sort(names.begin(), names.end());
    return names;
}

inline runline::BoxWorld Boxes(const WorldDump& d) {
    runline::BoxWorld w;
    for (const auto& b : d.boxes) w.Add(b);
    return w;
}

inline runline::MeshWorld Mesh(const WorldDump& d) {
    runline::MeshWorld w;
    w.tris = d.tris;
    return w;
}

} // namespace trace
