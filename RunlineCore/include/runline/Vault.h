#pragma once
// Vaults and step-ups: TdMove_SpeedVault / TdMove_VaultOver. Port of faith_move::vault.
//
// The game picks one of six VaultTypes by how high the hand-plant is, whether you go onto the
// obstacle or over it, your speed, your vertical speed and how soon you'd reach it. Each type has
// its own animation and three timed phases: up to the hand-plant, over the ledge, down to where
// you land. You come out at the speed it took to cover that last stretch. It's checked every
// frame of a jump or fall while you hold forward, so a vault is "jump at it with W held".
#include <array>
#include <cstddef>
#include <optional>
#include <utility>
#include "runline/Move.h"
#include "runline/Tuning.h"
#include "runline/World.h"

namespace runline {

/// One entry of TdMove_SpeedVault.VaultTypes (defaults from TdGame.u; heights and speeds in
/// Unreal units, -1 = no limit).
struct VaultType {
    const char* anim;
    bool onto;
    float minHeight, maxHeight;
    float minSpeedZ, maxSpeedZ;
    float maxMomentum;
    float maxDistanceTime;
    float clampSpeedMin, clampSpeedMax;
    float speedAddition;
    float timeUp, timeOver, timeDown;
    /// HandplantOffset / LedgeOffset: (along the move, up) from the ledge, for the body centre.
    std::pair<float, float> handplant;
    std::pair<float, float> ledge;
};

inline constexpr std::array<VaultType, 6> VAULT_TYPES{{
    {"autostepuprightleg", true, 0.0f, 48.0f, -600.0f, 0.0f, -1.0f, 0.2f, 100.0f, 300.0f, 0.0f, 0.0f, 0.3f, 0.2f, {0.0f, 5.0f}, {0.0f, 90.0f}},
    {"stepuprightleg88", true, 48.0f, 148.0f, 0.0f, 700.0f, 200.0f, 0.4f, 200.0f, 700.0f, 0.0f, 0.0f, 0.4f, 0.25f, {0.0f, 0.0f}, {-20.0f, 60.0f}},
    {"VaultOnto", true, 64.0f, 148.0f, 0.0f, 10000.0f, -1.0f, 0.4f, 400.0f, 720.0f, 80.0f, 0.0f, 0.35f, 0.3f, {0.0f, 5.0f}, {0.0f, 25.0f}},
    {"VaultOver", false, 64.0f, 148.0f, 0.0f, 10000.0f, -1.0f, 0.4f, 400.0f, 720.0f, 80.0f, 0.0f, 0.35f, 0.3f, {0.0f, 5.0f}, {0.0f, 25.0f}},
    {"VaultOverHigh", false, 145.0f, 192.0f, 50.0f, 10000.0f, -1.0f, 0.4f, 200.0f, 400.0f, 0.0f, 0.28f, 0.3f, 0.45f, {-40.0f, -69.0f}, {0.0f, 5.0f}},
    {"VaultOntoHigh", true, 145.0f, 192.0f, 50.0f, 10000.0f, -1.0f, 0.4f, 200.0f, 400.0f, 0.0f, 0.27f, 0.3f, 0.6f, {-65.0f, -15.0f}, {0.0f, 35.0f}},
}};

/// A vault in progress. Positions are feet positions.
struct Vault {
    /// Index into VAULT_TYPES.
    std::size_t kind = 0;
    /// Seconds since the vault started (the animation plays from 0 at rate 1).
    float t = 0.0f;
    Vec3 from{};
    Vec3 hand{};
    Vec3 over{};
    Vec3 end{};
    Vec3 exitVel{};
    /// Landing past the obstacle with nothing under you (bEndMoveFalling).
    bool falling = false;
    /// Attack pressed during it: the way down becomes TdMove_MeleeVault's kick.
    bool kick = false;
    /// At the end before the kick's hit detection came on: she stays there until it's over.
    bool held = false;

    const VaultType& Kind() const { return VAULT_TYPES[kind]; }
    const char* Anim() const { return Kind().anim; }
    bool Onto() const { return Kind().onto; }
    float Duration() const;
    /// Feet position `t` seconds in (UpdateVaultMovement's SetPreciseLocation legs).
    Vec3 Position(float t) const;
};

/// TdMove_SpeedVault.CanDoMove (with CheckCollision, FindValidOntoEndLocation,
/// FindValidOverEndLocation, FindVaultFloor and UpdateActiveVaultType). `view` is the full view
/// direction (pitch included), `fwd` the facing on the ground plane.
std::optional<Vault> PlanVault(const World& world, const Tuning& tu, Body body, Vec3 feet, Vec3 vel, Vec3 fwd, Vec3 view);

} // namespace runline
