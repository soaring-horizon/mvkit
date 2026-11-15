// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QMap>
#include <QObject>
#include <QPainterPath>
#include <QVector>
#include <functional>
#include <memory>
class QProcess;
class QTemporaryDir;

#include "model/document.hpp"

namespace workbench {
struct SpatialData;
struct ResultObject {
  QString type;
  double measure = 0;
  QPointF center;
  QPainterPath overlay;
  double angle = 0;
  // Analytic geometry, independent of display paths. Coordinates: x=column,
  // y=row.
  QPointF first, second;
  double radius = 0;
  QMap<QString, double> values;
  QString label, unit;
  double radius2 = 0;
  int instance =
      0;  // 1-based within current matching result; 0 means no iteration.
  QVector<QPointF> samples;
  QVector<double> residuals;
  int verdict = -1;  // -1 not a judgement, 0 NG, 1 OK.
};
enum class NodeState { Pending, Success, Error, Blocked };
struct InstanceStatus {
  int index = 0;
  NodeState state = NodeState::Pending;
  QString error;
  double algorithmMs = 0;
  int objectCount = 0;
};
struct NodeOutput {
  QByteArray calibrationResult;
  std::shared_ptr<const SourceData> sourceData; // Worker-local sequence carrier.
  std::shared_ptr<const Document> captureContext; // Frozen inputs of this branch.
  std::shared_ptr<const SpatialData> spatial;
  Parameters parameters;
  Roi roi;
  mv::MvImage inputImage;
  QImage inputPreview;
  mv::MvImage outputImage;
  QImage preview;
  QVector<ResultObject> objects;
  QString measureName;
  double algorithmMs = 0;
  double totalMs = 0;
  mv::MvRegion regions;
  mv::MvXLDCont contours;
  NodeState state = NodeState::Pending;
  QString error;
  mv::MvRegion effectiveDomain;
  QString coordinateFrame;
  QString modelKey;
  QVector<InstanceStatus> instances;
};
struct Result {
  std::shared_ptr<const SpatialData> spatial;
  QVector<ResultObject> objects;
  QString measureName;
  QString pipeline;
  double algorithmMs = 0;
  double totalMs = 0;
  QString error;
  mv::MvImage outputImage;
  QImage preview;
  QVector<QPair<QString, double>> steps;
  QVector<NodeOutput> nodeOutputs;
  mv::MvRegion regions;
  mv::MvXLDCont contours;
  int outputNode = -1;
  QVector<ModelResource> trainedModels;
  QVector<InstanceStatus> instances;
  QByteArray calibrationResult;
};
bool acceptsData(Algorithm tool, const QString& type, bool second = false);
bool executeInputNode(const Document& snapshot, int index,
                      const QVector<NodeOutput>& outputs, NodeOutput& output);
void applyCaptureContext(Document& stage, const Document& capture);
Result executeDetection(const Document& stage, const NodeOutput* a = nullptr,
                        const NodeOutput* b = nullptr);
QString fieldUnit(const ResultObject& object, const QString& field);
QString fieldLabel(const QString& field);
// Declared numerical output before a run; live object values take precedence.
ResultObject numericalOutputSchema(const Parameters& parameters);
Result executeGeometry(const Document& stage, const NodeOutput* a,
                       const NodeOutput* b);
Result execute(const Document& snapshot,
               const std::function<void(int)>& started = {},
               const QVector<NodeOutput>* overrides = nullptr);
Result executeInstances(const Document& snapshot,
                        const std::function<void(int)>& started);
class ExecutionService final : public QObject {
  Q_OBJECT
 public:
  explicit ExecutionService(QObject* parent = nullptr,
                            const QString& program = {});
  ~ExecutionService() override;
  bool busy() const { return worker_ != nullptr; }
  bool start(const Document& snapshot, quint64 revision);
  void cancel();
 signals:
  void completed(quint64 revision, const workbench::Result& result);
  void nodeStarted(quint64 revision, int node);

 private:
  QProcess* worker_ = nullptr;
  quint64 generation_ = 0;
  QString program_;
  std::shared_ptr<QTemporaryDir> directory_;
};
}  // namespace workbench
