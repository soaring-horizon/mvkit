// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
// Composes the frozen public rectification, SGM and XYZ APIs. All geometry is
// in rectified-left coordinates; display point coordinates are explicitly mm.
#include "model/spatial_data.hpp"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "runtime/execution.hpp"
#include "operators/operator_registry.hpp"
#include "runtime/worker_protocol.hpp"
namespace workbench {
namespace {
void require(bool b, const char* m) {
  if (!b) throw std::runtime_error(m);
}
mv::MvTuple tuple(QJsonArray a) {
  mv::MvTuple t;
  for (auto v : a) {
    if (v.isString())
      t.Append(v.toString().toUtf8().constData());
    else {
      require(v.isDouble() && std::isfinite(v.toDouble()),
              "标定参数不是有限数值");
      t.Append(v.toDouble());
    }
  }
  return t;
}
QJsonArray array(const mv::MvTuple& t) {
  QJsonArray a;
  for (size_t i = 0; i < t.Length(); ++i) {
    try {
      a.append(t.D(i));
    } catch (...) {
      a.append(QString::fromUtf8(t.S(i).Text()));
    }
  }
  return a;
}
QByteArray sha(const QByteArray& b) {
  return QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex();
}
mv::MvImage gray(mv::MvImage image) {
  require(
      image.CountObj() == 1 && (image.Channels() == 1 || image.Channels() == 3),
      "双目只接受单通道或 RGB 图像");
  for (size_t c = 0; c < image.Channels(); ++c)
    require(image.Type(c) == mv::MvPixelType::UInt8,
            "双目输入须为原始 byte 图像");
  return image.Channels() == 3 ? image.Rgb1ToGray() : image;
}
void add(SpatialData& s, const QString& key, const mv::MvImage& image,
         const QString& label, const QString& semantic,
         const QString& unit = {}) {
  s.rasters[key] = {image, label, semantic, unit,
                    key == "right" ? s.frame + "/right" : s.frame};
}

mv::MvTuple fitCloudPrimitive(const QVector<SpatialPoint>& points, const char* type, double minRadius = 0, double maxRadius = 0) {
  mv::MvTuple x, y, z;
  for (const auto& p : points) {
    x.Append(p.x);
    y.Append(p.y);
    z.Append(p.z);
  }
  mv::MvObjectModel3D cloud;
  cloud.GenObjectModel3dFromPoints(x, y, z);
  mv::MvTuple names("primitive_type"), values(type);
  names.Append("output_point_coord");
  values.Append("false");
  if (maxRadius > 0) {
    names.Append("min_radius").Append("max_radius");
    values.Append(minRadius).Append(maxRadius);
  }
  auto fit = cloud.FitPrimitivesObjectModel3d(names, values);
  auto parameters = fit.GetObjectModel3dParams("primitive_parameter");
  require(parameters.Length() == 4, "点云不足或退化，无法拟合指定几何体");
  return parameters;
}
struct CloudPlane {
  double nx, ny, nz, offset;  // Unit normal, N dot P = offset; points and offset in mm.
  double residual(const SpatialPoint& p) const { return nx*p.x + ny*p.y + nz*p.z - offset; }
};

CloudPlane fitPlane(const QVector<SpatialPoint>& points) {
  const auto fit = fitCloudPrimitive(points, "plane");
  return {fit.D(0), fit.D(1), fit.D(2), fit.D(3)};
}
}  // namespace
bool spatialTool(Algorithm a) {
  return (a >= Algorithm::StereoRectify && a <= Algorithm::StructuredImage) || a == Algorithm::SphereFit || a == Algorithm::PlaneFit;
}
QJsonObject stereoCalibration(const QByteArray& bytes) {
  require(!bytes.isEmpty() && bytes.size() <= 65536, "请加载有效双目标定文件");
  QJsonParseError error;
  auto j = QJsonDocument::fromJson(bytes, &error).object();
  require(error.error == QJsonParseError::NoError &&
              j["format"] == "mvkit-stereo-calibration" && j["version"] == 1 &&
              j["lengthUnit"] == "m",
          "双目标定格式或单位无效（要求 m）");
  for (auto key : {"leftCamera", "rightCamera"}) {
    auto a = j[key].toArray();
    require(a.size() == 9 && a[0] == "area_scan_division",
            "首版支持 area_scan_division 双目相机");
    mv::MvCamPar check(tuple(a));
    require(a[1].toDouble() > 0 && a[3].toDouble() > 0 && a[4].toDouble() > 0 &&
                a[7].toDouble() >= 1 && a[8].toDouble() >= 1,
            "双目相机参数无效");
  }
  const auto p = j["rightInLeft"].toArray();
  require(p.size() == 7 && p[6].toDouble() == 0, "双目位姿要求类型 0");
  tuple(p);
  require(std::hypot(std::hypot(p[0].toDouble(), p[1].toDouble()),
                     p[2].toDouble()) > 1e-9,
          "双目基线不能为零");
  return j;
}
QByteArray readStereoCalibration(const QString& path) {
  QFile f(path);
  require(f.open(QIODevice::ReadOnly) && f.size() > 0 && f.size() <= 65536,
          "双目标定文件无法读取或过大");
  auto b = f.readAll();
  stereoCalibration(b);
  return b;
}
QJsonObject captureJson(const Document& d, const QString& project) {
  if (d.captureId.isEmpty()) return {};
  if (d.structuredFrames.isEmpty() && !d.rightImage.CountObj())
    return {{"kind","sample"},{"id",d.captureId},{"name",d.captureName},{"referenceSha",d.pixelKey}};
  if (!d.structuredFrames.isEmpty()) {
    QJsonArray exposures;
    const auto root = QFileInfo(project).absoluteDir();
    for (int i = 0; i < d.structuredPaths.size(); ++i)
      exposures.append(QJsonObject{{"file", root.relativeFilePath(d.structuredPaths[i])},
                                   {"sha256", d.structuredKeys[i]}});
    return {{"kind", "structured-light"}, {"id", d.captureId}, {"name", d.captureName},
            {"modelName", d.stereoName}, {"frames", exposures},
            {"model", QString::fromLatin1(d.structuredModel.toBase64())},
            {"modelSha", QString::fromLatin1(sha(d.structuredModel))}};
  }
  return {
      {"kind", "stereo"},
      {"id", d.captureId},
      {"name", d.captureName},
      {"calibrationName", d.stereoName},
      {"leftSha", d.pixelKey},
      {"right", QFileInfo(project).absoluteDir().relativeFilePath(d.rightPath)},
      {"rightSha", d.rightPixelKey},
      {"calibration", QString::fromLatin1(d.stereoArtifact.toBase64())},
      {"calibrationSha", QString::fromLatin1(sha(d.stereoArtifact))}};
}
void restoreCapture(Document& d, const QJsonObject& j, const QString& project) {
  if (j.isEmpty()) return;
  if (j["kind"] == "sample") {
    require(!j["id"].toString().isEmpty() && j["id"].toString().size() <= 128 && j["referenceSha"].toString() == d.pixelKey, "样本身份或参考图内容无效");
    d.captureId=j["id"].toString(); d.captureName=j["name"].toString().left(80); return;
  }
  if (j["kind"] == "structured-light") {
    require(!j["id"].toString().isEmpty() && j["id"].toString().size() <= 128,
            "结构光采集身份无效");
    const auto frames = j["frames"].toArray();
    require(!frames.isEmpty() && frames.size() <= 64, "结构光曝光数量无效");
    const auto encoded = j["model"].toString().toLatin1();
    require(encoded.size() <= 90000, "结构光模型过大");
    d.structuredModel = QByteArray::fromBase64(encoded);
    require(d.structuredModel.toBase64() == encoded &&
                sha(d.structuredModel) == j["modelSha"].toString(), "结构光模型指纹不一致");
    for (const auto& value : frames) {
      const auto frame = value.toObject();
      const auto image = loadImage(QFileInfo(project).absoluteDir().absoluteFilePath(frame["file"].toString()));
      require(image.pixelKey == frame["sha256"].toString(), "结构光曝光像素已变化，请重新导入序列");
      d.structuredFrames.append(image.image);
      d.structuredPaths.append(image.imagePath);
      d.structuredKeys.append(image.pixelKey);
    }
    require(d.pixelKey == d.structuredKeys.first(), "结构光参考图须为曝光序列首帧");
    d.captureId = j["id"].toString();
    d.captureName = j["name"].toString().left(80);
    d.stereoName = j["modelName"].toString().left(120);
    validateStructuredCapture(d);
    return;
  }
  require(j["kind"] == "stereo" && !j["id"].toString().isEmpty() &&
              j["id"].toString().size() <= 128,
          "采集组类型或身份无效");
  const auto right = loadImage(
      QFileInfo(project).absoluteDir().absoluteFilePath(j["right"].toString()));
  require(d.pixelKey == j["leftSha"].toString() &&
              right.pixelKey == j["rightSha"].toString(),
          "采集组像素已改变，请重新导入左右图组");
  require(right.preview.size() == d.preview.size(), "左右图像尺寸不一致");
  const auto encoded = j["calibration"].toString().toLatin1();
  require(encoded.size() <= 90000, "双目标定资源过大");
  auto b = QByteArray::fromBase64(encoded);
  require(b.toBase64() == encoded && sha(b) == j["calibrationSha"].toString(),
          "双目标定资源损坏");
  if (!b.isEmpty()) stereoCalibration(b);
  d.captureId = j["id"].toString();
  d.captureName = j["name"].toString().left(80);
  d.stereoName = j["calibrationName"].toString().left(120);
  d.rightPath = right.imagePath;
  d.rightPixelKey = right.pixelKey;
  d.rightImage = right.image;
  d.rightPreview = right.preview;
  d.stereoArtifact = b;
  validateStereoCapture(d, b);
}
Result executeSpatial(const Document& d, const NodeOutput* input,
                      const NodeOutput* regionInput) {
  if (captureTool(d.parameters.algorithm) && !d.parameters.calibrationArtifact.isEmpty()) {
    const bool stereo=d.parameters.algorithm==Algorithm::StereoRectify;
    if ((stereo?d.stereoArtifact:d.structuredModel)!=d.parameters.calibrationArtifact) {
      auto bound=d;
      (stereo?bound.stereoArtifact:bound.structuredModel)=d.parameters.calibrationArtifact;
      return executeSpatial(bound,input,regionInput);
    }
  }
  if (d.parameters.algorithm >= Algorithm::StructuredDecode && d.parameters.algorithm <= Algorithm::StructuredImage)
    return executeStructured(d, input);
  QElapsedTimer timer;
  timer.start();
  d.parameters.validate();
  const auto a = d.parameters.algorithm;
  const auto& options = d.parameters.options;
  auto value = [&](const char* k, double def) {
    return options.value(k).toDouble(def);
  };
  auto s = std::make_shared<SpatialData>();
  if (a != Algorithm::StereoRectify) {
    require(input && input->state == NodeState::Success,
            "请连接本次有效的三维类型输入");
    if (input->spatial) *s = *input->spatial;
    else { s->group = d.captureId; s->frame = input->coordinateFrame;
      s->calibration = QJsonDocument(QJsonObject{{"format", "mvkit-image-results"}, {"version", 1}}).toJson(QJsonDocument::Compact); }
    require(s->group == d.captureId, "输入不属于当前采集组");
    if ((a == Algorithm::StereoDisparity || a == Algorithm::DisparityFilter) && regionInput) {
      require(regionInput && regionInput->outputImage.CountObj() == 1 && input->outputImage.CountObj() == 1,
              "请分别选择两个图像输入");
      const auto first = a == Algorithm::StereoDisparity ? "left" : "disparity";
      const auto second = a == Algorithm::StereoDisparity ? "right" : "score";
      if (!s->rasters.contains(first)) s->rasters[first] = {{}, a == Algorithm::StereoDisparity ? QStringLiteral("左图") : QStringLiteral("视差图"), a == Algorithm::StereoDisparity ? "intensity" : "disparity", a == Algorithm::StereoDisparity ? "" : "px", {}};
      if (!s->rasters.contains(second)) s->rasters[second] = {{}, a == Algorithm::StereoDisparity ? QStringLiteral("右图") : QStringLiteral("匹配代价"), a == Algorithm::StereoDisparity ? "intensity" : "score", {}, {}};
      s->rasters[first].image = input->outputImage; s->rasters[first].frame = input->coordinateFrame;
      s->rasters[second].image = regionInput->outputImage; s->rasters[second].frame = regionInput->coordinateFrame;
      s->frame = input->coordinateFrame;
      if (a == Algorithm::StereoDisparity) {
        require(input->outputImage.Type() == mv::MvPixelType::UInt8 && regionInput->outputImage.Type() == mv::MvPixelType::UInt8 &&
                    input->outputImage.Channels() == 1 && regionInput->outputImage.Channels() == 1 &&
                    input->outputImage.Height() == regionInput->outputImage.Height(),
                "视差输入须为等高的单通道 byte 左右图像");
      } else require(input->outputImage.Channels() == 1 && regionInput->outputImage.Channels() == 1,
                     "视差与代价须为单通道图像");
    } else if (a == Algorithm::StereoReconstruct && regionInput) {
      require(regionInput && regionInput->spatial && !regionInput->spatial->calibration.isEmpty(), "请选择校正几何模型");
      require(input->coordinateFrame == regionInput->spatial->frame, "视差坐标系与校正几何模型不一致");
      s->calibration = regionInput->spatial->calibration;
      s->rasters["disparity"] = {input->outputImage, {}, "disparity", "px", input->coordinateFrame};
    } else require(bool(input->spatial), "所选输入没有三维数据");
  }
  Result r;
  if (a == Algorithm::StereoImage) {
    const auto key = value("component", 0) == 0 ? QString("left") : QString("right");
    require(s->rasters.contains(key) && s->rasters[key].semantic == "intensity",
            "输入缺少所选校正图像分量");
    r.outputImage = s->rasters[key].image;
    r.preview = makePreview(r.outputImage).image;
    r.pipeline = operatorDefinition(a).name;
    r.measureName = s->rasters[key].label;
    r.algorithmMs = r.totalMs = timer.nsecsElapsed() / 1e6;
    r.steps.append({r.pipeline, r.algorithmMs});
    return r;
  }
  if (a == Algorithm::StereoRectify) {
    require(!d.captureId.isEmpty() && d.rightImage.CountObj() == 1,
            "请在数据资源中添加完整左右图组");
    require(d.activeRange.scope == ScopeMode::Full &&
                d.roi.kind == RoiKind::None &&
                d.activeRange.exclusions.isEmpty() &&
                d.activeRange.poseSource == -1,
            "双目校正使用完整图组；请在重建后使用二维 ROI 选取点云");
    require(d.coordinateFrame == d.imagePath, "双目校正须直接使用采集组原图");
    require(!d.stereoArtifact.isEmpty(), "请在双目校正节点选择双目标定文件");
    auto j = stereoCalibration(d.stereoArtifact);
    const auto left = j["leftCamera"].toArray(),
               right = j["rightCamera"].toArray();
    require(d.image.Width() == left[7].toInt() &&
                d.image.Height() == left[8].toInt() &&
                d.rightImage.Width() == right[7].toInt() &&
                d.rightImage.Height() == right[8].toInt(),
            "图像规格与双目标定不匹配");
    require(d.image.Width() * d.image.Height() <= 1000000,
            "首版双目重建每组最多 100 万像素");
    mv::MvCamPar c1, c2;
    mv::MvPose p1, p2, rel;
    mv::MvImage map2;
    auto map1 =
        mv::MvCamPar(tuple(left))
            .GenBinocularRectificationMap(
                &map2, mv::MvCamPar(tuple(right)),
                mv::MvPose(tuple(j["rightInLeft"].toArray())), 1.,
                "viewing_direction", "bilinear", &c1, &c2, &p1, &p2, &rel);
    s->group = d.captureId;
    s->frame = d.captureId + "/rectified-left/" +
               QString::fromLatin1(sha(d.stereoArtifact).left(16));
    add(*s, "left", gray(d.image).MapImage(map1), QStringLiteral("校正左图"),
        "intensity");
    add(*s, "right", gray(d.rightImage).MapImage(map2),
        QStringLiteral("校正右图"), "intensity");
    j["rectLeft"] = array(c1);
    j["rectRight"] = array(c2);
    j["rectPose"] = array(rel);
    s->calibration = QJsonDocument(j).toJson(QJsonDocument::Compact);
    s->primary = "left";
  } else if (a == Algorithm::StereoDisparity) {
    require(s->rasters.contains("left") && s->rasters.contains("right"),
            "输入缺少校正图像对");
    mv::MvImage score;
    auto disparity = s->rasters["left"].image.BinocularDisparityMs(
        s->rasters["right"].image, &score, int(value("minDisparity", -64)),
        int(value("maxDisparity", 0)), int(value("surface", 6)),
        int(value("edge", 35)), mv::MvTuple{}, mv::MvTuple{});
    add(*s, "disparity", disparity, QStringLiteral("视差 / px"), "disparity",
        "px");
    add(*s, "score", score, QStringLiteral("匹配代价 · 越小越优"), "score");
    s->primary = "disparity";
  } else if (a == Algorithm::DisparityFilter) {
    require(s->rasters.contains("disparity") && s->rasters.contains("score"),
            "视差过滤需要本次视差及 SGM 代价");
    const auto disparity = s->rasters["disparity"].image;
    const auto cost = s->rasters["score"].image;
    require(disparity.Width() == cost.Width() && disparity.Height() == cost.Height(),
            "视差与匹配代价规格不符");
    auto accepted = regionMask(cost.Threshold(0, value("maxCost", 32)))
                        .Intersection(disparity.GetDomain());
    const auto withinCost = accepted;
    if (value("minArea", 5) > 0)
      accepted = regionMask(accepted.Connection().SelectShape(
          "area", "and", value("minArea", 5), 1000000.));
    auto count = [](const mv::MvRegion& region) {
      mv::MvTuple row, column;
      return region.CountObj() ? region.AreaCenter(&row, &column).D() : 0.;
    };
    const double original = count(regionMask(disparity.GetDomain()));
    const double goodCost = count(withinCost), retained = count(accepted);
    const double totalPixels = double(disparity.Width()) * disparity.Height();
    mv::MvImage valid, reason;
    valid.GenImageConst("byte", disparity.Width(), disparity.Height());
    reason.GenImageConst("byte", disparity.Width(), disparity.Height());
    valid = valid.PaintRegion(accepted, 255., "fill");
    const auto full = reason.GetDomain();
    reason = reason.PaintRegion(full.Difference(disparity.GetDomain()), 1., "fill")
                   .PaintRegion(disparity.GetDomain().Difference(withinCost), 2., "fill")
                   .PaintRegion(withinCost.Difference(accepted), 3., "fill");
    s->rasters["disparity"].image = disparity.ReduceDomain(accepted);
    s->rasters["score"].image = cost.ReduceDomain(accepted);
    add(*s, "disparityValid", valid, QStringLiteral("有效视差"), "validity");
    add(*s, "rejection", reason, QStringLiteral("过滤原因 · 0通过 1输入无效 2代价 3小区域"), "reason");
    s->primary = "disparity";
    for (const auto& metric : QVector<QPair<QString, double>>{
        {QStringLiteral("有效视差"), retained}, {QStringLiteral("有效比例 / %"), retained * 100 / totalPixels},
        {QStringLiteral("输入无效视差"), totalPixels - original},
        {QStringLiteral("代价超限"), original - goodCost},
        {QStringLiteral("小区域剔除"), goodCost - retained}}) {
      ResultObject object;
      object.type = "Value"; object.label = metric.first; object.measure = metric.second;
      object.unit = metric.first.contains('%') ? "%" : "px";
      object.values["value"] = metric.second;
      r.objects.append(object);
    }
    r.measureName = QStringLiteral("有效视差与过滤统计");
  } else if (a == Algorithm::StereoReconstruct) {
    require(s->rasters.contains("disparity"), "输入缺少视差");
    auto j = stereoCalibration(s->calibration);
    mv::MvImage y, z;
    auto x = s->rasters["disparity"].image.DisparityImageToXyz(
        &y, &z, mv::MvCamPar(tuple(j["rectLeft"].toArray())),
        mv::MvCamPar(tuple(j["rectRight"].toArray())),
        mv::MvPose(tuple(j["rectPose"].toArray())));
    mv::MvObjectModel3D cloud;
    cloud.XyzToObjectModel3d(x, y, z);
    s->points.clear();
    require(x.Type() == mv::MvPixelType::Float32 &&
                y.Type() == mv::MvPixelType::Float32 &&
                z.Type() == mv::MvPixelType::Float32,
            "XYZ 类型无效");
    const auto* xp = static_cast<const char*>(x.Data());
    const auto* yp = static_cast<const char*>(y.Data());
    const auto* zp = static_cast<const char*>(z.Data());
    const auto xs = x.Stride(), ys = y.Stride(), zs = z.Stride();
    mv::MvTuple rr, first, last, validR, validFirst, validLast;
    z.GetDomain().GetRegionRuns(&rr, &first, &last);
    for (size_t run = 0; run < rr.Length(); ++run) {
      int row = int(rr.I(run)), begin = int(first.I(run)),
          end = int(last.I(run)), start = -1;
      const auto* xrow = reinterpret_cast<const float*>(xp + row * xs);
      const auto* yrow = reinterpret_cast<const float*>(yp + row * ys);
      const auto* zrow = reinterpret_cast<const float*>(zp + row * zs);
      for (int col = begin; col <= end + 1; ++col) {
        bool valid = col <= end && std::isfinite(xrow[col]) &&
                     std::isfinite(yrow[col]) && std::isfinite(zrow[col]) &&
                     zrow[col] > 0 && zrow[col] <= 1e9 &&
                     std::abs(xrow[col]) <= 1e9 && std::abs(yrow[col]) <= 1e9;
        if (valid) {
          s->points.append({xrow[col] * 1000., yrow[col] * 1000.,
                            zrow[col] * 1000., row, col});
          if (start < 0) start = col;
        } else if (start >= 0) {
          validR.Append(row);
          validFirst.Append(start);
          validLast.Append(col - 1);
          start = -1;
        }
      }
    }
    require(!s->points.isEmpty(),
            "重建没有有效正深度点，请检查左右配对、视差范围和标定");
    mv::MvRegion valid;
    valid.GenRegionRuns(validR, validFirst, validLast);
    add(*s, "x", x.ReduceDomain(valid), QStringLiteral("X / m"), "coordinate",
        "m");
    add(*s, "y", y.ReduceDomain(valid), QStringLiteral("Y / m"), "coordinate",
        "m");
    add(*s, "depth", z.ReduceDomain(valid), QStringLiteral("深度 Z / m"),
        "depth", "m");
    mv::MvImage mask;
    mask.GenImageConst("byte", z.Width(), z.Height());
    mask = mask.PaintRegion(valid, 255., "fill");
    add(*s, "valid", mask, QStringLiteral("有效三维点"), "validity");
    s->primary = "depth";
  } else if (a == Algorithm::CloudSelect) {
    require(s->rasters.contains("depth"), "请连接重建后的点云");
    require(d.activeRange.poseSource == -1 && !d.activeRange.iteratePose &&
                d.activeRange.scope != ScopeMode::Region,
            "首版点云选取支持固定二维 ROI 和深度区间");
    auto domain = s->rasters["depth"].image.GetDomain();
    if (d.activeRange.scope == ScopeMode::Manual)
      domain = domain.Intersection(makeRegion(d.activeRange.roi));
    const bool fromRegion = value("selection", 0) == 1;
    if (fromRegion) {
      require(regionInput && regionInput->state == NodeState::Success &&
                  regionInput->coordinateFrame == s->frame,
              "区域须来自同组校正左图或结构光相机图；不能混用其他坐标系");
      domain = domain.Intersection(regionMask(regionInput->regions));
    }
    QVector<SpatialPoint> selected;
    selected.reserve(s->points.size());
    mv::MvTuple rows, cols;
    for (const auto& roi : d.activeRange.exclusions)
      domain = domain.Difference(makeRegion(roi));
    // Use the actual native pixel region, not per-point QPainterPath queries.
    mv::MvImage membership;
    membership.GenImageConst("byte", d.image.Width(), d.image.Height());
    membership = membership.PaintRegion(domain, 255., "fill");
    const auto* selectedPixels = static_cast<const unsigned char*>(membership.Data());
    const auto stride = membership.Stride();
    const int width = int(membership.Width()), height = int(membership.Height());
    for (const auto& p : s->points) {
      require(p.row >= 0 && p.column >= 0 && p.row < height && p.column < width,
              "点云像素映射超出校正图像");
      const bool keep = selectedPixels[p.row * stride + p.column] != 0;
      if (keep && p.z >= value("minZ", 0) && p.z <= value("maxZ", 100000)) {
        selected.append(p);
        rows.Append(p.row);
        cols.Append(p.column);
      }
    }
    s->points = std::move(selected);
    mv::MvRegion mask;
    mask.GenRegionRuns(rows, cols, cols);
    for (auto key : {"x", "y", "depth"})
      s->rasters[key].image = s->rasters[key].image.ReduceDomain(mask);
    mv::MvImage validity;
    validity.GenImageConst("byte", d.image.Width(), d.image.Height());
    validity = validity.PaintRegion(mask, 255., "fill");
    add(*s, "valid", validity, QStringLiteral("有效三维点"), "validity");
    s->primary = "depth";
  } else if (a == Algorithm::SphereFit || a == Algorithm::PlaneFit || a == Algorithm::CloudMeasure) {
    require(!s->points.isEmpty(), "点云没有有效点，无法测量");
    ResultObject measured;
    measured.type = "Value";
    measured.unit = "mm";
    QVector<double> residuals;
    if (a == Algorithm::PlaneFit) {
      const auto plane = fitPlane(s->points);
      double low = plane.residual(s->points.first()), high = low, squared = 0;
      for (const auto& p : s->points) {
        const double residual = plane.residual(p);
        residuals.append(residual);
        low = std::min(low, residual);
        high = std::max(high, residual);
        squared += residual * residual;
      }
      const double rms = std::sqrt(squared / s->points.size());
      const double maximum = std::max(std::abs(low), std::abs(high));
      const double metrics[]{rms, maximum, high - low};
      const int mode = int(value("mode", 0));
      measured.type = "Plane3D";
      measured.measure = metrics[mode];
      measured.label = QStringList{QStringLiteral("平面残差 RMS"), QStringLiteral("最大绝对残差"), QStringLiteral("残差峰谷差")}[mode];
      measured.values = {{"normalX", plane.nx}, {"normalY", plane.ny}, {"normalZ", plane.nz},
                         {"planeOffset", plane.offset}, {"planeRms", rms}, {"planeMaxAbs", maximum},
                         {"planePeakToValley", high-low}, {"value", measured.measure}};
    } else if (a == Algorithm::SphereFit) {
      const auto fit = fitCloudPrimitive(s->points, "sphere", value("minRadius", 0.01), value("maxRadius", 10000));
      const double x = fit.D(0), y = fit.D(1), z = fit.D(2), radius = fit.D(3);
      double squared = 0, low = INFINITY, high = -INFINITY;
      for (const auto& p : s->points) {
        const double residual = std::hypot(std::hypot(p.x-x, p.y-y), p.z-z) - radius;
        residuals.append(residual);
        low = std::min(low, residual);
        high = std::max(high, residual);
        squared += residual * residual;
      }
      const double rms = std::sqrt(squared / s->points.size());
      const int mode = int(value("mode", 0));
      measured.measure = mode == 0 ? radius : mode == 1 ? 2 * radius : rms;
      measured.label = QStringList{QStringLiteral("球半径"), QStringLiteral("球直径"), QStringLiteral("径向 RMS")}[mode];
      measured.values = {{"value", measured.measure}, {"sphereRadius", radius}, {"sphereDiameter", 2*radius},
                         {"sphereX", x}, {"sphereY", y}, {"sphereZ", z}, {"sphereRms", rms},
                         {"sphereMaxAbs", std::max(std::abs(low), std::abs(high))}, {"spherePeakToValley", high-low}};
    } else if (value("mode", 0) == 3) {
      require(regionInput && regionInput != input && regionInput->spatial && regionInput->spatial->frame == s->frame &&
              regionInput->spatial->group == s->group, "请选择同一采集组内独立的被测点云 A 和基准点云 B");
      CloudPlane plane;
      if (regionInput->parameters.algorithm == Algorithm::PlaneFit) {
        require(regionInput->objects.size() == 1 && regionInput->objects.first().type == "Plane3D", "基准平面无有效拟合结果");
        const auto& fields = regionInput->objects.first().values;
        plane = {fields.value("normalX"), fields.value("normalY"), fields.value("normalZ"), fields.value("planeOffset")};
      } else {
        plane = fitPlane(regionInput->spatial->points);
      }
      for (const auto& p : s->points) {
        const double distance = std::abs(plane.residual(p));
        measured.measure = std::max(measured.measure, distance);
      }
      measured.values = {{"value", measured.measure}};
      measured.label = QStringLiteral("相对基准平面的最大垂距");
    } else if (value("mode", 0) == 2) {
      QVector<double> z;
      z.reserve(s->points.size());
      for (const auto& p : s->points) z.append(p.z);
      auto mid = z.begin() + z.size() / 2;
      std::nth_element(z.begin(), mid, z.end());
      double median = *mid;
      if (z.size() % 2 == 0)
        median = (median + *std::max_element(z.begin(), mid)) / 2;
      measured.measure = median;
      measured.values = {{"value", median}, {"medianZ", median}};
      measured.label = QStringLiteral("相机深度中位数");
    } else if (value("mode", 0) == 0) {
      double lo = s->points[0].z, hi = lo;
      for (const auto& p : s->points) {
        lo = std::min(lo, p.z);
        hi = std::max(hi, p.z);
      }
      measured.measure = hi - lo;
      measured.values = {{"value", hi - lo}, {"minZ", lo}, {"maxZ", hi}};
      measured.label = QStringLiteral("相机深度范围");
    } else {
      int ia = int(value("pointA", 1)) - 1, ib = int(value("pointB", 2)) - 1;
      require(
          ia >= 0 && ib >= 0 && ia < s->points.size() && ib < s->points.size(),
          "三维点编号不存在");
      const auto& p = s->points[ia];
      const auto& q = s->points[ib];
      measured.measure =
          std::hypot(std::hypot(p.x - q.x, p.y - q.y), p.z - q.z);
      measured.values = {{"value", measured.measure}};
      measured.label = QStringLiteral("三维两点距离");
      measured.first = {double(p.column), double(p.row)};
      measured.second = {double(q.column), double(q.row)};
      measured.overlay.moveTo(measured.first);
      measured.overlay.lineTo(measured.second);
    }
    if (!residuals.isEmpty()) {
      // A signed, camera-pixel raster preserves correspondence through existing
      // IPC. Only fitted points belong to its domain; no interpolated samples.
      const auto& reference = s->rasters[s->primary].image;
      mv::MvImage residualImage;
      const int width = int(reference.Width()), height = int(reference.Height());
      QVector<float> pixels(width * height, 0);
      mv::MvTuple rows, columns;
      for (int i = 0; i < s->points.size(); ++i) {
        const auto& point = s->points[i];
        require(point.row >= 0 && point.row < height && point.column >= 0 && point.column < width,
                "拟合点的相机像素映射无效");
        rows.Append(point.row); columns.Append(point.column);
        pixels[point.row * width + point.column] = float(residuals[i]);
      }
      residualImage.GenImage1("real", width, height, pixels.constData());
      mv::MvRegion domain;
      domain.GenRegionRuns(rows, columns, columns);
      add(*s, "fitResidual", residualImage.ReduceDomain(domain),
          a == Algorithm::PlaneFit ? QStringLiteral("平面有符号残差 / mm") : QStringLiteral("球面径向残差 / mm"),
          "residual", "mm");
      s->primary = "fitResidual";
    }
    r.objects = {measured};
    r.measureName = measured.label + " / mm";
  }
  r.spatial = s;
  r.outputImage = s->rasters[s->primary].image;
  r.preview = makePreview(r.outputImage).image;
  if (a == Algorithm::CloudMeasure || a == Algorithm::SphereFit || a == Algorithm::PlaneFit) r.preview = {};
  r.pipeline = operatorDefinition(a).name;
  r.algorithmMs = r.totalMs = timer.nsecsElapsed() / 1e6;
  r.steps.append({r.pipeline, r.algorithmMs});
  return r;
}
QByteArray encodeSpatial(const std::shared_ptr<const SpatialData>& s) {
  if (!s) return {};
  QByteArray b;
  QDataStream out(&b, QIODevice::WriteOnly);
  out.setVersion(QDataStream::Qt_5_15);
  out << quint32(1) << s->group << s->frame << s->primary << s->calibration
      << quint32(s->rasters.size());
  for (auto it = s->rasters.begin(); it != s->rasters.end(); ++it)
    out << it.key() << it->label << it->semantic << it->unit << it->frame
        << encodeImage(it->image);
  out << quint32(s->points.size());
  for (auto p : s->points)
    out << p.x << p.y << p.z << qint32(p.row) << qint32(p.column);
  require(b.size() <= 256 * 1024 * 1024, "三维结果超过传输预算");
  return b;
}
std::shared_ptr<const SpatialData> decodeSpatial(const QByteArray& b) {
  if (b.isEmpty()) return {};
  require(b.size() <= 256 * 1024 * 1024, "三维数据过大");
  QDataStream in(b);
  in.setVersion(QDataStream::Qt_5_15);
  auto s = std::make_shared<SpatialData>();
  quint32 version, n;
  in >> version >> s->group >> s->frame >> s->primary >> s->calibration >> n;
  require(version == 1 && n <= 12, "三维输出格式无效");
  const auto calibration = QJsonDocument::fromJson(s->calibration).object();
  if (calibration["format"] == "mvkit-structured-result" || calibration["format"] == "mvkit-xyz-result") {
    require(calibration["version"] == 1 && calibration["lengthUnit"] == "m",
            "结构光结果配置无效");
  } else if (calibration["format"] == "mvkit-image-results") {
    require(calibration["version"] == 1, "图像结果格式无效");
  } else stereoCalibration(s->calibration);
  for (quint32 i = 0; i < n; ++i) {
    QString key;
    NamedRaster r;
    QByteArray image;
    in >> key >> r.label >> r.semantic >> r.unit >> r.frame >> image;
    r.image = decodeImage(image);
    require(r.image.CountObj() == 1 &&
                r.image.Width() * r.image.Height() <= 1000000,
            "具名图像大小无效");
    require(!key.isEmpty() && !s->rasters.contains(key) &&
                !r.frame.isEmpty() && r.frame.size() <= 4096,
            "三维具名输出无效");
    s->rasters[key] = r;
  }
  in >> n;
  require(n <= 1000000, "点云点数超过预算");
  require(calibration["format"] != "mvkit-image-results" || n == 0, "普通图像结果不能携带未标定点云");
  for (quint32 i = 0; i < n; ++i) {
    SpatialPoint p;
    qint32 row, col;
    in >> p.x >> p.y >> p.z >> row >> col;
    p.row = row;
    p.column = col;
    require(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
                row >= 0 && col >= 0 && s->rasters.contains(s->primary) &&
                row < s->rasters[s->primary].image.Height() &&
                col < s->rasters[s->primary].image.Width() &&
                std::abs(p.x) <= 1e12 && std::abs(p.y) <= 1e12 && p.z > 0 &&
                p.z <= 1e12,
            "点云数据无效");
    s->points.append(p);
  }
  require(in.status() == QDataStream::Ok && in.atEnd() &&
              s->rasters.contains(s->primary),
          "三维输出损坏");
  return s;
}
}  // namespace workbench
