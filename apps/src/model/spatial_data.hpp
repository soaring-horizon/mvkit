// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QMap>
#include <QVector>
#include <memory>

#include "model/document.hpp"
namespace workbench {
struct SpatialPoint {
  double x = 0, y = 0, z = 0;
  int row = 0, column = 0;
};  // mm; rectified-left (stereo) or original-camera (structured light) pixel
struct NamedRaster {
  mv::MvImage image;
  QString label, semantic, unit, frame;
};
inline QString spatialDisplayLabel(const NamedRaster& raster) {
  auto label = raster.label;
  if (raster.unit == "m") label.replace("/ m", "/ mm");
  return label;
}
struct StructuredDecodedState;
// Immutable per-execution result. Rendering never becomes an algorithm input.
struct SpatialData {
  QString group, frame, primary;
  QByteArray calibration;
  QMap<QString, NamedRaster> rasters;
  QVector<SpatialPoint> points;
  // Worker-local, immutable decoded model. Never serialized, persisted or
  // reused across jobs. Raster/point IPC remains the source of GUI results.
  std::shared_ptr<const StructuredDecodedState> structuredState;
};
// Rendering and cursor sampling must use the same native-pixel background.
// Imported XYZ data has no intensity image; its depth raster is the reference.
inline QString spatialReferenceKey(const SpatialData& data) {
  for (const auto& key : {QStringLiteral("reference"), QStringLiteral("left"), QStringLiteral("depth")}) {
    const auto it = data.rasters.constFind(key);
    if (it != data.rasters.cend() && it->image.CountObj()) return key;
  }
  return {};
}
struct Result;
struct NodeOutput;
std::shared_ptr<const SpatialData> sourceViewData(const SourceData& data,const QString& group);
bool spatialTool(Algorithm a);
mv::MvStructuredLightModel readStructuredModel(const QByteArray& bytes);
QJsonObject structuredModelInfo(const QByteArray& bytes);
void validateStructuredCapture(const Document& d);
Document loadStructuredSequence(const QStringList& paths, const QString& model, const std::atomic_bool* cancelled = nullptr);
QString generateStructuredPatterns(const QJsonObject& config, const QString& parent);
Result executeStructured(const Document& d, const NodeOutput* input = nullptr);
QString structuredImageKey(const Parameters& p);
QString structuredReasonText(int mask);
struct SpatialPreview {
  QImage image;
  QPainterPath domain;
  QString legend;
};
struct ScalarRange {
  double lower = 0, upper = 0;
  bool available = false;
};
ScalarRange spatialRange(const SpatialData& data, const QString& key);
SpatialPreview spatialPreview(const SpatialData& data, const QString& key, bool color,
                              ScalarRange scale = {});
QJsonObject stereoCalibration(const QByteArray& bytes);
QByteArray readStereoCalibration(const QString& path);
void validateStereoCapture(const Document& d, const QByteArray& calibration);
void publishCaptureDirectory(const QString& source, const QString& destination);
void savePointCloud(const SpatialData& data, const QString& path);
QJsonObject captureJson(const Document& d, const QString& project);
void restoreCapture(Document& d, const QJsonObject& json,
                    const QString& project);
Result executeSpatial(const Document& d, const NodeOutput* input = nullptr,
                      const NodeOutput* region = nullptr);
QByteArray encodeSpatial(const std::shared_ptr<const SpatialData>& data);
std::shared_ptr<const SpatialData> decodeSpatial(const QByteArray& bytes);
}  // namespace workbench
