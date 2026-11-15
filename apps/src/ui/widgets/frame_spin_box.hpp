// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QSpinBox>
#include <QStyleOptionSpinBox>
#include <QStylePainter>
#include <QPainterPath>
namespace workbench {
// Keep QSpinBox editing, hit testing and keyboard behavior. Paint navigation
// chevrons explicitly so stylesheet/native spin indicators cannot become boxes.
class FrameSpinBox final : public QSpinBox {
 public:
  explicit FrameSpinBox(QWidget* parent=nullptr) : QSpinBox(parent) {
    setButtonSymbols(QAbstractSpinBox::UpDownArrows);
  }
 protected:
  void paintEvent(QPaintEvent*) override {
    QStyleOptionSpinBox option;initStyleOption(&option);
    const auto up=style()->subControlRect(QStyle::CC_SpinBox,&option,QStyle::SC_SpinBoxUp,this);
    const auto down=style()->subControlRect(QStyle::CC_SpinBox,&option,QStyle::SC_SpinBoxDown,this);
    auto frame=option;frame.buttonSymbols=QAbstractSpinBox::NoButtons;
    frame.subControls=QStyle::SC_SpinBoxFrame|QStyle::SC_SpinBoxEditField;
    QStylePainter painter(this);painter.drawComplexControl(QStyle::CC_SpinBox,frame);
    painter.setRenderHint(QPainter::Antialiasing);
    auto arrow=[&](const QRect& rect,bool upwards) {
      const bool enabled=isEnabled() && option.stepEnabled.testFlag(upwards?QAbstractSpinBox::StepUpEnabled:QAbstractSpinBox::StepDownEnabled);
      painter.setPen(QPen(palette().color(enabled?QPalette::Active:QPalette::Disabled,QPalette::Text),1.3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
      const QPointF center=rect.center();const qreal direction=upwards?-1:1;
      QPainterPath path;path.moveTo(center+QPointF(-3,-direction*1.5));
      path.lineTo(center+QPointF(0,direction*1.5));path.lineTo(center+QPointF(3,-direction*1.5));
      painter.drawPath(path);
    };
    arrow(up,true);arrow(down,false);
  }
};
} // namespace workbench
