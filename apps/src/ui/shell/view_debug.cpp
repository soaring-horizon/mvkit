// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/shell/main_window.hpp"
#include <QDockWidget>
#include <QAction>
#include "ui/style/appearance.hpp"
#include "ui/widgets/node_view_dock.hpp"
#include <QFileInfo>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QToolButton>
namespace workbench {
int MainWindow::viewNodeIndex() const {
  if (workflowMode_ && !pinnedViewNode_.isEmpty()) {
    const int pinned = workflowIds_.indexOf(pinnedViewNode_);
    if (pinned >= 0) return pinned;
  }
  return selectedStep_ == -2 ? workflowOutput_ : selectedStep_;
}
bool MainWindow::viewPinnedElsewhere() const {
  return workflowMode_ && !pinnedViewNode_.isEmpty() &&
         workflowIds_.contains(pinnedViewNode_) &&
         viewNodeIndex() != (selectedStep_ == -2 ? workflowOutput_ : selectedStep_);
}
Result MainWindow::resultForNode(int index) const {
  Result inspected;
  if (index == -1 && document_.hasImage()) {
    inspected.outputImage = document_.image;
    inspected.preview = document_.preview;
    inspected.pipeline = QStringLiteral("原始图像");
    return inspected;
  }
  if (activeImage_ < 0 || images_[activeImage_].workflowResultVersion !=
                              images_[activeImage_].version)
    return inspected;
  const auto& flow = images_[activeImage_].workflowResult;
  if (index < 0 || index >= flow.nodeOutputs.size()) return inspected;
  const auto& output = flow.nodeOutputs[index];
  inspected.pipeline = nodeNames().value(index);
  inspected.error = output.error;
  if (output.state == NodeState::Success || !output.objects.isEmpty() ||
      !output.instances.isEmpty()) {
    inspected.spatial = output.spatial;
    inspected.outputImage = output.outputImage;
    inspected.preview = output.preview;
    inspected.objects = output.objects;
    inspected.instances = output.instances;
    inspected.measureName = output.measureName;
    inspected.algorithmMs = output.algorithmMs;
    inspected.totalMs = output.totalMs;
    inspected.steps = flow.steps;
  }
  return inspected;
}
void MainWindow::refreshViewIdentity() {
  if (!pinView_) return;
  if (!pinnedViewNode_.isEmpty() && !workflowIds_.contains(pinnedViewNode_))
    pinnedViewNode_.clear();
  const bool pinned = !pinnedViewNode_.isEmpty();
  const int index = viewNodeIndex();
  const auto name = index == -1 ? QStringLiteral("原始图像")
                                : nodeNames().value(index, QStringLiteral("未选择节点"));
  const bool training = trainingView() && !viewPinnedElsewhere();
  const auto* model = training ? currentModel() : nullptr;
  const auto resourcePath = model ? model->referencePath : document_.imagePath;
  const auto title = QStringLiteral("视图 · ") + name +
                     (pinned ? QStringLiteral(" · 固定") : QString()) +
                     (training ? QStringLiteral(" · 模板") : QString());
  nodeViewTabs_->setTabText(0, title);
  nodeViewTabs_->setTabToolTip(0,
      QStringLiteral("%1\n图像：%2\n%3%4")
          .arg(title, QFileInfo(resourcePath).fileName(),
               pinned ? QStringLiteral("固定节点；仍显示当前图像的本次新结果")
                      : QStringLiteral("跟随当前所选节点"),
               viewPinnedElsewhere()
                   ? QStringLiteral("\n参数及结果表属于所选节点；解除固定或选回此节点后编辑 ROI")
                   : QString()));
  nodeView_->setProperty("viewSourceLabel", title);
  const QSignalBlocker block(pinView_);
  pinView_->setChecked(pinned);
  pinView_->setEnabled(pinned || index >= 0 && index < workflow_.size());
  pinView_->setToolTip(pinned ? QStringLiteral("解除固定，跟随所选节点")
                            : QStringLiteral("固定观察此节点；切换参数时视图保持"));
  compareSource_->setVisible(nodeView_->compareEnabled());
  compareSource_->setToolTip(compareNodeInput_ ? QStringLiteral("对比：节点输入 ↔ 输出")
                                               : QStringLiteral("对比：原始图像 ↔ 输出"));
  floatView_->setToolTip(nodeOutputDock_->isFloating() ? QStringLiteral("停靠视图")
                                                       : QStringLiteral("弹出并放大视图"));
  floatView_->setAccessibleName(floatView_->toolTip());
  floatView_->setIcon(studioIcon(nodeOutputDock_->isFloating() ? "dock" : "maximize"));
  if(auto* redock=findChild<QAction*>("dockNodeViewAction"))redock->setEnabled(nodeOutputDock_->isFloating());
}
void MainWindow::setNodeViewFloating(bool floating) {
  static_cast<NodeViewDock*>(nodeOutputDock_)->setExpandedFloating(floating);
  refreshViewIdentity();
}
}  // namespace workbench
