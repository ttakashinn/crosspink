#pragma once

#include "BookStub.h"

class Xtc : public BookStub {
 public:
  Xtc(std::string path, std::string cacheRoot) : BookStub(std::move(path), std::move(cacheRoot), "xtc") {}
};
