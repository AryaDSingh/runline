#pragma once
// The one place Unreal and RunlineCore coordinates meet (spec section 3, "Unit boundary").
//
// Core: metres, x right, y up, z back (right-handed); yaw 0 faces -Z, positive yaw turns left,
// positive pitch looks up. Unreal: centimetres, X forward, Y right, Z up (left-handed); positive
// yaw turns right. The map (x, y, z) -> (-z, x, y) keeps right, up and forward; it is a reflection
// (determinant -1), as a change of handedness must be. Positions round-trip exactly: a float
// times 100 and back in double recovers the float.
#include "CoreMinimal.h"
#include "RunlineCoreBridge.h"

namespace RunlineUnits
{
	inline constexpr double CmPerMetre = 100.0;

	inline FVector ToUnrealDirection(runline::Vec3 D) { return FVector(-D.z, D.x, D.y); }
	inline runline::Vec3 ToCoreDirection(const FVector& D) { return {float(D.Y), float(D.Z), float(-D.X)}; }
	inline FVector ToUnrealPosition(runline::Vec3 P) { return ToUnrealDirection(P) * CmPerMetre; }
	inline runline::Vec3 ToCorePosition(const FVector& P) { return {float(P.Y / CmPerMetre), float(P.Z / CmPerMetre), float(-P.X / CmPerMetre)}; }
	inline FVector ToUnrealVelocity(runline::Vec3 V) { return ToUnrealPosition(V); }
	/// Sizes (half extents): axes swap, no sign flips.
	inline FVector ToUnrealExtent(runline::Vec3 H) { return FVector(H.z, H.x, H.y) * CmPerMetre; }
	inline runline::Vec3 ToCoreExtent(const FVector& H) { return {float(H.Y / CmPerMetre), float(H.Z / CmPerMetre), float(H.X / CmPerMetre)}; }
	inline FBox ToUnrealBox(const runline::Aabb& B)
	{
		return FBox(FVector(-B.max.z, B.min.x, B.min.y) * CmPerMetre, FVector(-B.min.z, B.max.x, B.max.y) * CmPerMetre);
	}
	inline runline::Aabb ToCoreBox(const FBox& B)
	{
		return {{float(B.Min.Y / CmPerMetre), float(B.Min.Z / CmPerMetre), float(-B.Max.X / CmPerMetre)},
		        {float(B.Max.Y / CmPerMetre), float(B.Max.Z / CmPerMetre), float(-B.Min.X / CmPerMetre)}};
	}
	/// An Unreal yaw (degrees) as a core yaw (radians): the sign flips.
	inline float ToCoreYaw(double UnrealYawDegrees) { return float(-FMath::DegreesToRadians(UnrealYawDegrees)); }
	/// Core view angles (radians) to an Unreal rotator (degrees): yaw flips sign, pitch and roll keep it.
	inline FRotator ToUnrealRotator(float Yaw, float Pitch, float Roll = 0.0f)
	{
		return FRotator(FMath::RadiansToDegrees(double(Pitch)), FMath::RadiansToDegrees(-double(Yaw)), FMath::RadiansToDegrees(double(Roll)));
	}
}
