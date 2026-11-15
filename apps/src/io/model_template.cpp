// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>
#include <stdexcept>

#include "operators/range_model.hpp"
#include "runtime/worker_protocol.hpp"
namespace workbench {
namespace {
constexpr double degrees = 180. / 3.14159265358979323846;
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
QString sha(const QByteArray& bytes) {
  return QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
void write(const QString& path, const QByteArray& bytes) {
  QSaveFile file(path);
  require(file.open(QIODevice::WriteOnly) &&
              file.write(bytes) == bytes.size() && file.commit(),
          "无法保存模板文件");
}
// Validate through the public SDK reader, without interpreting its format.
void validate(ModelResource& model, bool readAngles) {
  QTemporaryDir temporary;
  require(temporary.isValid(), "无法创建模板验证目录");
  const auto file = temporary.filePath("model.mvmodel");
  write(file, model.artifact);
  double start = 0, extent = 0, step = 0;
  mv::MvString metric;
  if (model.algorithm == Algorithm::ShapeMatch) {
    mv::MvShapeModel native;
    native.ReadShapeModel(file.toUtf8().constData());
    double minimum, maximum, scaleStep;
    std::int64_t contrast;
    native.GetShapeModelParams(&start, &extent, &step, &minimum, &maximum,
                               &scaleStep, &metric, &contrast);
  } else {
    mv::MvNCCModel native;
    native.ReadNccModel(file.toUtf8().constData());
    native.GetNccModelParams(&start, &extent, &step, &metric);
  }
  if (readAngles) {
    model.angleStart = start * degrees;
    model.angleExtent = extent * degrees;
  }
}
}  // namespace
void saveTemplateFile(const ModelResource& model, const QString& path,
                      bool nativeModel) {
  require(
      !model.artifact.isEmpty() && model.artifactKey == modelBuildKey(model),
      "请先成功训练或加载有效模板，再保存模板文件");
  auto validated = model;
  validate(validated, false);
  if (nativeModel) {
    write(path, model.artifact);
    return;
  }
  auto meta = modelSettingsJson(model);
  meta["referencePath"] = QFileInfo(model.referencePath).fileName();
  const auto reference = model.referenceImage.CountObj()
                             ? encodeImage(model.referenceImage)
                             : QByteArray{};
  const QJsonObject object{
      {"format", "mvkit-studio-template"},
      {"version", 1},
      {"model", meta},
      {"reference", QString::fromLatin1(reference.toBase64())},
      {"referenceSha256", sha(reference)},
      {"artifact", QString::fromLatin1(model.artifact.toBase64())},
      {"artifactSha256", sha(model.artifact)}};
  const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
  require(bytes.size() <= 128 * 1024 * 1024, "模板文件过大");
  write(path, bytes);
}
ModelResource loadTemplateFile(const QString& path, Algorithm expected) {
  require(
      expected == Algorithm::ShapeMatch || expected == Algorithm::NccMatch,
      "当前算子不支持匹配模板");
  QFile file(path);
  require(file.open(QIODevice::ReadOnly) && file.size() > 0 &&
              file.size() <= 128LL * 1024 * 1024,
          "模板文件无法读取、为空或过大");
  const auto bytes = file.readAll();
  ModelResource model;
  if (bytes.trimmed().startsWith('{')) {
    QJsonParseError error;
    const auto object = QJsonDocument::fromJson(bytes, &error).object();
    require(error.error == QJsonParseError::NoError &&
                object["format"] == "mvkit-studio-template" &&
                object["version"] == 1,
            "模板文件格式或版本无效");
    model = modelSettingsFromJson(object["model"].toObject());
    require(model.algorithm == expected, "模板类型与当前匹配算子不一致");
    auto decode = [&](const char* key, const char* checksum) {
      const auto encoded = object[key].toString().toLatin1();
      const auto decoded = QByteArray::fromBase64(encoded);
      require(decoded.toBase64() == encoded &&
                  sha(decoded) == object[checksum].toString(),
              "模板文件内容校验失败");
      return decoded;
    };
    const auto reference = decode("reference", "referenceSha256");
    if (!reference.isEmpty()) {
      model.referenceImage = decodeImage(reference);
      model.preview = makePreview(model.referenceImage).image;
    }
    model.artifact = decode("artifact", "artifactSha256");
    require((model.artifactOnly || model.referenceImage.CountObj() == 1) &&
                !model.artifact.isEmpty() &&
                model.artifactKey == modelBuildKey(model),
            "模板文件缺少有效训练产物或参考资源");
    validate(model, false);
  } else {
    model.algorithm = expected;
    model.name = QFileInfo(path).completeBaseName();
    model.artifact = bytes;
    model.artifactOnly = true;
    validate(model, true);
    model.artifactKey = modelBuildKey(model);
  }
  // Import creates an explicit project resource; identical file contents
  // still have the same build key, so valid pose references can be retained.
  model.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  return model;
}
}  // namespace workbench
