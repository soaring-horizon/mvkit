// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QToolButton>
#include "ui/style/appearance.hpp"
namespace workbench {
inline QToolButton* studioIconButton(const QString& name, const QString& label,
                        QWidget* parent) {
  auto* b = new QToolButton(parent);
  b->setIcon(studioIcon(name));
  b->setToolTip(label);
  b->setAccessibleName(label);
  return b;
}
}
