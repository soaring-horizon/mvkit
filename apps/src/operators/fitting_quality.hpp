// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "runtime/execution.hpp"
namespace workbench {
void fittingQuality(ResultObject& object, const mv::MvTuple& rows,
                    const mv::MvTuple& columns, double tolerance);
}
