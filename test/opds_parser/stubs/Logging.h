#pragma once

template <typename... Args>
inline void opdsParserTestLog(const Args&...) {}

#define LOG_DBG(...) opdsParserTestLog(__VA_ARGS__)
