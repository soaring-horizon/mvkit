// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include <QFileInfo>
#include <QJsonDocument>
#include <QUuid>

#include "ui/shell/main_window.hpp"
#include "operators/range_model.hpp"
namespace workbench {
void MainWindow::migrateRanges(QVector<ImageResource>& images,
                               const QVector<Parameters>& flow, int active,
                               const QString&) {
  QVector<NodeSettings> common(flow.size()), workbench(toolSlotCount);
  QVector<ModelResource> models;
  auto legacyModel = [&](const Document& image, const Parameters& p,
                         const Roi& roi) {
    ModelResource m;
    m.algorithm = p.algorithm;
    m.referenceImage = image.image;
    m.preview = image.preview;
    m.referencePath = image.imagePath;
    m.trainingRoi = roi;
    m.angleStart = p.angleStart;
    m.angleExtent = p.angleExtent;
    if (!p.templatePath.isEmpty()) {
      auto reference = loadImage(p.templatePath);
      m.referenceImage = reference.image;
      m.preview = reference.preview;
      m.referencePath = reference.imagePath;
      m.trainingRoi = {RoiKind::Rectangle, QRectF(0, 0, m.preview.width() - 1,
                                                  m.preview.height() - 1)};
    }
    for (const auto& saved : models)
      if (saved.algorithm == m.algorithm &&
          saved.referencePath == m.referencePath &&
          saved.trainingRoi == m.trainingRoi &&
          saved.angleStart == m.angleStart &&
          saved.angleExtent == m.angleExtent)
        return saved.id;
    m.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m.name = QStringLiteral("旧工程模板 %1").arg(models.size() + 1);
    if (models.size() >= 1024)
      throw std::runtime_error("旧工程模板过多，请拆分工程");
    models.append(m);
    return m.id;
  };
  for (auto& image : images) {
    auto& d = image.document;
    d.workflow = flow;
    if (d.nodeSettings.isEmpty()) {
      QVector<NodeSettings> ranges;
      for (int i = 0; i < flow.size(); ++i)
        ranges.append(workflowNodeSettings(d, i));
      d.nodeSettings = ranges;
    }
    d.rangeOverrides = QVector<bool>(flow.size(), true);
    for (int i = 0; i < flow.size(); ++i) {
      auto& s = d.nodeSettings[i];
      s.referenceSize =
          s.scope == ScopeMode::Manual ? d.preview.size() : QSize{};
      if (flow[i].algorithm == Algorithm::ShapeMatch ||
          flow[i].algorithm == Algorithm::NccMatch)
        s.modelId = legacyModel(d, flow[i], s.templateRoi);
    }
    d.workbenchRanges = QVector<NodeSettings>(toolSlotCount);
    d.workbenchOverrides = QVector<bool>(toolSlotCount, false);
    auto& s = d.workbenchRanges[int(d.parameters.algorithm)];
    s.referenceSize =
        d.roi.kind != RoiKind::None ? d.preview.size() : QSize{};
    if (d.parameters.algorithm == Algorithm::ShapeMatch ||
        d.parameters.algorithm == Algorithm::NccMatch) {
      s.modelId = legacyModel(d, d.parameters, d.roi);
      if (!d.parameters.templatePath.isEmpty() &&
          d.roi.kind != RoiKind::None) {
        s.scope = ScopeMode::Manual;
        s.roi = d.roi;
      }
    } else if (d.roi.kind != RoiKind::None) {
      s.scope = ScopeMode::Manual;
      s.roi = d.roi;
    }
    d.workbenchOverrides[int(d.parameters.algorithm)] = true;
    d.workflow.clear();
  }
  auto equivalent = [](const NodeSettings& a, const NodeSettings& b) {
    return nodeSettingsJson(a) == nodeSettingsJson(b);
  };
  for (int i = 0; i < flow.size(); ++i) {
    common[i] = images[active].document.nodeSettings[i];
    for (auto& image : images)
      image.document.rangeOverrides[i] =
          !equivalent(image.document.nodeSettings[i], common[i]);
  }
  for (int i = 0; i < toolSlotCount; ++i) {
    for (const auto& image : images)
      if (image.document.workbenchOverrides.value(i)) {
        workbench[i] = image.document.workbenchRanges[i];
        break;
      }
    for (auto& image : images)
      if (image.document.workbenchOverrides.value(i) &&
          equivalent(image.document.workbenchRanges[i], workbench[i]))
        image.document.workbenchOverrides[i] = false;
  }
  sharedRanges_ = common;
  workbenchRanges_ = workbench;
  models_ = models;
}
}  // namespace workbench
