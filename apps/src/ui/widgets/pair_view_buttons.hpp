// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QToolButton>

namespace workbench {
// Compact camera selection. The combo remains the internal named-output model.
class PairViewButtons final : public QWidget {
public:
  PairViewButtons(QComboBox *choice, QWidget *parent)
      : QWidget(parent), choice_(choice) {
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    for (bool right : {false, true}) {
      auto *b = new QToolButton(this);
      b->setObjectName(choice->objectName() + (right ? "Right" : "Left"));
      b->setCheckable(true);
      b->setFocusPolicy(Qt::StrongFocus);
      b->setFixedHeight(30);
      b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
      b->setToolTip(QStringLiteral("切换当前相机视图；不重新运行算法"));
      group->addButton(b);
      row->addWidget(b);
      buttons_[right ? 1 : 0] = b;
      connect(b, &QToolButton::clicked, this, [this, right] {
        const int index = choice_->findData(right ? "right" : "left");
        if (index < 0)
          return;
        choice_->setCurrentIndex(index);
        emit choice_->activated(index);
        refresh();
      });
    }
    connect(choice_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { refresh(); });
    refresh();
  }
  bool paired() const {
    return choice_->count() == 2 && choice_->findData("left") >= 0 &&
           choice_->findData("right") >= 0;
  }
  void refresh() {
    setVisible(paired());
    choice_->setVisible(!paired() && choice_->count() > 0);
    if (!paired())
      return;
    for (int i = 0; i < 2; ++i) {
      const auto key = i ? "right" : "left";
      auto *b = buttons_[i];
      b->setText(choice_->itemText(choice_->findData(key)));
      b->setAccessibleName(QStringLiteral("查看") + b->text());
      b->setFixedWidth(
          qBound(88, b->fontMetrics().horizontalAdvance(b->text()) + 32, 144));
      b->setChecked(choice_->currentData().toString() == key);
    }
  }

private:
  QComboBox *choice_;
  QToolButton *buttons_[2]{};
};
} // namespace workbench
