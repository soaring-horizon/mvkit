// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "operators/range_model.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTransform>
#include <cmath>
#include <stdexcept>

#include "model/image_preview.hpp"
#include "runtime/worker_protocol.hpp"
namespace workbench {
namespace {
constexpr double radians = 3.14159265358979323846 / 180.;
void require(bool ok, const char* text) {
  if (!ok) throw std::runtime_error(text);
}
QString digest(const QByteArray& bytes) {
  return QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QByteArray readFile(const QString& path) {
  QFile f(path);
  require(f.open(QIODevice::ReadOnly) && f.size() <= 128LL * 1024 * 1024,
          "无法读取模型资源，或资源过大");
  return f.readAll();
}
void writeFile(const QString& path, const QByteArray& bytes) {
  QSaveFile f(path);
  require(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() &&
              f.commit(),
          "无法保存模型资源");
}
}  // namespace
QString referenceImageKey(const mv::MvImage& image) {
  return digest(encodeImage(image));
}
const ModelResource* findModel(const QVector<ModelResource>& models,
                               const QString& id) {
  for (const auto& m : models)
    if (m.id == id) return &m;
  return nullptr;
}
QJsonObject modelSettingsJson(const ModelResource& m) {
  NodeSettings s;
  s.roi = m.trainingRoi;
  s.exclusions = m.exclusions;
  return {{"id", m.id},
          {"name", m.name},
          {"algorithm", int(m.algorithm)},
          {"referencePath", m.referencePath},
          {"artifactOnly", m.artifactOnly},
          {"independent", m.independent},
          {"training", nodeSettingsJson(s)},
          {"angleStart", m.angleStart},
          {"angleExtent", m.angleExtent},
          {"artifactKey", m.artifactKey},
          {"originX", m.origin.x()},
          {"originY", m.origin.y()},
          {"buildMs", m.buildMs},
          {"generation", m.generation}};
}
ModelResource modelSettingsFromJson(const QJsonObject& o) {
  ModelResource m;
  m.id = o["id"].toString();
  m.name = o["name"].toString();
  m.referencePath = o["referencePath"].toString();
  m.artifactOnly = o["artifactOnly"].toBool(false);
  m.independent = o["independent"].toBool(false);
  const int a = o["algorithm"].toInt(-1);
  require(
      !m.id.isEmpty() && m.id.size() <= 128 && m.name.size() <= 256 &&
          (a == int(Algorithm::ShapeMatch) || a == int(Algorithm::NccMatch)),
      "模板类型或标识无效");
  m.algorithm = static_cast<Algorithm>(a);
  const auto s = nodeSettingsFromJson(o["training"].toObject());
  m.trainingRoi = s.roi;
  m.exclusions = s.exclusions;
  m.angleStart = o["angleStart"].toDouble(NAN);
  m.angleExtent = o["angleExtent"].toDouble(NAN);
  require(std::isfinite(m.angleStart) && std::isfinite(m.angleExtent) &&
              m.angleStart >= -180 && m.angleStart <= 180 &&
              m.angleExtent >= 0 && m.angleExtent <= 360,
          "模型训练角度无效");
  m.artifactKey = o["artifactKey"].toString();
  m.origin = {o["originX"].toDouble(), o["originY"].toDouble()};
  m.buildMs = o["buildMs"].toDouble();
  m.generation = o["generation"].toInt(0);
  require(m.generation >= 0, "模型训练版本无效");
  return m;
}
QString modelBuildKey(const ModelResource& m) {
  if (m.artifactOnly)
    return QStringLiteral("native-%1:").arg(int(m.algorithm)) +
           digest(m.artifact);
  auto o = modelSettingsJson(m);
  for (const char* key :
       {"id", "name", "referencePath", "artifactKey", "originX", "originY",
        "buildMs", "generation", "artifactOnly", "independent"})
    o.remove(key);
  // Keep classic ROI fingerprints byte-compatible with v6. Processing
  // iteration and unused shape fields are not part of template training.
  auto training = o["training"].toObject();
  training.remove("iteratePose");
  auto canonical = [](QJsonObject roi) {
    const int kind = roi["kind"].toInt();
    if (kind != int(RoiKind::RotatedRectangle)) roi.remove("angle");
    if (kind != int(RoiKind::Annulus)) roi.remove("innerRatio");
    return roi;
  };
  for (const auto* key : {"roi", "templateRoi"})
    training[key] = canonical(training[key].toObject());
  QJsonArray exclusions;
  for (const auto& value : training["exclusions"].toArray())
    exclusions.append(canonical(value.toObject()));
  training["exclusions"] = exclusions;
  o["training"] = training;
  o["pixels"] = referenceImageKey(m.referenceImage);
  return digest(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
mv::MvRegion trainingDomain(const ModelResource& m) {
  require(m.referenceImage.CountObj() == 1, "请设置模板参考图像");
  require(m.trainingRoi.kind != RoiKind::None, "请绘制训练范围");
  validateRoi(m.trainingRoi, int(m.referenceImage.Width()),
              int(m.referenceImage.Height()));
  auto domain =
      m.referenceImage.GetDomain().Intersection(makeRegion(m.trainingRoi));
  for (const auto& r : m.exclusions)
    if (r.kind != RoiKind::None) {
      validateRoi(r, int(m.referenceImage.Width()),
                  int(m.referenceImage.Height()));
      domain = domain.Difference(makeRegion(r));
    }
  return domain;
}
void buildModel(ModelResource& m) {
  require(!m.artifactOnly, "此模板不含训练参考，请先设置参考图像");
  auto domain = trainingDomain(m);
  mv::MvTuple row, col;
  const auto area = domain.AreaCenter(&row, &col);
  require(area.Length() == 1 && area.D() > 0, "训练范围没有有效像素");
  m.origin = {col.D(), row.D()};
  auto pattern = m.referenceImage.ReduceDomain(domain);
  if (pattern.Channels() == 3) pattern = pattern.Rgb1ToGray();
  QTemporaryDir temp;
  require(temp.isValid(), "无法创建模型训练目录");
  const auto path = temp.filePath("trained.mvmodel");
  QElapsedTimer timer;
  timer.start();
  if (m.algorithm == Algorithm::ShapeMatch) {
    const auto model = pattern.CreateShapeModel(
        1, m.angleStart * radians, m.angleExtent * radians, .05, "none",
        "use_polarity", 30, 10);
    model.WriteShapeModel(path.toUtf8().constData());
  } else {
    const auto model =
        pattern.CreateNccModel(1, m.angleStart * radians,
                               m.angleExtent * radians, .05, "use_polarity");
    model.WriteNccModel(path.toUtf8().constData());
  }
  m.buildMs = timer.nsecsElapsed() / 1e6;
  ++m.generation;
  m.artifact = readFile(path);
  m.artifactKey = modelBuildKey(m);
  QTransform offset;
  offset.translate(-m.origin.x(), -m.origin.y());
  m.outline = offset.map(imageDomainPath(pattern));
}
Result findWithModel(const Document& d, ModelResource& m) {
  require(m.algorithm == d.parameters.algorithm, "所选模型与算子类型不匹配");
  if (m.artifactOnly)
    require(!m.artifact.isEmpty() && m.artifactKey == modelBuildKey(m),
            "模板文件未就绪，请重新加载");
  else if (m.artifact.isEmpty() || m.artifactKey != modelBuildKey(m))
    buildModel(m);
  if (m.outline.isEmpty() && !m.artifactOnly) {
    QTransform offset;
    offset.translate(-m.origin.x(), -m.origin.y());
    m.outline = offset.map(
        imageDomainPath(m.referenceImage.ReduceDomain(trainingDomain(m))));
  }
  QTemporaryDir temp;
  require(temp.isValid(), "无法创建模型载入目录");
  const auto path = temp.filePath("model.mvmodel");
  writeFile(path, m.artifact);
  auto input = d.image;
  if (input.Channels() == 3) input = input.Rgb1ToGray();
  const auto& p = d.parameters;
  mv::MvTuple rows, columns, angles, scores;
  QElapsedTimer timer;
  if (m.algorithm == Algorithm::ShapeMatch) {
    mv::MvShapeModel model;
    model.ReadShapeModel(path.toUtf8().constData());
    timer.start();
    input.FindShapeModel(model, p.angleStart * radians,
                         p.angleExtent * radians, p.score, p.matches, .5,
                         "least_squares", 1, .9, &rows, &columns, &angles,
                         &scores);
  } else {
    mv::MvNCCModel model;
    model.ReadNccModel(path.toUtf8().constData());
    timer.start();
    input.FindNccModel(model, p.angleStart * radians, p.angleExtent * radians,
                       p.score, p.matches, .5, "true", 1, &rows, &columns,
                       &angles, &scores);
  }
  Result r;
  r.algorithmMs = timer.nsecsElapsed() / 1e6;
  for (std::size_t i = 0; i < rows.Length(); ++i) {
    ResultObject o;
    o.type = "Match";
    o.center = {columns.D(i), rows.D(i)};
    o.angle = -angles.D(i) / radians;  // SDK CCW -> Qt image clockwise
    o.values["sdkAngle"] = angles.D(i) / radians;
    o.values["angle"] = o.angle;
    o.measure = scores.D(i);
    QTransform pose;
    pose.translate(o.center.x(), o.center.y());
    pose.rotate(o.angle);
    o.overlay = pose.map(m.outline);
    if (o.overlay.isEmpty()) {
      QPainterPath cross;
      cross.moveTo(-7, 0);
      cross.lineTo(7, 0);
      cross.moveTo(0, -7);
      cross.lineTo(0, 7);
      o.overlay = pose.map(cross);
    }
    r.objects.append(o);
  }
  r.pipeline = m.algorithm == Algorithm::ShapeMatch
                   ? QStringLiteral("轮廓匹配")
                   : QStringLiteral("灰度匹配");
  r.measureName = QStringLiteral("匹配得分");
  r.steps.append({r.pipeline, r.algorithmMs});
  return r;
}
mv::MvRegion resolveRange(const Document& root, int node,
                          const NodeSettings& s, const mv::MvImage& image,
                          const QVector<NodeOutput>& outputs) {
  const QSize size(int(image.Width()), int(image.Height()));
  if (s.referenceSize.isValid() && s.referenceSize != size &&
      (s.scope == ScopeMode::Manual || !s.exclusions.isEmpty()))
    throw std::runtime_error(
        "图像规格与节点范围不一致，请启用此图覆盖；不会自动缩放 ROI");
  auto domain = image.GetDomain();
  mv::MvHomMat2D pose;
  const bool follow = s.poseSource != -1;
  if (follow) {
    require(
        node >= 0 &&
            workflowObjectSources(root, node, "Match").contains(s.poseSource),
        "定位输入未连接或类型不匹配");
    require(s.poseReferenceSet, "请设置定位参考位置");
    const auto& output = outputs[s.poseSource];
    require(s.referenceModelKey.isEmpty() ||
                s.referenceModelKey == output.modelKey,
            "定位模型已变化，请重新设置参考位置");
    require(output.state == NodeState::Success &&
                s.poseInstance < output.objects.size(),
            "定位未找到所选实例，当前节点不执行");
    const auto& match = output.objects[s.poseInstance];
    require(match.type == "Match", "定位结果类型无效");
    require(output.coordinateFrame == root.coordinateFrame,
            "定位与输入图像坐标系不一致");
    // Matcher/Qt poses use (Column,Row) pixel centers. AffineTransRegion's
    // public matrix uses (Row,Column) pixel corners: exchange the rotation
    // orientation and conjugate by the half-pixel translation.
    pose.VectorAngleToRigid(
        s.referencePosition.y() + .5, s.referencePosition.x() + .5,
        -s.referenceAngle * radians, match.center.y() + .5,
        match.center.x() + .5, -match.angle * radians);
  }
  auto geometry = [&](const Roi& r) {
    require(r.kind != RoiKind::None, "请绘制当前范围");
    validateRoi(
        r, s.referenceSize.isValid() ? s.referenceSize.width() : size.width(),
        s.referenceSize.isValid() ? s.referenceSize.height() : size.height());
    auto region = makeRegion(r);
    return follow ? region.AffineTransRegion(pose, "nearest_neighbor")
                  : region;
  };
  if (s.scope == ScopeMode::Manual)
    domain = domain.Intersection(geometry(s.roi));
  else if (s.scope == ScopeMode::Region) {
    require(node >= 0 && workflowObjectSources(root, node, "Region")
                             .contains(s.regionSource),
            "区域输入未连接或类型不匹配");
    require(outputs[s.regionSource].coordinateFrame == root.coordinateFrame,
            "区域与图像坐标系不一致");
    domain = domain.Intersection(regionMask(outputs[s.regionSource].regions));
  }
  for (const auto& r : s.exclusions)
    if (r.kind != RoiKind::None) domain = domain.Difference(geometry(r));
  return domain;
}
QJsonArray saveModels(const QVector<ModelResource>& models,
                      const QString& projectPath) {
  QJsonArray result;
  const auto folder =
      QFileInfo(projectPath)
          .absoluteDir()
          .filePath(QFileInfo(projectPath).fileName() + ".assets");
  if (!models.isEmpty())
    require(QDir().mkpath(folder), "无法创建工程资源目录");
  auto asset = [&](const QByteArray& bytes, const char* suffix) {
    const auto key = digest(bytes);
    const auto path = folder + "/" + key + suffix;
    if (!QFileInfo::exists(path)) writeFile(path, bytes);
    return QJsonObject{
        {"file", QFileInfo(projectPath).absoluteDir().relativeFilePath(path)},
        {"sha256", key}};
  };
  for (const auto& m : models) {
    auto o = modelSettingsJson(m);
    if (QFileInfo(m.referencePath).isAbsolute())
      o["referencePath"] = QFileInfo(projectPath)
                               .absoluteDir()
                               .relativeFilePath(m.referencePath);
    if (m.referenceImage.CountObj())
      o["reference"] = asset(encodeImage(m.referenceImage), ".mvimage");
    if (!m.artifact.isEmpty() && m.artifactKey == modelBuildKey(m))
      o["artifact"] = asset(m.artifact, ".mvmodel");
    result.append(o);
  }
  return result;
}
QVector<ModelResource> loadModels(const QJsonArray& array,
                                  const QString& projectPath) {
  require(array.size() <= 1024, "工程模型过多");
  QVector<ModelResource> result;
  auto asset = [&](const QJsonObject& o) {
    const auto bytes = readFile(QFileInfo(projectPath)
                                    .absoluteDir()
                                    .absoluteFilePath(o["file"].toString()));
    require(digest(bytes) == o["sha256"].toString(), "模型资源校验失败");
    return bytes;
  };
  for (const auto& value : array) {
    auto o = value.toObject();
    auto m = modelSettingsFromJson(o);
    require(!findModel(result, m.id), "模型标识重复");
    if (o.contains("reference")) {
      m.referenceImage = decodeImage(asset(o["reference"].toObject()));
      m.preview = makePreview(m.referenceImage).image;
    }
    if (o.contains("artifact")) m.artifact = asset(o["artifact"].toObject());
    if (!m.artifact.isEmpty() && m.artifactKey != modelBuildKey(m))
      m.artifact.clear();
    result.append(m);
  }
  return result;
}
}  // namespace workbench
