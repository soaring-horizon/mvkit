// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>

#include "ui/shell/main_window.hpp"
#include "ui/views/workflow_view.hpp"
namespace workbench {
void MainWindow::refreshDiagnostics() {
  if (!locateIssue_) return;
  issueNode_ = -3;
  issueField_.clear();
  QString reason;
  if (!document_.hasImage()) {
    locateIssue_->hide();
    return;
  }
  int node = selectedStep_ == -2 ? workflowOutput_ : selectedStep_;
  const QVector<NodeOutput>* outputs = nullptr;
  if (nodeContext() && activeImage_ >= 0 && activeImage_ < images_.size() &&
      images_[activeImage_].workflowResultVersion ==
          images_[activeImage_].version)
    outputs = &images_[activeImage_].workflowResult.nodeOutputs;
  if (outputs) {
    if (node >= 0 && node < outputs->size() &&
        (*outputs)[node].state == NodeState::Blocked) {
      for (int upstream : workflowAncestors(workflowLinks_, node))
        if (upstream >= 0 && upstream < outputs->size() &&
            (*outputs)[upstream].state == NodeState::Error) {
          node = upstream;
          break;
        }
    }
    if (node < 0 || node >= outputs->size() ||
        ((*outputs)[node].state != NodeState::Error &&
         (*outputs)[node].state != NodeState::Blocked)) {
      for (int i = 0; i < outputs->size(); ++i)
        if ((*outputs)[i].state == NodeState::Error) {
          node = i;
          break;
        }
    }
    if (node >= 0 && node < outputs->size() &&
        ((*outputs)[node].state == NodeState::Error ||
         (*outputs)[node].state == NodeState::Blocked))
      reason = (*outputs)[node].error;
    if (node >= 0 && node < outputs->size() &&
        (*outputs)[node].state == NodeState::Blocked) {
      const auto bound = rangeForNode(node);
      if (bound.poseSource >= 0 && bound.poseSource < outputs->size() &&
          (*outputs)[bound.poseSource].state == NodeState::Success &&
          bound.poseInstance >= (*outputs)[bound.poseSource].objects.size()) {
        node = bound.poseSource;
        issueField_ = "parameters";
      }
    }
  }
  const bool validNode = node >= 0 && node < workflow_.size();
  if (nodeContext() && !validNode) {
    locateIssue_->hide();
    return;
  }
  const auto& p = nodeContext() ? workflow_[node] : document_.parameters;
  const auto range = nodeContext() ? rangeForNode(node) : effectiveRange();
  try {
    p.validate();
  } catch (const std::exception& error) {
    reason = QString::fromUtf8(error.what());
    issueField_ = "parameters";
  }
  if (range.iteratePose && range.scope == ScopeMode::Full) {
    reason = QStringLiteral("逐目标处理需要明确的处理范围");
    issueField_ = "scope";
  } else if (nodeContext() && dataTool(p.algorithm) &&
             (range.dataA < 0 || needsSecondInput(p) && range.dataB < 0)) {
    reason = QStringLiteral("请连接类型化输入 A/B 并选择有效对象");
    issueField_ = "data";
  } else if (nodeContext() && !sourceTool(p.algorithm) && !captureTool(p.algorithm) && !dataTool(p.algorithm) && workflowInputs_.value(node, -2) < -1) {
    reason = QStringLiteral("未连接有效图像输入");
    issueField_ = "input";
  } else if (range.scope == ScopeMode::Region && range.regionSource < 0) {
    reason = QStringLiteral("区域来源未连接");
    issueField_ = "region";
  } else if (range.poseSource == -2 ||
             range.poseSource >= 0 && !range.poseReferenceSet) {
    reason = QStringLiteral("请连接定位来源并设置参考位置");
    issueField_ = "pose";
  } else if (p.algorithm == Algorithm::ShapeMatch ||
             p.algorithm == Algorithm::NccMatch) {
    const auto* model = findModel(models_, range.modelId);
    if (!model ||
        !model->artifactOnly && (model->preview.isNull() ||
                                 model->trainingRoi.kind == RoiKind::None)) {
      reason =
          QStringLiteral("请加载模板文件，或在模板页设置参考图像和训练范围");
      issueField_ = "template";
    }
  }
  if (!nodeContext() && reason.isEmpty() && !result_.error.isEmpty())
    reason = result_.error;
  if (!reason.isEmpty()) {
    issueNode_ = nodeContext() ? node : -1;
    if (issueField_.isEmpty()) issueField_ = "logs";
  }
  locateIssue_->setVisible(!reason.isEmpty());
  locateIssue_->setEnabled(!runQueue_.active() && !execution_.busy());
  locateIssue_->setText(issueField_ == "template" ? QStringLiteral("检查模板")
                        : issueField_ == "logs" ? QStringLiteral("定位报错")
                                                : QStringLiteral("检查配置"));
  locateIssue_->setToolTip(
      (nodeContext() ? nodeNames().value(node)
                     : operatorDefinition(p.algorithm).name) +
      QStringLiteral("：") + reason +
      QStringLiteral("\n定位时视图跟随问题节点；不会自动修改配置或弹出对话框"));
  locateIssue_->setProperty("issueNode", issueNode_);
  locateIssue_->setProperty("issueField", issueField_);
}
void MainWindow::locateIssue() {
  if (runQueue_.active() || execution_.busy() || issueNode_ < -1) return;
  const int node = issueNode_;
  const auto field = issueField_;
  pinnedViewNode_.clear();
  if (nodeContext() && node >= 0) {
    if (workflowMode_)
      workflowView_->focusNode(node);
    else
      debugNode(workflowIds_.value(node));
  }
  if (field == "logs") {
    outputDock_->show();outputDock_->raise();
    outputTabs_->setCurrentWidget(logs_);
    return;
  }
  outputDock_->show();outputDock_->raise();
  outputTabs_->setCurrentWidget(operatorScroll_);
  QWidget* target = nullptr;
  if (field == "scope")
    target = nodeScope_;
  else if (field == "data")
    target = geometryA_;
  else if (field == "input")
    target = nodeInput_;
  else if (field == "region")
    target = regionInput_;
  else if (field == "pose")
    target = effectiveRange().poseSource < 0 ? static_cast<QWidget*>(poseInput_)
                                             : referencePose_;
  else if (field == "template") {
    matchingTabs_->setCurrentIndex(1);
    target = currentModel() ? static_cast<QWidget*>(editRange_) : loadTemplate_;
  } else {
    const auto& p = document_.parameters;
    target = p.algorithm == Algorithm::Filter  ? static_cast<QWidget*>(mask_)
             : p.algorithm == Algorithm::Edges ? static_cast<QWidget*>(low_)
             : p.algorithm == Algorithm::Gray ? static_cast<QWidget*>(grayType_)
             : p.algorithm == Algorithm::Regions
                 ? static_cast<QWidget*>(minimum_)
                 : score_;
  }
  if (target) {
    operatorScroll_->ensureWidgetVisible(target);
    target->setFocus(Qt::OtherFocusReason);
  }
}
}  // namespace workbench
