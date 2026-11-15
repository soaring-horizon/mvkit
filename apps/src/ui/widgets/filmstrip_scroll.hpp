// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>
namespace workbench {
class FilmstripScroll final : public QScrollArea {
 public:
  using QScrollArea::QScrollArea;
 protected:
  void wheelEvent(QWheelEvent* event) override {
    auto* bar = horizontalScrollBar();
    if (bar->maximum() <= bar->minimum() || event->modifiers().testFlag(Qt::ControlModifier)) {
      QScrollArea::wheelEvent(event); return;
    }
    const auto pixels = event->pixelDelta(), angle = event->angleDelta();
    const int delta = !pixels.isNull() ? (pixels.x() ? pixels.x() : pixels.y())
        : qRound((angle.x() ? angle.x() : angle.y()) / 120. * 147);
    if (!delta) { event->ignore(); return; }
    bar->setValue(bar->value() - delta);
    event->accept();
  }
};
}
