// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "runtime/execution.hpp"
namespace workbench {
QJsonObject readCalibrationArtifact(const QByteArray& bytes);
QByteArray loadCalibrationFile(const QString& path);
void saveCalibrationFile(const QByteArray& bytes, const QString& path);
Result executeCalibrationJob(const Document& d);
Result executeWorkflowCalibration(const Document& d, const SourceData& source, const QString& binding);
QByteArray workflowCalibrationModel(const Document& d, int target, const QVector<NodeOutput>& outputs);
Result detectCalibrationBoard(const Document& d);
Result rectifyCalibrationImage(const Document& d);
Result executePlaneMeasure(const Document& d, const NodeOutput* a,
                           const NodeOutput* b);
}  // namespace workbench
