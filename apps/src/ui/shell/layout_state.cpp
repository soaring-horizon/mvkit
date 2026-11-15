// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/shell/main_window.hpp"
#include <QDockWidget>
#include <QSplitter>
namespace workbench {
void MainWindow::rememberModeLayout() {
  const int mode = workflowMode_ ? 1 : 0;
  modeLayouts_[mode] = saveState(4);
  modeViewSplits_[mode] = views_->saveState();
  modeSpatialSplits_[mode] = spatialViews_->saveState();
}
void MainWindow::restoreModeLayout() {
  const int mode = workflowMode_ ? 1 : 0;
  if (!modeLayouts_[mode].isEmpty()) {
    restoreState(modeLayouts_[mode], 4);
    nodeOutputDock_->setVisible(workflowMode_);
    views_->restoreState(modeViewSplits_[mode]);
    spatialViews_->restoreState(modeSpatialSplits_[mode]);
  } else if (workflowMode_) {
    resizeDocks({nodeOutputDock_, outputDock_}, {290, 485}, Qt::Vertical);
  } else {
    resizeDocks({outputDock_}, {725}, Qt::Vertical);
  }
}
QByteArray MainWindow::migrateLegacyLayout(const QByteArray& state) {
  if(state.isEmpty())return {};
  const auto current=saveState(4);
  if(!restoreState(state,4)) { restoreState(current,4);return {}; }
  // Keep the surviving result dock's geometry. The editor is now a permanent
  // page in that dock, so an obsolete parametersDock must not hide it.
  outputDock_->show();outputDock_->raise();
  const auto migrated=saveState(4);
  restoreState(current,4);
  return migrated;
}
}  // namespace workbench
