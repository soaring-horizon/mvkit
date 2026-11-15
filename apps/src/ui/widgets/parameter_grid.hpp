// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

namespace workbench {
// Qt 5 has no setRowVisible; hiding fields leaves spacing for empty rows.
// Keep widgets and labels alive while rebuilding only the visible rows.
class RangeForm final : public QFormLayout {
 public:
  explicit RangeForm(QWidget* parent) : QFormLayout(parent) {
    setRowWrapPolicy(QFormLayout::WrapLongRows);
    setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  }
  void addRow(const QString& name, QWidget* field) {
    auto* label = new QLabel(name, parentWidget());
    label->setBuddy(field);
    if(field->accessibleName().isEmpty())field->setAccessibleName(name);
    if(field->accessibleDescription().isEmpty())field->setAccessibleDescription(field->toolTip());
    rows_.append({label, field});
    QFormLayout::addRow(label, field);
  }
  void addRow(QWidget* field) {
    rows_.append({nullptr, field});
    QFormLayout::addRow(field);
  }
  void refreshRows() {
    while (rowCount()) {
      const auto row = takeRow(0);
      delete row.labelItem;
      delete row.fieldItem;
    }
    for (const auto& row : rows_) {
      const bool visible = !row.field->isHidden();
      if (row.label) row.label->setVisible(visible);
      if (!visible) continue;
      if (row.label)
        QFormLayout::addRow(row.label, row.field);
      else
        QFormLayout::addRow(row.field);
    }
    invalidate();
  }

 private:
  struct Row {
    QWidget* label;
    QWidget* field;
  };
  QVector<Row> rows_;
};
// Numeric settings use up to two labeled fields per row. Narrow dock panels
// fall back to one column instead of squeezing values or adding scroll bars.
class ParameterGrid final : public QWidget {
 public:
  explicit ParameterGrid(QWidget* parent = nullptr) : QWidget(parent) {
    grid_ = new QGridLayout(this);
    grid_->setContentsMargins(0, 3, 0, 3);
    grid_->setHorizontalSpacing(10);
    grid_->setVerticalSpacing(8);
    grid_->setSizeConstraint(QLayout::SetNoConstraint);
  }
  QSize minimumSizeHint() const override {
    return QSize(columnMinimum(), grid_->minimumSize().height());
  }
  void addField(const QString& name, QWidget* input) {
    auto* cell = new QWidget(this);
    auto* stack = new QVBoxLayout(cell);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setSpacing(4);
    auto* label = new QLabel(name, cell);
    label->setWordWrap(true);
    label->setToolTip(name);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    label->setBuddy(input);
    if(input->accessibleName().isEmpty())input->setAccessibleName(name);
    if(input->accessibleDescription().isEmpty())input->setAccessibleDescription(input->toolTip());
    input->setParent(cell);
    input->setMinimumWidth(0);
    input->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    stack->addWidget(label);
    stack->addWidget(input);
    cells_.append(cell);
    reflow(true);
  }

  void setFieldVisible(QWidget* input,bool visible) {
    auto* cell=input->parentWidget();
    if(cells_.contains(cell) && cell->isHidden()==visible){cell->setVisible(visible);reflow(true);}
  }
  bool hasFields() const { return !cells_.isEmpty(); }

protected:
  void resizeEvent(QResizeEvent* event) override {
    QWidget::resizeEvent(event);
    reflow(false);
  }

 private:
  void reflow(bool force) {
    const int threshold = qMax(360, columnMinimum() * 2 + 10);
    QVector<QWidget*> visible;for(auto* cell:cells_)if(!cell->isHidden())visible.append(cell);
    const int columns = visible.size() > 1 && width() >= threshold ? 2 : 1;
    if (!force && columns == columns_) return;
    columns_ = columns;
    while (auto* item = grid_->takeAt(0)) delete item;
    grid_->setColumnStretch(0, 1);
    grid_->setColumnStretch(1, columns == 2 ? 1 : 0);
    for (int i = 0; i < visible.size(); ++i)
      grid_->addWidget(visible[i], i / columns, i % columns);
    updateGeometry();
  }
  int columnMinimum() const {
    int minimum = 130;
    for (auto* cell : cells_)if(!cell->isHidden())
      minimum = qMax(minimum, cell->minimumSizeHint().width());
    return minimum;
  }
  QGridLayout* grid_ = nullptr;
  QVector<QWidget*> cells_;
  int columns_ = 0;
};
}  // namespace workbench
