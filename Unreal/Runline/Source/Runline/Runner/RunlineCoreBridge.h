#pragma once
// RunlineCore's headers for Unreal code. The core is plain C++20 (std::optional, std::variant,
// exceptions for NotPorted) with its own warning settings, so it is included as third-party code.
// Nothing outside RunlineUnits.h may convert its coordinates.
// The core is bit-exact with faith-runner only without fast-math reassociation (Runline.Build.cs
// sets FPSemantics; Game targets default to /fp:fast).
#if defined(_M_FP_FAST)
#error "Runline must be compiled with /fp:precise or /fp:strict (FPSemantics in Runline.Build.cs)"
#endif
THIRD_PARTY_INCLUDES_START
#include "runline/BoxWorld.h"
#include "runline/Camera.h"
#include "runline/Controller.h"
#include "runline/Look.h"
THIRD_PARTY_INCLUDES_END
