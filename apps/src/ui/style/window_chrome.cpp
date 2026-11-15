// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/style/window_chrome.hpp"

#include <QMouseEvent>
#include <QGuiApplication>
#include <QWindow>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <windowsx.h>
#endif
namespace workbench {
WindowChrome::WindowChrome(QWidget* parent) : QWidget(parent) {
  setObjectName("titleBar");
  setFixedHeight(40);
}
void WindowChrome::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    if (window()->windowHandle() &&
        window()->windowHandle()->startSystemMove()) {
      event->accept();
      return;
    }
    offset_ = event->globalPos() - window()->frameGeometry().topLeft();
    dragging_ = true;
    event->accept();
  } else
    QWidget::mousePressEvent(event);
}
void WindowChrome::mouseMoveEvent(QMouseEvent* event) {
  if (dragging_) {
    window()->move(event->globalPos() - offset_);
    event->accept();
  } else
    QWidget::mouseMoveEvent(event);
}
void WindowChrome::mouseReleaseEvent(QMouseEvent* event) {
  dragging_ = false;
  QWidget::mouseReleaseEvent(event);
}
void WindowChrome::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    if (window()->isMaximized())
      window()->showNormal();
    else
      window()->showMaximized();
    event->accept();
  } else
    QWidget::mouseDoubleClickEvent(event);
}
void enableStudioNativeResize(QWidget* window) {
#ifdef Q_OS_WIN
  if(QGuiApplication::platformName()!=QStringLiteral("windows"))return;
  const auto hwnd=reinterpret_cast<HWND>(window->winId());
  const auto style=GetWindowLongPtrW(hwnd,GWL_STYLE);
  // Qt 5.15.2 omits WS_THICKFRAME for FramelessWindowHint. Hit results alone
  // cannot enable Windows' sizing loop. Keep the frame capability, not its UI.
  if(!(style & WS_THICKFRAME)) {
    SetWindowLongPtrW(hwnd,GWL_STYLE,style | WS_THICKFRAME);
    SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|
                                   SWP_NOACTIVATE|SWP_FRAMECHANGED);
  }
#else
  Q_UNUSED(window);
#endif
}
bool handleStudioNativeEvent(QWidget* window, void* message, long* result) {
#ifdef Q_OS_WIN
  const auto* msg = static_cast<MSG*>(message);
  if(msg->message==WM_NCCALCSIZE) {
    // The custom header and contents occupy the whole native window, including
    // after restore/maximize; do not let WS_THICKFRAME add a system title/frame.
    *result=0;return true;
  }
  if (msg->message == WM_NCHITTEST && !window->isMaximized() && !window->isFullScreen()) {
    RECT rect;
    if (!GetWindowRect(msg->hwnd, &rect)) return false;
    const int border = qRound(8 * window->devicePixelRatioF());
    const int corner = qRound(24 * window->devicePixelRatioF());
    const int x = GET_X_LPARAM(msg->lParam), y = GET_Y_LPARAM(msg->lParam);
    if(x<rect.left || x>=rect.right || y<rect.top || y>=rect.bottom)return false;
    const bool left = x < rect.left + border, right = x >= rect.right - border,
               top = y < rect.top + border, bottom = y >= rect.bottom - border;
    if(x>=rect.right-corner && y>=rect.bottom-corner)
      *result=HTBOTTOMRIGHT;
    else if (top && left)
      *result = HTTOPLEFT;
    else if (top && right)
      *result = HTTOPRIGHT;
    else if (bottom && left)
      *result = HTBOTTOMLEFT;
    else if (bottom && right)
      *result = HTBOTTOMRIGHT;
    else if (left)
      *result = HTLEFT;
    else if (right)
      *result = HTRIGHT;
    else if (top)
      *result = HTTOP;
    else if (bottom)
      *result = HTBOTTOM;
    else
      return false;
    return true;
  }
  if (msg->message == WM_GETMINMAXINFO) {
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (GetMonitorInfoW(MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST),
                        &monitor)) {
      auto* info = reinterpret_cast<MINMAXINFO*>(msg->lParam);
      info->ptMaxPosition = {monitor.rcWork.left - monitor.rcMonitor.left,
                             monitor.rcWork.top - monitor.rcMonitor.top};
      info->ptMaxSize = {monitor.rcWork.right - monitor.rcWork.left,
                         monitor.rcWork.bottom - monitor.rcWork.top};
      // A restored window may legitimately exceed one monitor at high DPI.
      // Keep Qt's configured bounds instead of Windows' default screen-sized
      // tracking cap; maximization still uses the work area above.
      info->ptMaxTrackSize = {
          qRound(window->maximumWidth() * window->devicePixelRatioF()),
          qRound(window->maximumHeight() * window->devicePixelRatioF())};
      info->ptMinTrackSize = {
          qRound(window->minimumWidth() * window->devicePixelRatioF()),
          qRound(window->minimumHeight() * window->devicePixelRatioF())};
      *result = 0;
      return true;
    }
  }
#else
  Q_UNUSED(window);
  Q_UNUSED(message);
  Q_UNUSED(result);
#endif
  return false;
}
}  // namespace workbench
