// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "model/document.hpp"
#include <array>
namespace workbench {
struct ExposureStatistics {
  std::array<quint64, 256> histogram{};
  quint64 pixels = 0;
  int minimum = 0, maximum = 0;
  double mean = 0;
};
ExposureStatistics exposureStatistics(const mv::MvImage& image);
}  // namespace workbench
