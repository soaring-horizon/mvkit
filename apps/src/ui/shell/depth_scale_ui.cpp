// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/shell/main_window.hpp"
#include "ui/widgets/parameter_spin_box.hpp"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace workbench {
ScalarRange MainWindow::cachedSpatialRange(const std::shared_ptr<const SpatialData>& payload) {
  if (!payload || !payload->rasters.contains("depth") || payload->rasters["depth"].unit != "m" ||
      payload->rasters["depth"].semantic != "depth") return {};
  const auto id = quintptr(payload.get());
  auto it = depthRanges_.find(id);
  if (it != depthRanges_.end() && it->data.lock() == payload) return it->range;
  if (depthRanges_.size() > 256) depthRanges_.clear();
  const auto range = spatialRange(*payload, "depth");
  depthRanges_[id] = {payload, range};
  return range;
}
ScalarRange MainWindow::depthScaleRange(int mode, int* ready, int* selected) {
  if (ready) *ready = 0;
  if (selected) *selected = 0;
  if (mode == 2) return manualDepthScale_;
  if (mode == 0 || !nodeContext()) return cachedSpatialRange(spatialView_);
  ScalarRange combined;
  const int index = viewNodeIndex();
  if (index < 0 || index >= workflow_.size()) return {};
  for (const auto& resource : images_) {
    if (!resource.checked || resource.document.captureId.isEmpty()) continue;
    if (selected) ++*selected;
    if (resource.workflowResultVersion != resource.version || index >= resource.workflowResult.nodeOutputs.size()) continue;
    const auto& node = resource.workflowResult.nodeOutputs[index];
    if (node.state != NodeState::Success || node.parameters.algorithm != workflow_[index].algorithm ||
        !node.spatial || node.spatial->group != resource.document.captureId) continue;
    const auto range = cachedSpatialRange(node.spatial);
    if (!range.available) continue;
    if (ready) ++*ready;
    if (!combined.available) combined = range;
    else { combined.lower = std::min(combined.lower, range.lower); combined.upper = std::max(combined.upper, range.upper); }
  }
  return combined;
}
void MainWindow::showDepthScale() {
  if (!spatialView_) return;
  auto* dialog = new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setObjectName("depthScaleDialog");
  dialog->setWindowTitle(QStringLiteral("深度色阶"));
  dialog->resize(480, 300);
  auto* layout = new QVBoxLayout(dialog);
  auto* hint = new QLabel(QStringLiteral("只调整深度图的显示范围；原始 m 数值、点云、选点与测量保持不变。超出色阶的有效点使用端点颜色，域外保持透明。"), dialog);
  hint->setWordWrap(true); layout->addWidget(hint);
  auto* form = new QFormLayout;
  auto* mode = new QComboBox(dialog);
  mode->setObjectName("depthScaleMode");
  mode->addItems({QStringLiteral("当前自动"), QStringLiteral("跨组统一 · 已选有效结果"), QStringLiteral("手动固定")});
  mode->setCurrentIndex(depthScaleMode_);
  form->addRow(QStringLiteral("范围模式"), mode);
  auto field = [&](const QString& title, const char* name, double value) {
    auto* f = new ParameterDoubleSpinBox(dialog); f->setObjectName(name);
    f->setRange(-1e9, 1e9); f->setDecimals(3); f->setValue(value * 1000);
    f->setKeyboardTracking(false); f->setButtonSymbols(QAbstractSpinBox::NoButtons);
    form->addRow(title, f); return f;
  };
  auto* lower = field(QStringLiteral("下限 / mm"), "depthScaleLower", manualDepthScale_.lower);
  auto* upper = field(QStringLiteral("上限 / mm"), "depthScaleUpper", manualDepthScale_.upper);
  layout->addLayout(form);
  auto* useCurrent = new QPushButton(QStringLiteral("用当前深度范围转为手动"), dialog);
  useCurrent->setObjectName("depthScaleUseCurrent"); layout->addWidget(useCurrent);
  auto* status = new QLabel(dialog); status->setObjectName("depthScaleStatus"); status->setWordWrap(true);
  layout->addWidget(status);
  auto refresh = [=] {
    const bool manual = mode->currentIndex() == 2;
    lower->setEnabled(manual); upper->setEnabled(manual);
    useCurrent->setEnabled(cachedSpatialRange(spatialView_).available);
    if (manual) {
      status->setText(QStringLiteral("固定范围用于所有采集组及双目/结构光深度图；更换图像或切换工作流/工作台后保持。"));
      return;
    }
    int ready = 0, selected = 0;
    const auto range = depthScaleRange(mode->currentIndex(), &ready, &selected);
    if (range.available) { lower->setValue(range.lower * 1000); upper->setValue(range.upper * 1000); }
    status->setText(mode->currentIndex() == 1
        ? QStringLiteral("已选 %1 组，其中 %2 组在当前观察节点有有效新结果。未运行、过期、失败及空结果不参与；新增有效结果后统一范围会更新。%3")
              .arg(selected).arg(ready).arg(range.available ? QString() : QStringLiteral("暂无可用组，暂用当前自动范围。"))
        : QStringLiteral("按当前深度图的有效像素自动取范围；无效域与非有限值不参与。"));
  };
  auto previousMode = std::make_shared<int>(mode->currentIndex());
  auto manualValues = std::make_shared<QPair<double, double>>(lower->value(), upper->value());
  connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), dialog, [=](int value) {
    if (*previousMode == 2) *manualValues = {lower->value(), upper->value()};
    if (value == 2) { lower->setValue(manualValues->first); upper->setValue(manualValues->second); }
    *previousMode = value; refresh();
  });
  for (auto* control : {lower, upper})
    connect(control, QOverload<double>::of(&QDoubleSpinBox::valueChanged), dialog, [=] {
      if (mode->currentIndex() == 2) refresh();
    });
  connect(useCurrent, &QPushButton::clicked, dialog, [=] {
    const auto range = cachedSpatialRange(spatialView_);
    if (!range.available) return;
    mode->setCurrentIndex(2);
    lower->setValue(range.lower * 1000);
    upper->setValue(range.upper * 1000);
    if (lower->value() == upper->value()) { lower->setValue(lower->value() - .001); upper->setValue(upper->value() + .001); }
  });
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用"));
  buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消")); layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
    if (mode->currentIndex() == 2 && lower->value() >= upper->value()) {
      status->setText(QStringLiteral("下限必须小于上限，尚未更改色阶。")); return;
    }
    depthScaleMode_ = mode->currentIndex();
    if (depthScaleMode_ == 2) manualDepthScale_ = {lower->value() / 1000, upper->value() / 1000, true};
    if (persistSettings_) {
      QSettings settings;
      settings.setValue("view/depthMode", depthScaleMode_);
      settings.setValue("view/depthLowerM", manualDepthScale_.lower);
      settings.setValue("view/depthUpperM", manualDepthScale_.upper);
    }
    spatialPreviews_.clear(); selectSpatialRaster(); dialog->accept();
  });
  refresh(); dialog->show();
}
}  // namespace workbench
