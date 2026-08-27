#pragma once

namespace vannhanso_cache {

inline constexpr const char* TEMP_PATH = "/.vannhanso-sleep.tmp";
inline constexpr const char* CACHE_PATH = "/vannhanso-sleep.bmp";
inline constexpr const char* BACKUP_PATH = "/.vannhanso-sleep.bak";

bool validateImage(const char* path, int screenWidth, int screenHeight);
void recoverInterruptedInstall(int screenWidth, int screenHeight);
bool installDownloadedImage(int screenWidth, int screenHeight);
const char* findRenderableImage(int screenWidth, int screenHeight);

}  // namespace vannhanso_cache
