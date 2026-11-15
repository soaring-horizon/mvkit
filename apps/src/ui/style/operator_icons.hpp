// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QString>
namespace workbench {
enum class Algorithm;
// One visual identity per tool, shared by the full and recent operator lists.
QString operatorIconName(Algorithm algorithm);
} // namespace workbench
