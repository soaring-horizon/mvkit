// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QImage>
#include <QPainterPath>
#include <mvkit/cpp/mvkit.hpp>

namespace workbench {
QPainterPath imageDomainPath(const mv::MvImage& image);
QImage displayComposite(const QImage& original, const QImage& processed,
                        const QPainterPath& domain);
}  // namespace workbench
