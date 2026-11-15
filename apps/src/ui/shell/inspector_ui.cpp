// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "ui/shell/main_window.hpp"
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QTabBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QToolButton>
#include "operators/operator_registry.hpp"
#include "ui/widgets/parameter_grid.hpp"
namespace workbench {
void MainWindow::resetOperatorParameters() {
  if (runQueue_.active() || nodeContext() && selectedStep_ < 0) return;
  EditGuard edit(*this, QStringLiteral("恢复算子默认参数"));
  Parameters defaults;
  auto& p = document_.parameters;
  switch (p.algorithm) {
    case Algorithm::Regions:
      p.minimum = defaults.minimum; p.maximum = defaults.maximum;
      p.options = {};
      p.opening = defaults.opening; p.minimumArea = defaults.minimumArea;
      break;
    case Algorithm::Edges:
      p.sigma = defaults.sigma; p.edgeLow = defaults.edgeLow; p.edgeHigh = defaults.edgeHigh;
      break;
    case Algorithm::Filter: p.filter = defaults.filter; p.mask = defaults.mask; break;
    case Algorithm::Gray: p.gray = defaults.gray; break;
    default:
      p.score = defaults.score; p.matches = defaults.matches;
      p.angleStart = defaults.angleStart; p.angleExtent = defaults.angleExtent;
      break;
  }
  if (optionTool(p.algorithm)) {
    QJsonObject inputs;
    for (const auto& field : geometryFields_)
      if (field.first->property("tool").toInt() == int(p.algorithm) &&
          field.first->property("inputParameter").toBool() && p.options.contains(field.second))
        inputs.insert(field.second, p.options[field.second]);
    p.options = inputs;
  }
  syncParameters();
  parametersChanged();
}
void MainWindow::updateParameterGuidance() {
  const auto& p = document_.parameters;
  QString detail;
  switch (p.algorithm) {
    case Algorithm::Regions:
      {
        auto* grid = static_cast<ParameterGrid*>(parameterForms_.first());
        const int mode = p.options.value("thresholdMethod").toInt();
        grid->setFieldVisible(minimum_, mode == 0);
        grid->setFieldVisible(maximum_, mode == 0);
        grid->setFieldVisible(thresholdPolarity_, mode != 0);
        grid->setFieldVisible(thresholdWindow_, mode == 2);
        grid->setFieldVisible(thresholdOffset_, mode == 2);
      }
      detail = QStringLiteral("全局自动使用最大类间方差，局部动态比较邻域均值。固定灰度上下限包含端点，使用原始像素值；开运算半径为 0 时跳过形态学处理。面积单位为 px²。");
      break;
    case Algorithm::Edges:
      detail = QStringLiteral("平滑系数控制降噪；低阈值不得大于高阈值。输出亚像素轮廓，长度单位为 px。");
      break;
    case Algorithm::Filter:
      detail = p.filter == 2 ? QStringLiteral("中值滤波：奇数窗口，内部半径为窗口尺寸的一半向下取整。")
               : p.filter == 1 ? QStringLiteral("高斯滤波：窗口尺寸为奇数，当前使用 SDK 的离散高斯窗口。")
                               : QStringLiteral("均值滤波：窗口宽高相同，使用 3–31 的奇数尺寸。");
      break;
    case Algorithm::Gray:
      detail = p.gray ? QStringLiteral("灰度反相保持输入通道及有效范围。")
                      : QStringLiteral("三通道图像转为单通道；单通道输入保持原始像素。");
      break;
    case Algorithm::Polar:
    case Algorithm::GrayStats:
    case Algorithm::PlaneFit:
    case Algorithm::SphereFit:
    case Algorithm::ImageMorph:
    case Algorithm::ImageAffine:
    case Algorithm::ImageGradient:
    case Algorithm::ImageEnhance:
    case Algorithm::CalibrationDetect:
    case Algorithm::MonoCalibration:
    case Algorithm::StereoCalibration:
    case Algorithm::StructuredCalibration:
    case Algorithm::ImageRectify:
    case Algorithm::PlaneMeasure:
    case Algorithm::StereoRectify:
    case Algorithm::StereoDisparity:
    case Algorithm::StereoReconstruct:
    case Algorithm::CloudSelect:
    case Algorithm::CloudMeasure:
    case Algorithm::StereoImage:
    case Algorithm::DisparityFilter:
      case Algorithm::StructuredDecode:
      case Algorithm::StructuredReconstruct:
      case Algorithm::StructuredImage:
      detail = operatorDefinition(p.algorithm).description;
      break;
    case Algorithm::RegionMorph:
    case Algorithm::RegionFilter:
    case Algorithm::RegionFeatures:
    case Algorithm::RegionBoolean:
      detail = QStringLiteral(
          "区域工具使用真实 Region "
          "数据。数量模式对空集合返回零；面积、尺寸、圆度各有独立单位。工作流输"
          "入由连线限定。");
      break;
    case Algorithm::ScaleCalibration:
      detail = QStringLiteral(
          "仅适合等比例、同一平面和固定成像条件。用已知像素长度与实际毫米长度建"
          "立比例，不用于透视或畸变校正。只接受 px 长度。");
      break;
    case Algorithm::EllipseFit:
    case Algorithm::ContourProcess:
    case Algorithm::ContourFilter:
    case Algorithm::LineFit:
    case Algorithm::CircleFit:
    case Algorithm::Measure:
    case Algorithm::Judge:
      detail = QStringLiteral(
          "类型化输入来自已连通上游。几何保持原图坐标，长度为 px，夹角为 "
          "°。残差为全部输入点的 RMS。独立拟合测试显式使用当前图像 "
          "Canny；测量/判定需工作流输入。NG 表示检测超差，不是算法失败。");
      break;
    default:
      detail = QStringLiteral("得分范围 0–1；数量为最多返回的匹配实例。查找角度以 ° 为单位，训练范围与训练角度在模板页独立配置。");
      break;
  }
  advancedDetails_->setText(QStringLiteral("输出类型：%1\n%2\n参数或 ROI 改变后重新处理；恢复默认仅修改当前算子的处理参数。")
                               .arg(operatorDefinition(p.algorithm).output, detail));
  for (auto* field : QVector<QWidget*>{minimum_, maximum_, low_, high_, mask_}) {
    const bool invalid = field == minimum_ || field == maximum_
                            ? p.algorithm == Algorithm::Regions && p.options.value("thresholdMethod").toInt() == 0 && p.minimum > p.maximum
                         : field == low_ || field == high_
                            ? p.algorithm == Algorithm::Edges && p.edgeLow > p.edgeHigh
                            : p.algorithm == Algorithm::Filter && p.mask % 2 == 0;
    if (field->property("invalidParameter").toBool() != invalid) {
      field->setProperty("invalidParameter", invalid);
      field->style()->unpolish(field);
      field->style()->polish(field);
    }
  }
  minimum_->setToolTip(QStringLiteral("原始灰度下限，须不大于灰度上限"));
  maximum_->setToolTip(QStringLiteral("原始灰度上限，须不小于灰度下限"));
  mask_->setToolTip(QStringLiteral("窗口宽高相同；3–31 的奇数像素尺寸"));
}
int MainWindow::selectedResultObject() const {
  const auto* item = results_->item(results_->currentRow(), 0);
  return item && item->data(Qt::UserRole).isValid() ? item->data(Qt::UserRole).toInt() : -1;
}
void MainWindow::selectResultObject(int index, bool focus) {
  if (viewPinnedElsewhere() || index < 0 || index >= inspectedResult_.objects.size()) return;
  if (!calibrationDetails_->isHidden())
    calibrationDetails_->setCurrentIndex(inspectedResult_.objects[index].type == "Point" ? 1 : 0);
  for (int row = 0; row < results_->rowCount(); ++row) {
    auto* item = results_->item(row, 0);
    if (item && item->data(Qt::UserRole).toInt() == index) {
      results_->setCurrentCell(row, 0);
      results_->selectRow(row);
      results_->scrollToItem(item);
      outputDock_->show();outputDock_->raise();
      outputTabs_->setCurrentWidget(results_->parentWidget());
      if (focus) activeImageView()->focusResult(index);
      return;
    }
  }
}
void MainWindow::refreshRangeContext() {
  if (!roiPurpose_ || !nodeView_) return;
  auto* view = activeImageView();
  const bool pinned = viewPinnedElsewhere();
  const auto range = pinned ? rangeForNode(viewNodeIndex()) : effectiveRange();
  const bool training = trainingView() && !pinned;
  const auto tool = pinned && viewNodeIndex() >= 0
                        ? workflow_[viewNodeIndex()].algorithm : document_.parameters.algorithm;
  const bool match = tool == Algorithm::ShapeMatch || tool == Algorithm::NccMatch;
  QString role = training ? QStringLiteral("训练范围")
                         : match ? QStringLiteral("搜索范围") : QStringLiteral("处理范围");
  if (!pinned && roiPurpose_->currentIndex() > 0) role = roiPurpose_->currentText();
  QString relation = training ? QStringLiteral("模板参考坐标")
                      : range.scope == ScopeMode::Region
                          ? QStringLiteral("引用：%1").arg(nodeNames().value(range.regionSource, QStringLiteral("未连接")))
                      : range.poseSource != -1
                          ? QStringLiteral("跟随：%1").arg(nodeNames().value(range.poseSource, QStringLiteral("未连接")))
                          : QStringLiteral("固定图像坐标");
  QPointF origin = range.referencePosition;
  double angle = range.referenceAngle;
  bool frame = false;
  if (!training && range.poseSource >= 0 && range.poseReferenceSet && activeImage_ >= 0) {
    const auto& image = images_[activeImage_];
    if (image.workflowResultVersion == image.version &&
        range.poseSource < image.workflowResult.nodeOutputs.size()) {
      const auto& pose = image.workflowResult.nodeOutputs[range.poseSource];
      if (pose.state == NodeState::Success && range.poseInstance < pose.objects.size()) {
        origin = pose.objects[range.poseInstance].center;
        angle = pose.objects[range.poseInstance].angle;
        frame = true;
      }
    }
    relation += frame ? QStringLiteral(" · 当前坐标") : QStringLiteral(" · 等待定位");
  }
  if (range.iteratePose)
    relation +=
        QStringLiteral(" · 逐目标 / 参考 %1").arg(range.poseInstance + 1);
  if (pinned) relation += QStringLiteral(" · 只读");
  else if (!training)
    relation += (nodeContext() ? document_.rangeOverrides.value(selectedStep_)
                               : document_.workbenchOverrides.value(int(tool)))
                    ? QStringLiteral(" · 此图") : QStringLiteral(" · 共用");
  view->setRangeContext(role + QStringLiteral(" · ") + relation,
      training || range.scope != ScopeMode::Full || range.poseSource != -1 || !range.exclusions.isEmpty(),
      origin, angle, frame);
}
}  // namespace workbench
