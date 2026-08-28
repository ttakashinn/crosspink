#pragma once

#include <string>

#include "PerBookReaderSettings.h"

namespace PerBookReaderSettingsStore {

constexpr char FILE_NAME[] = "reader-settings-vns.bin";
enum class LoadStatus { LOADED, LOADED_BACKUP, LOADED_TEMP, MISSING, NEWER_VERSION, INVALID, IO_ERROR };
enum class SaveStatus { SAVED, INVALID_SETTINGS, NEWER_VERSION, IO_ERROR };

LoadStatus load(const std::string& cachePath, PerBookReaderSettings& settings);
SaveStatus save(const std::string& cachePath, const PerBookReaderSettings& settings);

}  // namespace PerBookReaderSettingsStore
