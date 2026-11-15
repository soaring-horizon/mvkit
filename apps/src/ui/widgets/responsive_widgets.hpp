// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "ui/style/appearance.hpp"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QResizeEvent>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStyleOption>
#include <QToolButton>
#include <QToolBar>
#include <QButtonGroup>
#include "ui/widgets/pair_view_buttons.hpp"

namespace workbench {
// Commands keep their names in menus, tooltips and accessibility metadata.
class CommandToolbar final : public QToolBar {
 public:
  using QToolBar::QToolBar;
 protected:
  void resizeEvent(QResizeEvent* event) override {
    QToolBar::resizeEvent(event);
    if(toolButtonStyle()!=Qt::ToolButtonIconOnly)setToolButtonStyle(Qt::ToolButtonIconOnly);
  }
};
// Keep the full value available even when the current pane cannot display it.
class ElidedLabel final : public QLabel {
 public:
  explicit ElidedLabel(QWidget* parent = nullptr) : QLabel(parent) {
    setTextFormat(Qt::PlainText);
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  }
  void setText(const QString& text) {
    QLabel::setText(text);
    setToolTip(text);
    update();
  }
  QString fullText() const { return text(); }
  void setElideMode(Qt::TextElideMode mode) { mode_ = mode; update(); }
 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    QStyleOption option;
    option.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawText(contentsRect(), alignment(), fontMetrics().elidedText(
        text(), mode_, qMax(0, contentsRect().width())));
  }
 private:
  Qt::TextElideMode mode_ = Qt::ElideRight;
};

class ViewSplitter final : public QSplitter {
 public:
  explicit ViewSplitter(Qt::Orientation direction, QWidget* parent = nullptr)
      : QSplitter(direction, parent) {
    setHandleWidth(7);
    setChildrenCollapsible(false);
  }
 protected:
  QSplitterHandle* createHandle() override {
    auto* handle = new QSplitterHandle(orientation(), this);
    handle->setAttribute(Qt::WA_Hover);
    handle->setToolTip(QStringLiteral("拖动调整视图大小；布局菜单可保存或恢复"));
    handle->setAccessibleName(QStringLiteral("调整视图大小"));
    return handle;
  }
};

// Secondary controls remain reachable from More in narrow panes. Resizing only
// changes presentation, never algorithm settings or the selected output.
class SpatialToolbar final : public QWidget {
 public:
  SpatialToolbar(QComboBox* choice, QWidget* parent) : QWidget(parent), choice_(choice) {
    setObjectName("spatialToolbar");
    setFixedHeight(40);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(8, 3, 8, 3);
    row->setSpacing(6);
    choice->setParent(this);
    choice->setMinimumWidth(0);
    choice->setMinimumContentsLength(10);
    choice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    choice->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    pair_ = new PairViewButtons(choice,this);
    row->addWidget(pair_,0,Qt::AlignLeft);
    row->addWidget(choice, 2);
    color = new QComboBox(this);
    color->addItems({QStringLiteral("伪彩"), QStringLiteral("灰度")});
    color->setAccessibleName(QStringLiteral("数值图显示配色"));
    color->setToolTip(QStringLiteral("仅改变显示配色；蓝→青→黄表示低→高"));
    row->addWidget(color);
    scale = new QToolButton(this);
    scale->setText(QStringLiteral("自动"));
    scale->setAccessibleName(QStringLiteral("深度色阶范围"));
    row->addWidget(scale);
    legend = new ElidedLabel(this);
    legend->setProperty("class", "secondaryText");
    row->addWidget(legend, 1);
    more_ = new QToolButton(this);
    more_->setIcon(studioIcon("more"));
    more_->setIconSize(QSize(18,18));
    more_->setFixedSize(40,28);
    more_->setStyleSheet("QToolButton{padding:2px 9px 2px 2px;min-height:0;min-width:0;}");
    more_->setAccessibleName(QStringLiteral("更多显示设置"));
    more_->setToolTip(QStringLiteral("更多显示设置"));
    more_->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(more_);
    more_->setMenu(menu);
    row->addWidget(more_);
    row_ = row;row->addStretch(0);
    connect(menu, &QMenu::aboutToShow, this, [this, menu] {
      menu->clear();
      if (!intensity_) {
        for (int i = 0; i < color->count(); ++i) {
          auto* a = menu->addAction(color->itemText(i));
          a->setCheckable(true);
          a->setChecked(i == color->currentIndex());
          connect(a, &QAction::triggered, this, [this, i] {
            color->setCurrentIndex(i);
            emit color->activated(i);
          });
        }
      }
      if (depth_ || residual_) {
        auto* a = menu->addAction(residual_ ? QStringLiteral("残差显示…") : QStringLiteral("深度色阶…"));
        connect(a, &QAction::triggered, scale, &QToolButton::click);
      }
      if (!legend->fullText().isEmpty()) {
        menu->addSeparator();
        auto* a = menu->addAction(legend->fullText());
        a->setEnabled(false);
      }
    });
  }
  QSize minimumSizeHint() const override { return {180, 40}; }
  void setDisplayModes(bool intensity, bool depth, bool residual = false) {
    intensity_ = intensity; depth_ = depth; residual_ = residual; adapt();
  }
  QComboBox* color = nullptr;
  QToolButton* scale = nullptr;
  ElidedLabel* legend = nullptr;
 protected:
  void resizeEvent(QResizeEvent* e) override { QWidget::resizeEvent(e); adapt(); }
 private:
  void adapt() {
    const bool pair=choice_->count()==2 && choice_->findData("left")>=0 && choice_->findData("right")>=0;
    pair_->setVisible(pair);choice_->setVisible(!pair);
    pair_->refresh();
    row_->setStretch(row_->count()-1,pair?1:0);
    color->setVisible(!intensity_ && width() >= 440);
    scale->setVisible((depth_ || residual_) && width() >= 340);
    legend->setVisible(!pair && width() >= 580);
    more_->setVisible(!pair && width() < 580);
  }
  QComboBox* choice_=nullptr;
  PairViewButtons* pair_=nullptr;
  QHBoxLayout* row_=nullptr;
  QToolButton* more_ = nullptr;
  bool intensity_ = false, depth_ = false, residual_ = false;
};
}  // namespace workbench
