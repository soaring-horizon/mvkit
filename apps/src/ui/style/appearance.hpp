// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
class QApplication;
#include <QColor>
#include <QIcon>
#include <QString>
namespace workbench {
void applyAppearance(QApplication& app, bool dark = false);
QIcon studioIcon(const QString& name, const QColor& color = QColor("#748197"));
}  // namespace workbench
