#pragma once

#include <string>
#include <vector>

#include "ClippingCodec.h"

namespace ClippingStore {

enum class LoadStatus { LOADED, LOADED_BACKUP, LOADED_TEMP, MISSING, NEWER_VERSION, INVALID, IO_ERROR };
enum class SaveStatus { SAVED, NEWER_VERSION, INVALID, IO_ERROR };

LoadStatus load(const std::string& cachePath, std::vector<ClippingCodec::Record>& records);
SaveStatus save(const std::string& cachePath, const std::vector<ClippingCodec::Record>& records);

}  // namespace ClippingStore
