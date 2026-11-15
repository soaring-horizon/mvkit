// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "model/document.hpp"
namespace workbench {
// Canonical, frozen input view for new columns and legacy sample roots.
QMap<QString, SourceData> sampleInputs(const Document& document);
QString sampleKind(const Document& document);
QJsonObject sampleProvenance(const Document& document, const QString& identity);
void applyLegacyInputOverrides(Document& document, bool requireComplete = true);
}
