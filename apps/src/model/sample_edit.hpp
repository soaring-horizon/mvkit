// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "model/document.hpp"
namespace workbench {
// Returns a staged replacement; validation never changes the original sample.
Document withSampleInput(const Document& original,const QVector<Parameters>& workflow,const QString& binding,const SourceData& input);
}
