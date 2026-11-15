// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QCloseEvent>
#include <QDockWidget>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <algorithm>
namespace workbench {
// A native title bar while floating keeps movement and Close reachable.
class NodeViewDock final : public QDockWidget {
 public:
  explicit NodeViewDock(const QString& title, QWidget* parent) : QDockWidget(title,parent) {
    grip_=new QWidget(this);
    grip_->setObjectName("dockGrip");
    grip_->setFixedHeight(7);
    grip_->setToolTip(QStringLiteral("拖动调整面板，双击浮动"));
    setTitleBarWidget(grip_);
    connect(this,&QDockWidget::topLevelChanged,this,[this](bool floating) {
      setTitleBarWidget(floating ? nullptr : grip_);
      grip_->setVisible(!floating);
      if(floating)QTimer::singleShot(0,this,[this]{ensureOnScreen();});
    });
  }
  void setExpandedFloating(bool floating) {
    setFloating(floating);
    if(floating) {
      const auto area=availableArea();
      resize(std::min(760,area.width()),std::min(560,area.height()));
      move(area.center()-QPoint(width()/2,height()/2));
    }
    show();raise();
    if(floating)QTimer::singleShot(0,this,[this]{ensureOnScreen();});
  }
 protected:
  void closeEvent(QCloseEvent* event) override {
    if(isFloating()) {
      setExpandedFloating(false);
      event->ignore();
    } else QDockWidget::closeEvent(event);
  }
 private:
  QRect availableArea() const {
    auto* target=QGuiApplication::screenAt(frameGeometry().center());
    if(!target && parentWidget())target=parentWidget()->screen();
    if(!target)target=QGuiApplication::primaryScreen();
    return target ? target->availableGeometry().adjusted(12,12,-12,-12) : QRect(12,12,776,576);
  }
  void ensureOnScreen() {
    if(!isFloating() || !isVisible())return;
    const auto area=availableArea();
    const auto extra=frameGeometry().size()-size();
    resize(std::min(width(),std::max(1,area.width()-extra.width())),
           std::min(height(),std::max(1,area.height()-extra.height())));
    const auto frame=frameGeometry();
    const QPoint target(std::clamp(frame.left(),area.left(),std::max(area.left(),area.right()-frame.width()+1)),
                        std::clamp(frame.top(),area.top(),std::max(area.top(),area.bottom()-frame.height()+1)));
    move(pos()+target-frame.topLeft());
  }
  QWidget* grip_=nullptr;
};
} // namespace workbench
