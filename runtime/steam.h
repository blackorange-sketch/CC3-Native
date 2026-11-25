#pragma once
// Stand-in steam_api.dll (steam.cpp)
#include <cstdint>
#include <string>

namespace steam
{
constexpr uint32_t kAppId = 24790;   // Command & Conquer 3: Tiberium Wars on Steam
constexpr const char* kGameName = "Command & Conquer 3: Tiberium Wars";
constexpr const char* kSteamDir = "C:\\Steam";   // Steam folder the registry reports (misc.cpp)
// the game folder as seen through the Steam library (rt.cpp hostPath maps it to the real game folder)
constexpr const char* kInstallDir = "C:\\Steam\\steamapps\\common\\Command and Conquer 3 Tiberium Wars";
constexpr const char* kLibraryPath = "steam/steamapps/common/command and conquer 3 tiberium wars";   // the same, host side
uint32_t loadLibrary(const std::string& name);                  // module handle for steam_api.dll, else 0
uint32_t procAddress(uint32_t module, const std::string& name);  // export of that module, else 0
}
