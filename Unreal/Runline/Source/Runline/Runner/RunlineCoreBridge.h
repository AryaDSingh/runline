#pragma once
// RunlineCore's headers for Unreal code. The core is plain C++20 (std::optional, std::variant,
// exceptions for NotPorted) with its own warning settings, so it is included as third-party code.
// Nothing outside RunlineUnits.h may convert its coordinates.
THIRD_PARTY_INCLUDES_START
#include "runline/BoxWorld.h"
#include "runline/Controller.h"
THIRD_PARTY_INCLUDES_END
