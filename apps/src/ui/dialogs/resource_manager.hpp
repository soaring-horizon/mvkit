// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "model/document.hpp"
#include <QDialog>
class QCloseEvent;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
namespace workbench {
class MainWindow;
class ImageView;
class PairViewButtons;
class ResourceManager final : public QDialog {
 public:
  explicit ResourceManager(MainWindow* owner);
  void focusInput(const QString& binding, int sample);
 protected:
  void closeEvent(QCloseEvent* event) override;
 private:
  struct InputColumn {
    QString key, title;
    QStringList nodes;
    bool sequence = false, multiView = false, conflict = false, editable = true;
  };
  struct ViewColumn { int input = -1; QString role; };
  void refresh();
  void inspect();
  void updateActions();
  void showFrame();
  void selectFiles();
  void editOrder();
  void clearInput();
  void shareInput();
  QVector<int> selectedSamples() const;
  int sampleIndex() const;
  int inputIndex() const;
  const Document& sampleDocument(int index) const;
  MainWindow* owner_;
  QVector<InputColumn> inputs_;
  QVector<ViewColumn> columns_;
  SourceData preview_;
  QTableWidget* table_ = nullptr;
  ImageView* view_ = nullptr;
  QLabel *count_ = nullptr, *inputTitle_ = nullptr, *inputInfo_ = nullptr,
         *path_ = nullptr,
         *status_ = nullptr;
  QSpinBox* frame_ = nullptr;
  QComboBox* viewChoice_ = nullptr;
  QComboBox* source_ = nullptr;
  QString binding_;
  PairViewButtons* viewButtons_ = nullptr;
  QLabel* viewNames_ = nullptr;
  QPushButton *replaceInput_ = nullptr, *clearInput_ = nullptr,
              *shareInput_ = nullptr, *order_ = nullptr, *browse_ = nullptr;
  bool refreshing_ = false;
};
}  // namespace workbench
