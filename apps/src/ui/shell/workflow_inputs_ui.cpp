// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/shell/main_window.hpp"
#include "model/workflow_ports.hpp"
#include <QComboBox>
#include <QMenu>
#include <QSignalBlocker>
#include <QJsonArray>
#include <algorithm>

namespace workbench {
Parameters MainWindow::portParameters(int node) const {
  auto p = workflow_.value(node);
  if (multiViewInput(p)) {
    const auto data = resolveSourceData(document_, p.options["binding"].toString());
    if (!data.views.isEmpty()) p.options["viewRoles"] = QJsonArray::fromStringList(data.views);
  }
  return p;
}
void MainWindow::fillNamedInput(QComboBox* box, int target, const QString& slot) {
  if (target < 0 || target >= workflow_.size()) return;
  const QSignalBlocker block(box);
  box->clear();
  QString label = slot;
  for (const auto& port : inputPorts(workflow_[target])) if (port.key == slot) label = port.label;
  box->setAccessibleName(label);
  box->setToolTip(QStringLiteral("选择上游节点的具体输出；唯一可用输出自动选择。视图切换不会改变此绑定。"));
  box->addItem(label + QStringLiteral(" · 请选择来源"), -2);
  const auto names = nodeNames();
  for (int node : workflowAncestors(workflowLinks_, target)) {
    if (node < 0) {
      if (slot == "Image") box->addItem(QStringLiteral("原始图像"), -1);
      continue;
    }
    for (const auto& port : compatibleOutputs(portParameters(node), workflow_[target], slot)) {
      box->addItem(label + " · " + names[node] + " → " + port.label, node);
      box->setItemData(box->count()-1, port.key, Qt::UserRole+1);
      box->setItemData(box->count()-1, port.type + " · " + port.key, Qt::ToolTipRole);
    }
  }
  const auto settings = rangeForNode(target);
  auto ref = inputBinding(settings, slot);
  if (slot == "Image" && !settings.inputs.contains(slot)) ref.node = workflowInputs_.value(target, -2);
  int selected = -1;
  for (int row = 1; row < box->count(); ++row)
    if (box->itemData(row).toInt() == ref.node &&
        (box->itemData(row, Qt::UserRole+1).toString() == ref.output || ref.output.isEmpty() &&
         (ref.node < 0 || outputType(workflow_[ref.node]) == "Image" && slot == "Image"))) selected = row;
  if (selected < 0 && ref.node == -2 && !settings.inputs.contains(slot) && box->count() == 2) selected = 1;
  if (selected < 0 && (settings.inputs.contains(slot) || ref.node >= 0)) {
    box->addItem(label + QStringLiteral(" · 原选择已失效，请重新选择"), ref.node);
    selected = box->count()-1;
    box->setItemData(selected, ref.output, Qt::UserRole+1);
    box->setItemData(selected, QColor("#c95757"), Qt::ForegroundRole);
  }
  box->setCurrentIndex(std::max(0, selected));
  box->setEnabled(!runQueue_.active());
}
void MainWindow::chooseNamedInput(int target, const QString& slot, int source) {
  if (target < 0 || target >= workflow_.size() || source < 0 || source >= workflow_.size()) return;
  if (slot.isEmpty()) {
    struct Choice { QString slot, label, output; };
    QVector<Choice> choices;
    for (const auto& in : inputPorts(workflow_[target]))
      for (const auto& out : compatibleOutputs(portParameters(source), workflow_[target], in.key))
        choices.append({in.key, out.label + " → " + in.label, out.key});
    if (choices.size() == 1) { bindNamedInput(target, choices.first().slot, source, choices.first().output); return; }
    if (choices.isEmpty()) return;
    QMenu menu(this);
    for (const auto& choice : choices) {
      auto* action = menu.addAction(choice.label);
      connect(action, &QAction::triggered, this, [=] { bindNamedInput(target, choice.slot, source, choice.output); });
    }
    menu.exec(QCursor::pos()); return;
  }
  const auto choices = compatibleOutputs(portParameters(source), workflow_[target], slot);
  if (choices.size() == 1) { bindNamedInput(target, slot, source, choices.first().key); return; }
  if (choices.isEmpty()) return;
  QMenu menu(this);
  for (const auto& port : choices) {
    auto* action = menu.addAction(nodeNames()[source] + " → " + port.label);
    connect(action, &QAction::triggered, this, [=] { bindNamedInput(target, slot, source, port.key); });
  }
  menu.exec(QCursor::pos());
}
void MainWindow::bindNamedInput(int target, const QString& slot, int source, const QString& output) {
  if (runQueue_.active() || target < 0 || target >= workflow_.size() || source < 0 || source >= workflow_.size()) return;
  const auto choices = compatibleOutputs(portParameters(source), workflow_[target], slot);
  if (std::none_of(choices.begin(), choices.end(), [&](const OutputPort& p) {return p.key == output;})) return;
  auto links = workflowLinks_;
  if (!workflowAncestors(links, target).contains(source)) links[target].append(source);
  Document graph; graph.workflow = workflow_; graph.workflowInputs = workflowInputs_; graph.workflowLinks = links;
  try { workflowOrder(graph, true); } catch (...) { return; }
  EditGuard edit(*this, QStringLiteral("选择具名输出"));
  if (!confirmInputMerge(source,target)) return;
  workflowLinks_ = links;
  auto set = [&](NodeSettings& s) {
    setInputBinding(s, slot, {source, output});
    if (slot == "Region") s.scope = ScopeMode::Region;
    if (slot == "Match") s.poseReferenceSet = false;
  };
  set(sharedRanges_[target]);
  for (auto& item : images_) if (target < item.document.nodeSettings.size()) set(item.document.nodeSettings[target]);
  if (target < document_.nodeSettings.size()) set(document_.nodeSettings[target]);
  if (slot == "Image") workflowInputs_[target] = source;
  if (slot == "ObjectB" && workflow_[target].algorithm == Algorithm::CloudSelect) workflow_[target].options["selection"] = 1;
  if (slot == "ObjectA" && workflow_[target].algorithm == Algorithm::Judge && output.startsWith("value:")) workflow_[target].options["field"] = "value";
  if (workflow_[target].algorithm == Algorithm::StereoRectify) workflow_[target].options["multiView"] = 0;
  selectedStep_ = target;
  workflowChanged();
}
}
