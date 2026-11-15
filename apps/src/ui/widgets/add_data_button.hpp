// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QPushButton>
#include <QToolButton>
#include "ui/style/appearance.hpp"
namespace workbench {
// The main surface adds to the current input; the corner is keyboard-focusable.
class AddDataButton final : public QPushButton {
 public:
  explicit AddDataButton(QWidget* parent) : QPushButton(studioIcon("plus"),QString(),parent) {
    choices_=new QToolButton(this);
    choices_->setObjectName("addDataTypesButton");
    choices_->setIcon(studioIcon("corner-menu"));
    choices_->setIconSize(QSize(12,12));
    choices_->setFixedSize(14,14);
    choices_->setFocusPolicy(Qt::StrongFocus);
    choices_->setPopupMode(QToolButton::InstantPopup);
    choices_->setToolTip(QStringLiteral("输入数据操作"));
    choices_->setAccessibleName(QStringLiteral("输入数据操作"));
  }
  void setDataMenu(QMenu* menu) { choices_->setMenu(menu); }
 protected:
  void resizeEvent(QResizeEvent* event) override {
    QPushButton::resizeEvent(event);
    choices_->move(width()-choices_->width(),height()-choices_->height());
  }
 private:
  QToolButton* choices_;
};
} // namespace workbench
