// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/shell/main_window.hpp"
#include "model/spatial_data.hpp"

#include <QAction>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QTableWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <cmath>

namespace workbench {
bool MainWindow::renameCapture(int index, const QString& name) {
  if (busy() || index < 0 || index >= images_.size()) return false;
  return guard(QStringLiteral("重命名采集组"), [&] {
    const auto text = name.trimmed();
    if (text.isEmpty() || text.size() > 80)
      throw std::runtime_error("采集组名称须为 1–80 个字符");
    EditGuard edit(*this, QStringLiteral("重命名采集组"));
    if(images_[index].document.captureId.isEmpty())images_[index].document.captureId=images_[index].id;
    images_[index].document.captureName = text;
    if(index==activeImage_)document_.captureId=images_[index].document.captureId;
    if (index == activeImage_) document_.captureName = text;
    setDirty(true);
    refreshFilm();
  });
}
bool MainWindow::replaceStereoCalibration(const QVector<int>& indices, const QString& path) {
  if (busy() || indices.isEmpty()) return false;
  return guard(QStringLiteral("替换双目标定"), [&] {
    const auto bytes = readStereoCalibration(path);
    for (int index : indices) {
      if (index < 0 || index >= images_.size() || images_[index].document.captureId.isEmpty())
        throw std::runtime_error("请选择有效双目采集组");
      validateStereoCapture(images_[index].document, bytes);
    }
    EditGuard edit(*this, QStringLiteral("替换双目标定"));
    storeActive();
    for (int index : QSet<int>(indices.begin(), indices.end())) {
      auto& resource = images_[index];
      resource.document.stereoArtifact = bytes;
      resource.document.stereoName = QFileInfo(path).completeBaseName();
      resource.result = {};
      resource.workflowResult = {};
      resource.deliveryResult = {};
      resource.deliveryUtc.clear();
      ++resource.version;
      ++resource.baseVersion;
      if (index == activeImage_) {
        document_.stereoArtifact = bytes;
        document_.stereoName = resource.document.stereoName;
      }
    }
    if (indices.contains(activeImage_)) {
      result_ = {};
      invalidate();
    }
    setDirty(true);
    refreshFilm();
  });
}
void MainWindow::showCaptureManager() {
  showResourceManager();
}
}  // namespace workbench
