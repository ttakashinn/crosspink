#pragma once

#include <cstdint>

namespace vannhanso_cache {

inline constexpr const char* TEMP_PATH = "/.vannhanso-sleep.tmp";
inline constexpr const char* CACHE_PATH = "/vannhanso-sleep.bmp";
inline constexpr const char* BACKUP_PATH = "/.vannhanso-sleep.bak";

bool validateImage(const char* path, int screenWidth, int screenHeight);
void recoverInterruptedInstall(int screenWidth, int screenHeight);
bool installDownloadedImage(int screenWidth, int screenHeight);
const char* findRenderableImage(int screenWidth, int screenHeight);
bool readCurrentDate(int screenWidth, int screenHeight, uint32_t& dateKey);
bool writeCurrentDate(int screenWidth, int screenHeight, uint32_t dateKey);

}  // namespace vannhanso_cache
