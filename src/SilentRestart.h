#pragma once

#include "activities/network/NetworkMode.h"

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session.

void silentRestart();                      // home screen
void silentRestartToReader();              // currently-open EPUB (APP_STATE.openEpubPath)
void silentRestartToReaderForLowMemory();  // force heap-defrag reboot, including touch boards
// Reader settings after Manage Fonts. Keeps the current e-ink frame until the
// destination's first paint, avoiding a throwaway popup refresh before reboot.
void silentRestartToReaderSettings();
void silentRestartToVanNhanSoSettings();  // Văn Nhân Số settings
// C3 File Transfer entry: reboot into a clean, reader-resource-free heap before
// allocating the WiFi driver and HTTP/WebSocket services.
void silentRestartToFileTransfer(NetworkMode mode);
bool bootWasLowMemoryRestart();

// Reboots immediately after an activity releases exclusive raw storage. The
// RTC target ensures setup() lands on Home instead of resuming a reader.
void restartToHomeAfterStorageHandoff();
