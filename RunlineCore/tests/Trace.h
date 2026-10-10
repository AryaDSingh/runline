#pragma once
// Reads the per-frame traces tools/trace/make-traces.sh records from faith-runner's own tests.
// The format and parser are in TraceFormat.h (no doctest, so Unreal's tests can use them too).
#include <doctest/doctest.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include "TraceFormat.h"

namespace trace {

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
