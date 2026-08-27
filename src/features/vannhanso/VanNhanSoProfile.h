#pragma once

#include <cstddef>
#include <cstdint>

namespace vannhanso_profile {

inline constexpr size_t QUERY_MAX_LENGTH = 128;
inline constexpr size_t ID_MAX_LENGTH = 144;
inline constexpr size_t PATH_MAX_LENGTH = 96;
inline constexpr const char* CACHE_DIRECTORY = "/.crosspoint/vannhanso-cache";

bool buildQuery(char* output, size_t outputSize);
bool buildIdentity(int screenWidth, int screenHeight, char* output, size_t outputSize);
uint32_t identityHash(int screenWidth, int screenHeight);
bool buildImagePath(int screenWidth, int screenHeight, char* output, size_t outputSize);
bool buildBackupPath(int screenWidth, int screenHeight, char* output, size_t outputSize);
bool buildDatePath(int screenWidth, int screenHeight, char* output, size_t outputSize);
bool buildManifestUrl(const char* baseUrl, char* output, size_t outputSize);

}  // namespace vannhanso_profile
