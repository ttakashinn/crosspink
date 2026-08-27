#pragma once

// Called by the automatic updater after an update requested by the sleep flow
// finishes (successfully, unsuccessfully, or by being skipped). main.cpp then
// resumes the deferred deep-sleep transition outside ActivityManager::loop().
void vanNhanSoUpdateFinishedBeforeSleep();
