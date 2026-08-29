#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class String {
 public:
  String() = default;
  String(const char* value) : value_(value ? value : "") {}
  const char* c_str() const { return value_.c_str(); }

 private:
  std::string value_;
};

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(const uint8_t* buffer, size_t size) = 0;
  virtual size_t write(uint8_t byte) = 0;
};
