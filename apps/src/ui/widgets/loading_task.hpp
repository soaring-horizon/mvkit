// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <exception>
namespace workbench {
class LoadingCancelled final : public std::exception {
 public:
  const char* what() const noexcept override {return "loading cancelled";}
};
}
