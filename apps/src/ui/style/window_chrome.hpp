// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QWidget>
namespace workbench {
class WindowChrome final : public QWidget {
 public:
  explicit WindowChrome(QWidget* parent);

 protected:
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void mouseDoubleClickEvent(QMouseEvent*) override;

 private:
  QPoint offset_;
  bool dragging_ = false;
};
// Restore native sizing for the frameless main window, retaining its custom client area.
void enableStudioNativeResize(QWidget* window);
bool handleStudioNativeEvent(QWidget* window, void* message, long* result);
}  // namespace workbench
