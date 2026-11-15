// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "runtime/execution.hpp"
namespace workbench {
bool imageProcessingTool(Algorithm tool);
void validateProcessing(const Parameters& p);
Result executeProcessing(const Document& d);
QString processingFrame(const QString& input, const Parameters& p);
QJsonObject calibrationConfigJson(QJsonObject config, const QString& path,
                                  bool save);
mv::MvHomMat2D affineMatrix(const Parameters& p);
}  // namespace workbench
