#include "runline/Vault.h"
#include <cmath>
#include "runline/Probes.h"

namespace runline {
namespace {

/// Faith's collision cylinder: CollisionRadius 30, CollisionHeight 90 (half height), so
/// MoveLedgeLocation.Z - Location.Z + CollisionHeight is the ledge height above the feet.
constexpr float RADIUS = Uu(30.0f);
constexpr float HALF_HEIGHT = Uu(90.0f);
/// TdMove_SpeedVault.MaxTimeToLedge and VaultClearObjectHeight.
constexpr float MAX_TIME_TO_LEDGE = 0.4f;
constexpr float CLEAR_OBJECT_HEIGHT = Uu(35.0f);

Vec3 Horiz(Vec3 v) { return {v.x, 0.0f, v.z}; }
Vec3 Lerp(Vec3 a, Vec3 b, float s) { return a * (1.0f - s) + b * s; }   // glam's lerp

/// Sweep a box from `start` along `dir` for `dist` and return how far it gets before it runs into
/// something. Like the game's extent traces, anything it starts inside doesn't count; nor does
/// floor it only skims (the bottom 10 cm of the box is left out).
std::optional<float> Sweep(const World& world, Vec3 start, Vec3 dir, float dist, Vec3 half) {
    const Vec3 h{half.x, FMax(half.y - 0.05f, 0.01f), half.z};
    const Vec3 s = start + Vec3::UnitY() * 0.05f;
    const auto hit = world.Sweep(h, s, dir * dist);
    if (!hit) return std::nullopt;
    return hit->t * dist;
}

} // namespace

float Vault::Duration() const {
    const VaultType& k = Kind();
    return k.timeUp + k.timeOver + k.timeDown;
}

Vec3 Vault::Position(float tt) const {
    const VaultType& k = Kind();
    const float up = k.timeUp, overT = k.timeOver, down = k.timeDown;
    if (tt < up) return Lerp(from, hand, tt / up);
    if (tt < up + overT) {
        const Vec3 start = up > 0.0f ? hand : from;
        return Lerp(start, over, (tt - up) / overT);
    }
    return Lerp(over, end, FMin((tt - up - overT) / down, 1.0f));
}

std::optional<Vault> PlanVault(const World& world, const Tuning& tu, Body body, Vec3 feet, Vec3 vel, Vec3 fwd, Vec3 view) {
    const float speed = Horiz(vel).Length();
    // The ledge in front of you, close enough to reach in MaxTimeToLedge (at least 300 uu/s).
    const float reach = MAX_TIME_TO_LEDGE * FMax(speed, Uu(300.0f));
    std::optional<WallHit> hit;
    float y = 0.05f;
    while (y < Uu(192.0f)) {
        if (const auto h = TraceWall(world, body, feet, fwd, reach, y)) {
            hit = h;
            break;
        }
        y += 0.1f;
    }
    if (!hit) return std::nullopt;
    const Vec3 n = hit->n;
    // Facing it: vector(Controller.Rotation) . MoveNormal <= -0.2.
    if (view.Dot(n) > -0.2f) return std::nullopt;
    const Vec3 inside = hit->at - n * 0.02f;
    const float top = ColumnTop(world, inside, feet.y + y, feet.y + Uu(192.0f) + 0.05f);
    const float height = (top - feet.y) * 100.0f;
    if (!(height >= 0.0f && height <= 192.0f)) return std::nullopt;
    const Vec3 ledge{hit->at.x, top, hit->at.z};
    const Vec3 toLedge = Horiz(ledge - feet);
    const Vec3 dir = toLedge.NormalizeOrZero();
    if (dir == Vec3::Zero()) return std::nullopt;
    const float timeToHandplant = toLedge.Length() / FMax(speed, Uu(300.0f));
    if (timeToHandplant > MAX_TIME_TO_LEDGE) return std::nullopt;
    const Body stand{tu.halfWidth, tu.standHeight};
    Vec3 end = ledge + dir * FMax(Uu(48.0f), speed * 0.3f);

    // FindValidOntoEndLocation: room on top, at least 32 (low) or 64 units deep.
    const Vec3 ext{RADIUS * 1.4f, HALF_HEIGHT * 0.5f, RADIUS * 1.4f};
    const float ontoY = top + CLEAR_OBJECT_HEIGHT + ext.y;
    const float span = Horiz(end - ledge).Length();
    Vec3 ontoEnd{end.x, top, end.z};
    if (const auto s = Sweep(world, Vec3{ledge.x, ontoY, ledge.z}, dir, span, ext)) {
        const float width = *s + ext.x;
        if (width < (height <= 48.0f ? Uu(32.0f) : Uu(64.0f))) return std::nullopt;
        ontoEnd = Vec3{ledge.x, top, ledge.z} + dir * (width - RADIUS);
    }
    // CheckCollision.
    end = Vec3{ontoEnd.x, end.y, ontoEnd.z};
    const float ontoDist = FMin(Horiz(ontoEnd - ledge).Length(), Uu(160.0f));
    ontoEnd = Vec3{ledge.x, top, ledge.z} + dir * ontoDist;
    const float maxLedgeWidth = FClamp(speed * 0.2f, Uu(60.0f), Uu(180.0f));
    // FindValidOverEndLocation: from the far end back toward the ledge, just under its top.
    const Vec3 overExt{RADIUS, Uu(48.0f), RADIUS};
    const float overY = top - overExt.y;
    const float back = Horiz(end - ledge).Length();
    const Vec3 far = Vec3{ledge.x, overY, ledge.z} + dir * back;
    bool onto;
    if (const auto d = Sweep(world, far, -dir, back, overExt)) {
        // Hit the obstacle's far side: over if that's within MaxLedgeWidth of the ledge.
        onto = back - *d >= maxLedgeWidth;
    } else {
        onto = true;
    }
    end = onto ? ontoEnd : Vec3{end.x, top, end.z};
    // FindVaultFloor: a floor under the end, from 32 above the ledge down to 32 (onto) or 240
    // (over) below it. Landing no more than 64 below the ledge counts as onto.
    const float drop = onto ? Uu(32.0f) : Uu(240.0f);
    const auto floors = TopsBelow(world, end, tu.halfWidth, top + Uu(32.0f), top - drop);
    bool falling = false;
    if (!floors.empty()) {
        const float f = floors.front();
        onto = f > top - Uu(64.0f);
        end.y = f;
    } else {
        falling = !onto;
    }
    if (!world.IsFree(stand.AabbAt(end + Vec3::UnitY() * 0.01f))) return std::nullopt;
    // The way there is clear too: crouched, across the top from the lip and down to the end.
    const Vec3 crouchHalf{tu.halfWidth * 0.9f, tu.crouchHeight * 0.5f, tu.halfWidth * 0.9f};
    const Vec3 lip = Vec3{ledge.x, top + 0.05f, ledge.z} + dir * 0.05f + Vec3::UnitY() * crouchHalf.y;
    const Vec3 above{end.x, lip.y, end.z};
    const Vec3 across = above - lip;
    const Vec3 down = Vec3{end.x, end.y + 0.02f, end.z} + Vec3::UnitY() * crouchHalf.y - above;
    if (world.Sweep(crouchHalf, lip, across).has_value() || (down.y < -0.01f && world.Sweep(crouchHalf, above, down).has_value()))
        return std::nullopt;

    // UpdateActiveVaultType: the first type that fits.
    const float momentum = speed * 100.0f;
    const float vz = vel.y * 100.0f;
    std::optional<std::size_t> kind;
    for (std::size_t i = 0; i < VAULT_TYPES.size(); ++i) {
        const VaultType& t = VAULT_TYPES[i];
        if (height >= t.minHeight && height <= t.maxHeight && (t.onto == onto || i == 0) &&
            (t.maxMomentum < 0.0f || momentum <= t.maxMomentum) && vz >= t.minSpeedZ && vz <= t.maxSpeedZ &&
            timeToHandplant <= t.maxDistanceTime) {
            kind = i;
            break;
        }
    }
    if (!kind) return std::nullopt;
    const VaultType& k = VAULT_TYPES[*kind];
    // The step-up (type 0) takes either; going onto, it ends on top.
    if (!onto && k.onto) end = ontoEnd;
    onto = k.onto;
    const float clamped = Uu(FClamp(momentum + k.speedAddition, k.clampSpeedMin, k.clampSpeedMax));
    if (onto) {
        const float d = FMin(Horiz(end - ledge).Length(), FMax(Uu(48.0f), clamped * k.timeDown));
        end = Vec3{ledge.x, end.y, ledge.z} + dir * d;
        if (!falling && !world.IsFree(stand.AabbAt(end + Vec3::UnitY() * 0.01f))) return std::nullopt;
    }

    const auto at = [&](std::pair<float, float> o) { return Vec3{ledge.x, top + Uu(o.second) - HALF_HEIGHT, ledge.z} + dir * Uu(o.first); };
    const Vec3 over = at(k.ledge);
    const float exitSpeed = Horiz(end - over).Length() / k.timeDown;
    Vault v;
    v.kind = *kind;
    v.from = feet;
    v.hand = at(k.handplant);
    v.over = over;
    v.end = end;
    v.exitVel = dir * exitSpeed;
    v.falling = falling && !onto;
    return v;
}

} // namespace runline
