// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/shell/main_window.hpp"
#include "model/spatial_data.hpp"

#include <stdexcept>

namespace workbench {
namespace {
void require(bool valid, const char* message) {
  if (!valid) throw std::runtime_error(message);
}
}  // namespace
bool MainWindow::exportObservedPointCloud(const QString& path) {
  if (busy()) return false;
  return guard(QStringLiteral("导出点云"), [&] {
    require(spatialView_ && spatialView_->group == (document_.captureId.isEmpty() ? document_.imagePath : document_.captureId) &&
                activeImage_ >= 0 && images_[activeImage_].workflowResultVersion == images_[activeImage_].version,
            "请先运行当前流程并查看有效点云");
    savePointCloud(*spatialView_, path);
    log(QStringLiteral("点云已导出（mm，完整有效点）：") + path);
  });
}
} // namespace workbench
