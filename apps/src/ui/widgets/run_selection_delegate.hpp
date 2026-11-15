// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QStyledItemDelegate>
#include <QKeyEvent>
#include <QMouseEvent>
namespace workbench {
// Keep standard Qt checkbox accessibility while allowing the whole cell to toggle.
class RunSelectionDelegate final : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;
  bool editorEvent(QEvent* event,QAbstractItemModel* model,const QStyleOptionViewItem& option,const QModelIndex& index) override {
    if(!(index.flags()&Qt::ItemIsEnabled) || !(index.flags()&Qt::ItemIsUserCheckable))return false;
    bool toggle=false;
    if(event->type()==QEvent::MouseButtonRelease) {
      const auto* mouse=static_cast<QMouseEvent*>(event);
      toggle=mouse->button()==Qt::LeftButton && option.rect.contains(mouse->pos());
    } else if(event->type()==QEvent::KeyPress) {
      const auto key=static_cast<QKeyEvent*>(event)->key();toggle=key==Qt::Key_Space || key==Qt::Key_Select;
    } else if(event->type()==QEvent::MouseButtonDblClick)return true;
    if(!toggle)return false;
    return model->setData(index,index.data(Qt::CheckStateRole).toInt()==Qt::Checked ? Qt::Unchecked : Qt::Checked,Qt::CheckStateRole);
  }
};
}
