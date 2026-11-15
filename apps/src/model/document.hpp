// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QImage>
#include <atomic>
#include <QJsonObject>
#include <QMap>
#include <QPainterPath>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <QVector>
#include <mvkit/cpp/mvkit.hpp>

namespace workbench {
enum class RoiKind { None, Rectangle, Circle, RotatedRectangle, Annulus };
struct Roi {
  RoiKind kind = RoiKind::None;
  QRectF bounds;  // Image coordinates: x=column, y=row; integer positions are
                  // pixel centers.
  double angle = 0,
         innerRatio = .5;  // Qt image degrees; annulus inner/outer radius.
  bool operator==(const Roi& other) const {
    return kind == other.kind && bounds == other.bounds &&
           angle == other.angle && innerRatio == other.innerRatio;
  }
};
// Software tools are stable GUI concepts; SDK functions are implementation
// options.
enum class Algorithm {
  Regions,
  Edges,
  Filter,
  Gray,
  ShapeMatch,
  NccMatch,
  ContourFilter,
  LineFit,
  CircleFit,
  Measure,
  Judge,
  ContourProcess,
  EllipseFit,
  RegionMorph,
  RegionFilter,
  RegionFeatures,
  RegionBoolean,
  // Slot 17 is retired; preserve saved per-tool range indices.
  ScaleCalibration = 18,
  ImageMorph,
  ImageAffine,
  ImageGradient,
  ImageEnhance,
  CalibrationDetect,
  ImageRectify,
  PlaneMeasure,
  StereoRectify,
  StereoDisparity,
  StereoReconstruct,
  CloudSelect,
  CloudMeasure,
  StereoImage,
  DisparityFilter,
  StructuredDecode,
  StructuredReconstruct,
  StructuredImage,
  ImageInput,
  SequenceInput,
  MonoCalibration,
  StereoCalibration,
  StructuredCalibration,
  GrayStats,
  Polar,
  SphereFit,
  PlaneFit
};
constexpr int studioProjectVersion = 20;
constexpr int toolCount = 44;
constexpr int toolSlotCount = 45;
inline bool calibrationTool(Algorithm a) {
  return a==Algorithm::MonoCalibration || a==Algorithm::StereoCalibration || a==Algorithm::StructuredCalibration;
}
inline bool calibrationConsumer(Algorithm a) {
  return a==Algorithm::ImageRectify || a==Algorithm::PlaneMeasure || a==Algorithm::StereoRectify || a==Algorithm::StructuredDecode;
}
inline bool calibrationLink(Algorithm source,Algorithm target) {
  return source==Algorithm::MonoCalibration&&(target==Algorithm::ImageRectify||target==Algorithm::PlaneMeasure)||
      source==Algorithm::StereoCalibration&&target==Algorithm::StereoRectify||
      source==Algorithm::StructuredCalibration&&target==Algorithm::StructuredDecode;
}
inline bool sourceTool(Algorithm a) {
  return a == Algorithm::ImageInput || a == Algorithm::SequenceInput;
}
inline bool captureTool(Algorithm a) {
  return a == Algorithm::StereoRectify || a == Algorithm::StructuredDecode;
}
inline bool typedInputTool(Algorithm a);
inline bool dataTool(Algorithm a) {
  return a != Algorithm::Polar && a != Algorithm::GrayStats && !sourceTool(a) && int(a) >= int(Algorithm::ContourFilter) &&
         a != Algorithm::StructuredDecode &&
         (a < Algorithm::ImageMorph || a == Algorithm::PlaneMeasure ||
          a > Algorithm::StereoRectify);
}
inline bool typedInputTool(Algorithm a) { return dataTool(a) || captureTool(a); }
inline bool optionTool(Algorithm a) {
  return int(a) >= int(Algorithm::ContourFilter);
}
struct Parameters {
  Algorithm algorithm = Algorithm::Regions;
  double minimum = 105;
  double maximum = 255;
  double opening = 1.5;
  double minimumArea = 150;
  double sigma = 1.2;
  int edgeLow = 18;
  int edgeHigh = 40;
  int filter = 0;  // mean, Gaussian, median
  int mask = 7;
  int gray = 0;  // grayscale, invert
  QString templatePath;
  QString calibrationName;
  QJsonObject calibrationConfig;
  QByteArray calibrationArtifact;
  double score = .7;
  int matches = 1;
  double angleStart = -10;
  double angleExtent = 20;
  QJsonObject options;  // Private GUI tool-specific parameters; serialized with
                        // the project.
  void validate() const;
};
bool needsSecondInput(const Parameters& p);
enum class ScopeMode { Full, Manual, Region };
struct OutputBinding {
  int node = -2;
  QString output;
};
struct NodeSettings {
  ScopeMode scope = ScopeMode::Full;
  Roi roi;
  Roi templateRoi;
  // -1: no region selected; -2: a previously selected region was
  // disconnected.
  int regionSource = -1;
  QVector<Roi> exclusions;
  QSize referenceSize;
  // -1 fixed position, -2 missing pose binding, >=0 a reachable Match node.
  int poseSource = -1;
  QPointF referencePosition;
  double referenceAngle = 0;
  int poseInstance = 0;
  bool iteratePose = false;
  bool poseReferenceSet = false;
  QString referencePixelKey;
  QString referenceModelKey;
  QString modelId;
  int dataA = -2,
      dataB = -2;  // Explicit typed upstream bindings; -2 is disconnected.
  // -2: sole connected calibration; -1: frozen file resource.
  int calibrationSource = -2;
  // Private workflow contract: input slot -> upstream node and named output.
  // Legacy indices above remain adapters for ranges and pre-v20 projects.
  QMap<QString, OutputBinding> inputs;
};
struct ModelResource {
  QString id, name, referencePath;
  Algorithm algorithm = Algorithm::ShapeMatch;
  mv::MvImage referenceImage;
  QImage preview;
  // Native SDK model imported without an editable training reference.
  bool artifactOnly = false;
  bool independent = false;
  Roi trainingRoi;
  QVector<Roi> exclusions;
  double angleStart = -10, angleExtent = 20;
  QByteArray artifact;
  QString artifactKey;
  QPointF origin;
  QPainterPath outline;
  double buildMs = 0;
  int generation = 0;
};
// One cell of a complete execution sample. A sequence is atomic and ordered.
// Only private Studio data; public SDK types and ABI remain unchanged.
struct SourceData {
  bool sequence = false;
  QStringList paths, keys;
  QVector<mv::MvImage> frames;
  QStringList views;  // Named simultaneous views; empty for single images/sequences.
};
struct Document {
  QString imagePath;
  mv::MvImage image;
  QImage preview;
  QString displayMapping;
  Roi roi;
  Parameters parameters;
  QVector<Parameters> workflow;
  // -1: current image resource; -2: unconnected draft; >=0: node output.
  QVector<int> workflowInputs;
  QVector<QVector<int>> workflowLinks;
  // Per-image, per-node processing/template ranges in original pixel
  // coordinates.
  QVector<NodeSettings> nodeSettings;
  QVector<NodeSettings> sharedNodeSettings;
  QVector<bool> rangeOverrides;
  QVector<NodeSettings> workbenchRanges;
  QVector<bool> workbenchOverrides;
  QVector<ModelResource> models;
  NodeSettings activeRange;
  bool applyActiveRange = false;
  QString modelBinding;
  QJsonObject calibrationJob;  // Dedicated multi-view worker task.
  QString batchCalibrationDirectory;  // Transient worker context; never saved in projects.
  QJsonObject calibrationInputs; // Frozen selected sample manifests, by input binding.
  bool modelsRequired = false;
  QString coordinateFrame;
  QString pixelKey;
  // Capture-group identity is shared by stereo pairs and ordered structured
  // exposures. Single-image documents leave these fields empty. A group is one
  // job, regardless of how many raw exposures it contains.
  QString captureId, rightPath, rightPixelKey;
  QString captureName, stereoName;
  mv::MvImage rightImage;
  QImage rightPreview;
  QByteArray stereoArtifact;
  // An ordered exposure sequence is one job, not a set of independent images.
  // The reference image is exposure 0; all source pixels remain immutable.
  QVector<mv::MvImage> structuredFrames;
  QStringList structuredPaths, structuredKeys;
  QByteArray structuredModel;
  QMap<QString, SourceData> sourceInputs;
  // -1 migrates legacy projects to their last-created output; -2 is
  // unassigned.
  int workflowOutput = -1;
  // -1 executes the complete graph; >=0 executes this node and its ancestors.
  int workflowTarget = -1;
  QVector<int> workflowScope; // Current connected flow; empty means the complete graph.
  QStringList workflowLabels;
  Roi templateRoi;
  bool separateTemplateRoi = false;
  mv::MvImage templateImage;
  bool runWorkflow = false;
  bool hasImage() const { return !preview.isNull(); }
};
bool multiViewInput(const Parameters& p);
inline bool xyzInput(const Parameters& p) {
  return p.algorithm == Algorithm::ImageInput && p.options.value("inputMode").toInt() == 2;
}
mv::MvImage readInputImage(const QString& path);
QString outputType(const Parameters& p);
SourceData loadMultiViewData(const QStringList& paths, const QStringList& views, const std::atomic_bool* cancelled = nullptr);
SourceData loadSourceData(const QStringList& paths, bool sequence, const std::atomic_bool* cancelled = nullptr);
SourceData resolveSourceData(const Document& document, const QString& binding);
QJsonObject sourceInputsJson(const Document& document, const QString& project);
void readSourceInputs(Document& document, const QJsonObject& json, const QString& project);
QVector<int> workflowSources(const Document& document);
QVector<int> workflowOrder(const Document& document, bool allowDraft = false);
QVector<int> workflowImageSources(const Document& document, int target);
QVector<int> workflowObjectSources(const Document& document, int target,
                                   const QString& type);
NodeSettings workflowNodeSettings(const Document& document, int node);
NodeSettings resolveNodeInputs(const QVector<Parameters> &workflow,
                               const QVector<QVector<int>> &links, int target,
                               NodeSettings settings,
                               const QVector<ModelResource> &models = {});
int workflowOutputNode(const Document& document);
QJsonObject nodeSettingsJson(const NodeSettings& settings);
NodeSettings nodeSettingsFromJson(const QJsonObject& object);
QJsonObject modelSettingsJson(const ModelResource& model);
ModelResource modelSettingsFromJson(const QJsonObject& object);
QString modelBuildKey(const ModelResource& model);
const ModelResource* findModel(const QVector<ModelResource>& models, const QString& id);
QVector<QVector<int>> workflowConnections(const Document& document);
QVector<int> workflowAncestors(const QVector<QVector<int>>& links, int target);
QVector<int> workflowInputSources(const QVector<QVector<int>>& links,
                                  int target);
struct Preview {
  QImage image;
  QString mapping;
};
Preview makePreview(const mv::MvImage& image);
QString pixelText(const mv::MvImage& image, int row, int column);
QString pixelTypeText(mv::MvPixelType type);
void validateRoi(const Roi& roi, int width, int height);
mv::MvRegion makeRegion(const Roi& roi);
mv::MvRegion regionMask(const mv::MvRegion& regions);
Document loadImage(const QString& path);
QJsonObject parametersJson(const Parameters& parameters, const QString& path);
Parameters parametersFromJson(const QJsonObject& values, const QString& path, bool draft = false);
QJsonObject projectJson(const Document& document, const QString& projectPath, bool draft = false);
void saveProject(const Document& document, const QString& path);
Document loadProject(const QString& path);
Document documentFromJson(const QJsonObject& root, const QString& projectPath, bool draft = false);
QString roiText(const Roi& roi);
QPainterPath roiPath(const Roi& roi);
}  // namespace workbench
