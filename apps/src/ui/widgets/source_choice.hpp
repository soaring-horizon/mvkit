// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QComboBox>
#include <QCompleter>
#include <QLineEdit>
#include <QTimer>
#include <QStandardItemModel>
namespace workbench {
// Prompt rows describe the missing binding; they are never bindable sources.
inline void showSourceSelection(QComboBox* box, const QVariant& selected, const QVariant& prompt) {
  int hint = box->findData(prompt);
  const int chosen = box->findData(selected);
  if (hint >= 0 && chosen >= 0 && chosen != hint) {
    box->removeItem(hint);
    hint = -1;
  } else if (hint >= 0) {
    auto* model = qobject_cast<QStandardItemModel*>(box->model());
    if (model) model->item(hint)->setEnabled(false);
  }
  const int row = box->findData(selected);
  box->setCurrentIndex(row >= 0 ? row : hint);
  if (row >= 0 && hint < 0 && box->count() == 1)
    box->setItemText(row, box->itemText(row) + QStringLiteral("（自动）"));
}
// Searching never creates an input or changes graph connectivity. Only a
// valid activated item is handled by the existing binding transactions.
class SourceChoice final : public QComboBox {
 public:
   explicit SourceChoice(QWidget *parent, bool searchable = false)
       : QComboBox(parent) {
     setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
     setMinimumContentsLength(8);
     if (!searchable)
       return;
     setEditable(true);
     lineEdit()->setFrame(false);
     lineEdit()->setStyleSheet("border:0;background:transparent;padding:0;");
     setInsertPolicy(QComboBox::NoInsert);
     lineEdit()->setPlaceholderText(QStringLiteral("搜索已连通的上游"));
     completer()->setFilterMode(Qt::MatchContains);
     completer()->setCaseSensitivity(Qt::CaseInsensitive);
     completer()->setCompletionMode(QCompleter::PopupCompletion);
     connect(lineEdit(), &QLineEdit::editingFinished, this, [this] {
       QTimer::singleShot(0, this, [this] {
         setEditText(currentIndex() < 0 ? QString() : itemText(currentIndex()));
       });
     });
   }
};
}  // namespace workbench
