// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QSpinBox>
#include <QTimer>
#include <QWheelEvent>

namespace workbench {
// Treat the first click as replacing a value, while subsequent clicks retain
// native caret/selection behavior. Scrolling the panel must not edit values.
template <class Base>
class ParameterSpinBoxBase final : public Base {
 public:
  explicit ParameterSpinBoxBase(QWidget* parent = nullptr) : Base(parent) {
    this->setFocusPolicy(Qt::StrongFocus);
    this->setKeyboardTracking(false);
  }

 protected:
  void focusInEvent(QFocusEvent* event) override {
    Base::focusInEvent(event);
    if (event->reason() == Qt::MouseFocusReason)
      QTimer::singleShot(0, this, [this] {
        // Select after the initiating mouse press has positioned the caret.
        if (this->hasFocus()) this->selectAll();
      });
  }
  void wheelEvent(QWheelEvent* event) override {
    if (!this->hasFocus()) {
      event->ignore();
      return;
    }
    Base::wheelEvent(event);
  }
};
using ParameterDoubleSpinBox = ParameterSpinBoxBase<QDoubleSpinBox>;
using ParameterSpinBox = ParameterSpinBoxBase<QSpinBox>;
}  // namespace workbench
