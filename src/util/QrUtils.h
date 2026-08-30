#pragma once

#include <GfxRenderer.h>

#include <cstddef>
#include <string>

#include "components/themes/BaseTheme.h"

namespace QrUtils {

inline constexpr size_t MAX_PAYLOAD_BYTES = 2953;  // Version 40, ECC_LOW, byte mode.

// Renders a QR code with the given text payload within the specified bounding box.
void drawQrCode(const GfxRenderer& renderer, const Rect& bounds, const std::string& textPayload);

}  // namespace QrUtils
