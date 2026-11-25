#pragma once
#include <cstdint>
// The game this library is recompiled from: C&C 3 Tiberium Wars, Steam release (cnc3game.dat without SecuROM).
//
// Layout of the game folder:  <root>/RetailExe/<version>/cnc3game.dat, <root>/CNC3_<language>_<version>.SkuDef, ...
// (the Steam folder can carry several versions, e.g. 1.9 and 1.10; the launcher picks the one this build matches).
// The guest sees <root> as C:\CC3 and starts with that as its working directory, so the relative paths in the
// SkuDef files resolve against the game root.

namespace gb {
constexpr const char* kExe = "cnc3game.dat";
constexpr const char* kVer = "1.9";                       // default; the real one is rt::config.gameVersion
// cnc3game.dat 1.10 (Steam, SHA af568a89...): the graphics options' display mode check for the resolution list
// (bpp >= 32, at least 800x600, aspect 4:3, 5:4, 16:9 or 16:10) - gated (CC3_GATES in the workflow)
constexpr uint32_t ResModeCheck = 0x0044ed42;
constexpr const char* kSkuPrefix = "CNC3";                 // CNC3_<language>_<version>.SkuDef
constexpr const char* kProfilesDir = "command & conquer 3 tiberium wars/profiles";   // under My Documents (lower case)
}
