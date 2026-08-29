#pragma once

#include "BookStub.h"

class Epub : public BookStub {
 public:
  Epub(std::string path, std::string cacheRoot) : BookStub(std::move(path), std::move(cacheRoot), "epub") {}
};
