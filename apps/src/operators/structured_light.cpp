// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
// Offline acquisition uses only the frozen public SDK. No projector driver or
// undocumented model layout is used; model files are read/written by the SDK.
#include "model/spatial_data.hpp"
#include "runtime/execution.hpp"
#include "operators/operator_registry.hpp"

#include <QCryptographicHash>
#include <QCache>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QUuid>
#include <cmath>
#include <array>
#include <stdexcept>

namespace workbench {
struct StructuredDecodedState {
  mv::MvStructuredLightModel model;
  QVector<mv::MvImage> frames;
  QStringList keys;
  QByteArray config;
};
namespace {
void require(bool valid, const char* message) {
  if (!valid) throw std::runtime_error(message);
}
QByteArray read(const QString& path) {
  QFile f(path);
  require(f.open(QIODevice::ReadOnly) && f.size() > 0 && f.size() <= 65536,
          "结构光模型无法读取或超过 64 KB");
  return f.readAll();
}
void write(const QString& path, const QByteArray& bytes) {
  QSaveFile f(path);
  require(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit(),
          "结构光资源写入失败");
}
mv::MvTuple tuple(const QJsonArray& array) {
  mv::MvTuple result;
  for (const auto& v : array) {
    if (v.isString()) result.Append(v.toString().toUtf8().constData());
    else {
      require(v.isDouble() && std::isfinite(v.toDouble()), "结构光标定参数无效");
      result.Append(v.toDouble());
    }
  }
  return result;
}
mv::MvImage exposures(const Document& d) {
  mv::MvObject all = d.structuredFrames.first();
  for (int i = 1; i < d.structuredFrames.size(); ++i)
    all = all.ConcatObj(d.structuredFrames[i]);
  return mv::MvImage(all);
}
void configureDecode(const mv::MvStructuredLightModel& model, const QJsonObject& options) {
  model.SetStructuredLightModelParam("persistence", mv::MvTuple("true"));
  model.SetStructuredLightModelParam("quality_output", mv::MvTuple("true"));
  model.SetStructuredLightModelParam("min_gray_difference", qint64(options.value("minContrast").toInt(10)));
  model.SetStructuredLightModelParam("min_phase_amplitude", mv::MvTuple(options.value("minAmplitude").toDouble(2)));
  // Preserve observations: no hidden hole filling or interpolated 3D points.
  model.SetStructuredLightModelParam("hole_max_width", std::int64_t(0));
}
void raster(SpatialData& s, const QString& key, const mv::MvImage& image,
            const QString& label, const QString& semantic, const QString& unit = {}) {
  s.rasters[key] = {image, label, semantic, unit, s.frame};
}
void metric(Result& r, const QString& name, double value, const QString& unit) {
  ResultObject o;
  o.type = "Value"; o.label = name; o.measure = value; o.unit = unit;
  o.values["value"] = value;
  r.objects.append(o);
}
}  // namespace
mv::MvStructuredLightModel readStructuredModel(const QByteArray& bytes) {
  require(!bytes.isEmpty() && bytes.size() <= 65536, "请加载有效 MVKit .mvslm 模型");
  QTemporaryFile file;
  require(file.open() && file.write(bytes) == bytes.size() && file.flush(), "无法暂存结构光模型");
  const auto path = file.fileName();
  file.close();
  mv::MvStructuredLightModel model;
  model.ReadStructuredLightModel(path.toUtf8().constData());
  return model;
}
QJsonObject structuredModelInfo(const QByteArray& bytes) {
  // The manager refreshes periodically. Querying immutable metadata must not
  // regenerate a million-pixel projection sequence on each refresh.
  static QMutex mutex;
  static QCache<QByteArray, QJsonObject> cache(32);
  QMutexLocker lock(&mutex);
  const auto key = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
  if (const auto* known = cache.object(key)) return *known;
  auto model = readStructuredModel(bytes);
  const auto width = model.GetStructuredLightModelParam("pattern_width").I();
  const auto height = model.GetStructuredLightModelParam("pattern_height").I();
  require(width > 0 && height > 0 && width * height <= 1000000,
          "结构光投影图案最多 100 万像素");
  const auto type = QString::fromUtf8(model.GetStructuredLightModelParam("pattern_type").S().Text());
  require(type == "gray_code" || type == "gray_code_and_phase_shift",
          "当前 Studio 支持 Gray Code 或 Gray Code + 四步相移模型");
  const auto frames = model.GenStructuredLightPattern().CountObj();
  require(frames > 0 && frames <= 64, "结构光图案序列须为 1–64 帧");
  bool calibrated = false;
  try {
    mv::MvCameraSetupModel setup(model.GetStructuredLightModelParam("camera_setup_model").H());
    calibrated = setup.GetCameraSetupParam(0, "params").Length() > 0;
  } catch (...) {
    // A projection-only model is valid for generation/decoding, not for 3D.
  }
  const QJsonObject info{{"width", int(width)}, {"height", int(height)}, {"frames", int(frames)},
          {"type", type}, {"orientation", QString::fromUtf8(model.GetStructuredLightModelParam("pattern_orientation").S().Text())},
          {"calibrated", calibrated}};
  cache.insert(key, new QJsonObject(info));
  return info;
}
void validateStructuredCapture(const Document& d) {
  require(!d.captureId.isEmpty() && !d.structuredFrames.isEmpty() && d.structuredFrames.size() <= 64 &&
              d.structuredPaths.size() == d.structuredFrames.size() &&
              d.structuredKeys.size() == d.structuredFrames.size(),
          "曝光帧信息不完整，请添加完整序列");
  require(d.image.CountObj() == 1 && d.image.Width() * d.image.Height() <= 1000000 &&
              d.image.Width() * d.image.Height() * d.structuredFrames.size() <= 64000000,
          "结构光相机每帧最多 100 万像素，每组最多 6400 万曝光像素");
  for (const auto& frame : d.structuredFrames)
    require(frame.CountObj() == 1 && frame.Width() == d.image.Width() &&
                frame.Height() == d.image.Height() && frame.Channels() == 1 &&
                frame.Type() == mv::MvPixelType::UInt8,
            "结构光曝光须为相同尺寸的原始 byte 单通道图像");
  if (d.structuredModel.isEmpty()) return;
  const auto info=structuredModelInfo(d.structuredModel);
  require(d.structuredFrames.size()==info["frames"].toInt(),"曝光数量与模型生成顺序不匹配，请检查解码节点模型与序列");
  if (info["calibrated"].toBool()) {
    auto model = readStructuredModel(d.structuredModel);
    mv::MvCameraSetupModel setup(model.GetStructuredLightModelParam("camera_setup_model").H());
    auto camera = setup.GetCameraSetupParam(0, "params");
    require(camera.Length() >= 9 && camera.D(camera.Length() - 2) == double(d.image.Width()) &&
                camera.D(camera.Length() - 1) == double(d.image.Height()),
            "结构光曝光规格与模型中的相机标定不符");
  }
}
Document loadStructuredSequence(const QStringList& paths, const QString& path, const std::atomic_bool* cancelled) {
  require(!paths.isEmpty() && paths.size()<=64,"曝光序列须为 1–64 帧");
  const auto bytes = path.isEmpty() ? QByteArray() : read(path);
  if (!bytes.isEmpty())require(paths.size()==structuredModelInfo(bytes)["frames"].toInt(),"所选曝光数与模型不符");
  Document d;
  for (const auto& filename : paths) {
    if(cancelled && cancelled->load())return {};
    auto image = loadImage(filename);
    if (d.structuredFrames.isEmpty()) d = image;
    require(!d.structuredPaths.contains(image.imagePath), "同一个曝光文件不能重复占据两个序号");
    d.structuredFrames.append(image.image);
    d.structuredPaths.append(image.imagePath);
    d.structuredKeys.append(image.pixelKey);
  }
  d.captureId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  d.captureName = QFileInfo(paths.first()).absoluteDir().dirName();
  d.stereoName = QFileInfo(path).completeBaseName();
  d.structuredModel = bytes;
  validateStructuredCapture(d);
  return d;
}
QString generateStructuredPatterns(const QJsonObject& config, const QString& parent) {
  require(QDir(parent).exists(), "请选择已有输出目录");
  const int width = config["width"].toInt(128), height = config["height"].toInt(80);
  require(width >= 8 && height >= 8 && qint64(width) * height <= 1000000,
          "投影图案尺寸无效或超过 100 万像素");
  mv::MvStructuredLightModel model;
  model.CreateStructuredLightModel("3d_reconstruction");
  model.SetStructuredLightModelParam("pattern_width", qint64(width));
  model.SetStructuredLightModelParam("pattern_height", qint64(height));
  model.SetStructuredLightModelParam("min_stripe_width", qint64(config["stripe"].toInt(8)));
  model.SetStructuredLightModelParam("pattern_type", mv::MvTuple(config.value("type").toString("gray_code_and_phase_shift").toUtf8().constData()));
  model.SetStructuredLightModelParam("pattern_orientation", mv::MvTuple(config.value("orientation").toString("both").toUtf8().constData()));
  model.SetStructuredLightModelParam("normalization", mv::MvTuple("global"));
  if (config.contains("camera")) {
    require(config["lengthUnit"] == "m", "相机/投影仪标定长度单位须为 m");
    mv::MvCameraSetupModel setup;
    setup.CreateCameraSetupModel(2);
    const auto camera = tuple(config["camera"].toArray());
    const auto projector = tuple(config["projector"].toArray());
    const auto pose = tuple(config["projectorInCamera"].toArray());
    require(pose.Length() == 7, "投影仪相对相机位姿无效");
    auto identity = mv::MvTuple(0.).Append(0.).Append(0.).Append(0.).Append(0.).Append(0.).Append(0);
    setup.SetCameraSetupCamParam(0, "area_scan_division", mv::MvCamPar(camera), identity);
    setup.SetCameraSetupCamParam(1, "area_scan_division", mv::MvCamPar(projector), pose);
    model.SetStructuredLightModelParam("camera_setup_model", mv::MvTuple(setup));
  }
  auto patterns = model.GenStructuredLightPattern();
  require(patterns.CountObj() <= 64, "生成图案超过 64 帧");
  QTemporaryDir staging(QDir(parent).filePath(".mvkit-patterns-XXXXXX"));
  require(staging.isValid(), "无法创建图案目录");
  const QDir root(staging.path());
  const auto modelPath = root.filePath("projector.mvslm");
  model.WriteStructuredLightModel(modelPath.toUtf8().constData());
  structuredModelInfo(read(modelPath));
  QJsonArray frames;
  for (int i = 0; i < int(patterns.CountObj()); ++i) {
    const auto name = QString("pattern-%1.png").arg(i, 3, 10, QChar('0'));
    mv::MvImage(patterns.SelectObj(i + 1)).WriteImage("png", 0, root.filePath(name).toUtf8().constData());
    frames.append(name);
  }
  write(root.filePath("patterns.mvpatterns"), QJsonDocument(QJsonObject{
      {"format", "mvkit-projection-patterns"}, {"version", 1}, {"model", "projector.mvslm"},
      {"frames", frames}, {"note", "投影顺序即数组顺序；采集每帧原始相机曝光。此目录不是实拍数据。"}}).toJson());
  const auto target = QDir(parent).filePath("mvkit-patterns-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
  publishCaptureDirectory(staging.path(), target);
  staging.setAutoRemove(false);
  return target;
}
QString structuredImageKey(const Parameters& p) {
  return QStringList{"reference", "projectorColumn", "projectorRow", "contrast", "confidence", "decodeValid", "decodeReason"}
      .value(p.options.value("component").toInt(0));
}
QString structuredReasonText(int mask) {
  if (!mask) return QStringLiteral("有效");
  QStringList reasons;
  for (const auto& reason : QVector<QPair<int, QString>>{
      {1, QStringLiteral("域外")}, {2, QStringLiteral("无对应")}, {4, QStringLiteral("不一致")},
      {8, QStringLiteral("低对比度")}, {16, QStringLiteral("相移幅值不足")},
      {32, QStringLiteral("几何失败")}, {64, QStringLiteral("歧义")},
      {128, QStringLiteral("投影坐标越界")}, {256, QStringLiteral("曝光饱和")}})
    if (mask & reason.first) reasons.append(reason.second);
  if (mask & ~511) reasons.append(QStringLiteral("未知原因"));
  return reasons.join(QStringLiteral(" / "));
}
Result executeStructured(const Document& d, const NodeOutput* input) {
  QElapsedTimer timer;
  timer.start();
  d.parameters.validate();
  const auto tool = d.parameters.algorithm;
  auto s = std::make_shared<SpatialData>();
  Result result;
  if (tool == Algorithm::StructuredImage) {
    require(input && input->state == NodeState::Success && input->spatial && input->spatial->group == d.captureId,
            "请连接本组有效结构光输入");
    const auto key = structuredImageKey(d.parameters);
    require(input->spatial->rasters.contains(key), "所选模型没有该方向/图像分量，请选择可用分量");
    result.outputImage = input->spatial->rasters[key].image;
  } else if (tool == Algorithm::StructuredDecode) {
    require(!d.structuredModel.isEmpty(),"请在结构光解码节点选择结构光模型文件");
    validateStructuredCapture(d);
    require(d.activeRange.scope == ScopeMode::Full && d.roi.kind == RoiKind::None &&
                d.activeRange.exclusions.isEmpty() && d.activeRange.poseSource == -1,
            "结构光解码使用完整曝光序列；请在点云选取或二维分支设置 ROI");
    auto model = readStructuredModel(d.structuredModel);
    configureDecode(model, d.parameters.options);
    model.GenStructuredLightPattern();
    model.DecodeStructuredLightPattern(exposures(d));
    const auto info = structuredModelInfo(d.structuredModel);
    s->group = d.captureId;
    s->frame = d.captureId + "/camera/" + QString::fromLatin1(QCryptographicHash::hash(d.structuredModel, QCryptographicHash::Sha256).toHex().left(16));
    s->calibration = QJsonDocument(QJsonObject{{"format", "mvkit-structured-result"}, {"version", 1},
        {"lengthUnit", "m"}, {"modelSha", QString::fromLatin1(QCryptographicHash::hash(d.structuredModel, QCryptographicHash::Sha256).toHex())},
        {"decode", d.parameters.options}}).toJson(QJsonDocument::Compact);
    auto state = std::make_shared<StructuredDecodedState>();
    state->model = model;
    state->frames = d.structuredFrames;
    state->keys = d.structuredKeys;
    state->config = s->calibration;
    s->structuredState = state;
    raster(*s, "reference", d.structuredFrames.first(), QStringLiteral("相机参考图 · 首帧曝光"), "intensity");
    auto correspondence = model.GetStructuredLightObject("correspondence_image");
    int at = 1;
    if (info["orientation"] != "horizontal")
      raster(*s, "projectorColumn", mv::MvImage(correspondence.SelectObj(at++)), QStringLiteral("投影列坐标 / px"), "correspondence", "px");
    if (info["orientation"] != "vertical")
      raster(*s, "projectorRow", mv::MvImage(correspondence.SelectObj(at++)), QStringLiteral("投影行坐标 / px"), "correspondence", "px");
    raster(*s, "contrast", mv::MvImage(model.GetStructuredLightObject("contrast_image")), QStringLiteral("曝光对比度"), "contrast");
    raster(*s, "confidence", mv::MvImage(model.GetStructuredLightObject("confidence_image")), QStringLiteral("归一化曝光对比度 · 非概率"), "confidence");
    auto valid = mv::MvImage(model.GetStructuredLightObject("validity_image"));
    // Coordinate images contain NaN outside decoded correspondences. Publish
    // their actual valid domain so ordinary downstream statistics/processing
    // consume the same pixels as reconstruction, without a bridge operator.
    const auto decodedDomain = regionMask(valid.Threshold(1, 255));
    for (const auto& key : {"projectorColumn", "projectorRow"})
      if (s->rasters.contains(key)) s->rasters[key].image = s->rasters[key].image.ReduceDomain(decodedDomain);
    raster(*s, "decodeValid", valid, QStringLiteral("解码有效域"), "validity");
    auto reason = mv::MvImage(model.GetStructuredLightObject("reason_image"));
    raster(*s, "decodeReason", reason, QStringLiteral("解码拒绝原因"), "reason");
    s->primary = s->rasters.contains("projectorColumn") ? "projectorColumn" : "projectorRow";
    mv::MvTuple centerRow, centerCol;
    const auto area = regionMask(valid.Threshold(1, 255)).AreaCenter(&centerRow, &centerCol).D();
    metric(result, QStringLiteral("曝光帧数"), d.structuredFrames.size(), "帧");
    metric(result, QStringLiteral("解码有效像素"), area, "px");
    metric(result, QStringLiteral("有效比例"), 100. * area / (valid.Width() * valid.Height()), "%");
    std::array<qint64, 512> histogram{};
    for (int row = 0; row < int(reason.Height()); ++row) {
      const auto* line = reinterpret_cast<const unsigned short*>(static_cast<const char*>(reason.Data()) + row * reason.Stride());
      for (int col = 0; col < int(reason.Width()); ++col) ++histogram[line[col] & 511];
    }
    for (int bit : {1, 2, 4, 8, 16, 32, 64, 128, 256}) {
      qint64 count = 0;
      for (size_t mask = 0; mask < histogram.size(); ++mask) if (mask & bit) count += histogram[mask];
      if (count) metric(result, structuredReasonText(bit), count, "px");
    }
  } else {
    require(tool == Algorithm::StructuredReconstruct && input && input->state == NodeState::Success && input->spatial,
            "请连接本次结构光解码结果");
    *s = *input->spatial;
    const auto config = QJsonDocument::fromJson(s->calibration).object();
    require(s->group == d.captureId && config["format"] == "mvkit-structured-result" &&
                config["modelSha"].toString() == QString::fromLatin1(QCryptographicHash::hash(d.structuredModel, QCryptographicHash::Sha256).toHex()),
            "结构光输入与当前序列/模型不一致");
    validateStructuredCapture(d);
    require(structuredModelInfo(d.structuredModel)["calibrated"].toBool(),
            "模型没有相机/投影仪标定；投影坐标不能直接当作深度");
    // Reuse only the model owned by this execution's upstream result, with the
    // same immutable exposure vector, ordered pixel keys and full configuration.
    // Serialized results have no model; reconstruct their state on demand.
    const auto state = s->structuredState;
    const bool reusable = state && state->config == s->calibration &&
        state->keys == d.structuredKeys && state->frames.constData() == d.structuredFrames.constData();
    mv::MvStructuredLightModel model;
    if (reusable) model = state->model;
    else {
      model = readStructuredModel(d.structuredModel);
      configureDecode(model, config["decode"].toObject());
      model.GenStructuredLightPattern();
      model.DecodeStructuredLightPattern(exposures(d));
      auto rebuilt = std::make_shared<StructuredDecodedState>();
      rebuilt->model = model; rebuilt->frames = d.structuredFrames;
      rebuilt->keys = d.structuredKeys; rebuilt->config = s->calibration;
      s->structuredState = rebuilt;
    }
    const auto cloud = model.ReconstructSurfaceStructuredLight();
    auto xs = cloud.GetObjectModel3dParams("point_coord_x");
    auto ys = cloud.GetObjectModel3dParams("point_coord_y");
    auto zs = cloud.GetObjectModel3dParams("point_coord_z");
    auto rows = cloud.GetObjectModel3dParams("mapping_row");
    auto cols = cloud.GetObjectModel3dParams("mapping_col");
    require(xs.Length() <= 1000000 && xs.Length() == ys.Length() && xs.Length() == zs.Length() &&
                xs.Length() == rows.Length() && xs.Length() == cols.Length(), "结构光点云映射无效");
    mv::MvTuple rr, cc;
    s->points.clear();
    const int width = int(d.image.Width()), height = int(d.image.Height());
    QVector<float> xx(width * height), yy(width * height), zz(width * height);
    for (size_t i = 0; i < xs.Length(); ++i) {
      const double x = xs.D(i), y = ys.D(i), z = zs.D(i);
      const auto row = rows.I(i), col = cols.I(i);
      require(row >= 0 && col >= 0 && row < height && col < width, "结构光点云超出相机像素坐标");
      if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || z <= 0) continue;
      s->points.append({x * 1000, y * 1000, z * 1000, int(row), int(col)});
      rr.Append(row); cc.Append(col);
      xx[int(row) * width + int(col)] = float(x);
      yy[int(row) * width + int(col)] = float(y);
      zz[int(row) * width + int(col)] = float(z);
    }
    require(!s->points.isEmpty(), "没有有效正深度点，请检查模型标定、曝光顺序与解码质量");
    mv::MvRegion region;
    region.GenRegionRuns(rr, cc, cc);
    mv::MvImage x, y, z, valid;
    x.GenImage1("real", width, height, xx.constData());
    y.GenImage1("real", width, height, yy.constData());
    z.GenImage1("real", width, height, zz.constData());
    valid.GenImageConst("byte", width, height);
    raster(*s, "x", x.ReduceDomain(region), QStringLiteral("X / m"), "coordinate", "m");
    raster(*s, "y", y.ReduceDomain(region), QStringLiteral("Y / m"), "coordinate", "m");
    raster(*s, "depth", z.ReduceDomain(region), QStringLiteral("相机深度 Z / m"), "depth", "m");
    raster(*s, "valid", valid.PaintRegion(region, 255., "fill"), QStringLiteral("有效三维点"), "validity");
    s->primary = "depth";
    metric(result, QStringLiteral("有效三维点"), s->points.size(), "点");
  }
  if (tool != Algorithm::StructuredImage) {
    result.spatial = s;
    result.outputImage = s->rasters[s->primary].image;
  }
  result.preview = makePreview(result.outputImage).image;
  result.pipeline = operatorDefinition(tool).name;
  result.algorithmMs = result.totalMs = timer.nsecsElapsed() / 1e6;
  result.steps.append({result.pipeline, result.algorithmMs});
  return result;
}
}  // namespace workbench
