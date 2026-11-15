// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QtGlobal>
namespace workbench {
// Bound malformed metadata allocation, not the number of user samples.
// Writers and readers use the same limit, including project configurations.
constexpr qint64 metadataByteLimit = 128LL * 1024 * 1024;
}
