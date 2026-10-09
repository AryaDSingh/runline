#pragma once
// Shared by the golden tests: reading the CSVs tools/golden writes, loading its scenes, and
// reporting the first mismatches by row and field.
#include <doctest/doctest.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include "runline/BoxWorld.h"
#include "runline/MeshWorld.h"

namespace golden {

using Row = std::vector<float>;
using Rows = std::vector<Row>;

inline Rows ReadCsv(const std::string& name) {
    std::ifstream in(std::string(RUNLINE_GOLDEN_DIR) + "/" + name);
    Rows rows;
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        Row row;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, ',')) row.push_back(std::stof(cell));
        rows.push_back(row);
    }
    return rows;
}

/// The rows of `name`, failing loudly if it's missing, short, or a row has the wrong width.
inline Rows Require(const std::string& name, size_t minRows, size_t cols) {
    Rows rows = ReadCsv(name);
    REQUIRE_MESSAGE(rows.size() >= minRows, name << " missing or short; run cargo run --release in tools/golden");
    for (const Row& r : rows)
        REQUIRE_MESSAGE(r.size() == cols, name << " has a row of width " << r.size() << ", expected " << cols);
    return rows;
}

inline runline::Vec3 V(const Row& r, size_t i) { return {r[i], r[i + 1], r[i + 2]}; }

/// The box scene tools/golden used (scene.csv), so it can't drift from the Rust side.
inline runline::BoxWorld BoxScene() {
    runline::BoxWorld w;
    for (const Row& r : Require("scene.csv", 1, 6)) w.Add(runline::Aabb::New(V(r, 0), V(r, 3)));
    return w;
}

/// The triangle scene tools/golden used (mesh_scene.csv).
inline runline::MeshWorld MeshScene() {
    runline::MeshWorld w;
    for (const Row& r : Require("mesh_scene.csv", 1, 9)) w.tris.push_back(runline::Tri{V(r, 0), V(r, 3), V(r, 6)});
    return w;
}

inline bool Same(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }
inline bool Near(float a, float b) { return std::fabs(a - b) <= 1e-5f; }

/// Collects mismatches; the first ten are logged with their row and field.
class Report {
public:
    void Add(size_t row, const std::string& what) {
        if (++count_ <= 10) MESSAGE("row " << row << " (csv line " << row + 2 << "): " << what);
    }
    void Float(size_t row, const std::string& field, float want, float got) {
        if (!Near(want, got)) Add(row, field + ": want " + std::to_string(want) + ", got " + std::to_string(got));
    }
    void Flag(size_t row, const std::string& field, float want, bool got) {
        const bool w = want != 0.0f;
        if (w != got) Add(row, field + ": want " + (w ? "1" : "0") + ", got " + (got ? "1" : "0"));
    }
    void Vec(size_t row, const std::string& field, const Row& r, size_t i, runline::Vec3 got) {
        Float(row, field + ".x", r[i], got.x);
        Float(row, field + ".y", r[i + 1], got.y);
        Float(row, field + ".z", r[i + 2], got.z);
    }
    void Finish() const { CHECK(count_ == 0); }

private:
    int count_ = 0;
};

/// Compare a sweep result with the 8 columns at `at`: hit, t, normal xyz, point xyz.
inline void CompareHit(Report& rep, size_t row, const std::optional<runline::SweepHit>& h, const Row& r, size_t at) {
    rep.Flag(row, "hit", r[at], h.has_value());
    if (!h || r[at] == 0.0f) return;
    rep.Float(row, "t", r[at + 1], h->t);
    if (std::signbit(r[at + 1]) != std::signbit(h->t)) rep.Add(row, "t: sign of zero differs from Rust");
    rep.Vec(row, "normal", r, at + 2, h->normal);
    rep.Vec(row, "point", r, at + 5, h->point);
}

} // namespace golden
