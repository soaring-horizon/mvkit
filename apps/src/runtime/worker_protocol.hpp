// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "runtime/execution.hpp"
namespace workbench {
void writeWorkerRequest(const QString& path, const Document& document);
Document readWorkerRequest(const QString& path);
void writeWorkerResult(const QString& path, const Result& result);
Result readWorkerResult(const QString& path, const Document& input);
QByteArray encodeImage(const mv::MvImage& image);
mv::MvImage decodeImage(const QByteArray& bytes);
}  // namespace workbench
