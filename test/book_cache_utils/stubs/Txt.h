#pragma once

#include "BookStub.h"

class Txt : public BookStub {
 public:
  Txt(std::string path, std::string cacheRoot) : BookStub(std::move(path), std::move(cacheRoot), "txt") {}
};
