#pragma once
// The game build this library was recompiled from: C&C 3 Tiberium Wars (Steam, cnc3game.dat without SecuROM).
//
// Layout of the game folder:  <root>/RetailExe/<version>/cnc3game.dat, <root>/CNC3_<language>_<version>.SkuDef, ...
// (the Steam folder can carry several versions, e.g. 1.9 and 1.10)
// The guest sees <root> as C:\RA3 (the runtime's internal name for the game mount) and starts with that as its
// working directory, so the relative paths in the SkuDef files resolve against the game root.
//
// Game addresses: the RA3 runtime touched some places in the game directly (resolution list gate, frame limiter,
// options manager...). They are unknown for CC3 yet: 0 keeps every feature that needs one switched off.
#include <cstdint>

namespace gb {
constexpr const char* kExe = "cnc3game.dat";
constexpr const char* kVer = "1.9";                       // default version; the real one is the RetailExe/<version>
                                                           // folder the launcher picked (rt::config.gameVersion)
constexpr const char* kSkuPrefix = "CNC3";                 // CNC3_<language>_<version>.SkuDef
constexpr const char* kProfilesDir = "command & conquer 3 tiberium wars/profiles";   // under My Documents (lower case)

constexpr uint32_t ResModeCheck = 0;
constexpr uint32_t GfxOptionsList = 0;
constexpr uint32_t GfxOptionsList2 = 0;
constexpr uint32_t SetTextureWrapper = 0;
constexpr uint32_t ApplyStaticLOD = 0;
constexpr uint32_t LODManager = 0;
constexpr uint32_t FpsCap = 0, LogicHz = 0;
constexpr uint32_t LimiterRet = 0;
constexpr uint32_t ThreadWrap1 = 0, ThreadWrap2 = 0;
constexpr uint32_t OptionsMgr = 0, OptionsStore = 0;
constexpr uint32_t FnStrInit = 0, FnStrFree = 0, FnOptHas = 0, FnOptGet = 0, FnOptCount = 0;
constexpr bool kLodDiag = false;
}
