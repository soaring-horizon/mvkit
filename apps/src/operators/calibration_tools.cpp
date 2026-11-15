// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
// Studio composes the existing public single-camera calibration and plane
// projection APIs. No vendor implementation or example assets are embedded.
#include "operators/calibration_tools.hpp"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <cmath>
#include <stdexcept>

#include "operators/operator_registry.hpp"
#include "operators/range_model.hpp"
namespace workbench {
namespace {
void require(bool b, const char* reason) {
  if (!b) throw std::runtime_error(reason);
}
QJsonArray array(const mv::MvTuple& t) {
  QJsonArray a;
  for (std::size_t i = 0; i < t.Length(); ++i) {
    try {
      a.append(t.D(i));
    } catch (...) {
      a.append(QString::fromUtf8(t.S(i).Text()));
    }
  }
  return a;
}
mv::MvTuple tuple(const QJsonArray& a) {
  mv::MvTuple t;
  for (const auto& v : a) {
    if (v.isString())
      t.Append(v.toString().toUtf8().constData());
    else {
      require(v.isDouble() && std::isfinite(v.toDouble()), "标定参数必须有限");
      t.Append(v.toDouble());
    }
  }
  return t;
}
QByteArray read(const QString& path, qint64 limit) {
  QFile f(path);
  require(f.open(QIODevice::ReadOnly) && f.size() > 0 && f.size() <= limit,
          "标定文件缺失或过大");
  return f.readAll();
}
mv::MvTuple initialCamera(const QJsonObject& config, QSize size) {
  if (config.contains("camera")) return tuple(config["camera"].toArray());
  const double focus = config.value("focusMM").toDouble(12),
               pitch = config.value("pitchUM").toDouble(3.45);
  require(std::isfinite(focus) && std::isfinite(pitch) && focus > 0 &&
              focus <= 500 && pitch >= .1 && pitch <= 100,
          "焦距或像元尺寸无效");
  const double cx = config.value("cx").toDouble(-1),
               cy = config.value("cy").toDouble(-1);
  mv::MvTuple camera("area_scan_division");
  camera.Append(focus * .001)
      .Append(0.)
      .Append(pitch * 1e-6)
      .Append(pitch * 1e-6)
      .Append(cx < 0 ? (size.width() - 1) * .5 : cx)
      .Append(cy < 0 ? (size.height() - 1) * .5 : cy)
      .Append(size.width())
      .Append(size.height());
  return camera;
}
void configure(mv::MvCalibData& data, const QJsonObject& config, QSize size) {
  const auto path = config["boardPath"].toString();
  require(!path.isEmpty(), "请选择 .cpd / .descr 标定板描述文件");
  const auto bytes = read(path, 1024 * 1024);
  const auto hash = QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
  require(config["boardSha"].toString().isEmpty() || config["boardSha"] == hash,
          "标定板文件已改变，请重新选择");
  data.CreateCalibData("calibration_object", 1, 1);
  data.SetCalibDataCamParam(0, mv::MvTuple{},
                            mv::MvCamPar(initialCamera(config, size)));
  data.SetCalibDataCalibObject(0, mv::MvTuple(path.toUtf8().constData()));
  data.SetCalibData("camera", 0, "excluded_settings",
                    mv::MvTuple("sx").Append("sy"));
}
mv::MvImage gray(const mv::MvImage& image) {
  require(image.CountObj() == 1, "标定需要单张图像");
  for (std::size_t i = 0; i < image.Channels(); ++i)
    require(image.Type(i) == mv::MvPixelType::UInt8,
            "标定板检测要求原始 byte 图像");
  return image.Channels() == 3
             ? image.FullDomain().Rgb1ToGray().ReduceDomain(image.GetDomain())
             : image;
}
QJsonObject observation(const mv::MvCalibData& data, int index,
                        const QString& path) {
  mv::MvTuple rows, cols, ids, pose;
  data.GetCalibDataObservPoints(0, 0, index, &rows, &cols, &ids, &pose);
  require(rows.Length() >= 6 && rows.Length() <= 10000,
          "有效标定点数量不足或过多");
  return {{"path", path},        {"index", index},
          {"rows", array(rows)}, {"columns", array(cols)},
          {"ids", array(ids)},   {"initialPose", array(pose)},
          {"status", "detected"}};
}
void rawCoordinates(const Document& d) {
  require(d.coordinateFrame == d.imagePath,
          "相机标定仅接受原始相机像素坐标；仿射/校正图需另行标定或坐标映射");
}
void imageSize(const QJsonObject& model, const mv::MvImage& image) {
  require(model["width"].toInt() == image.Width() &&
              model["height"].toInt() == image.Height(),
          "标定图像规格不匹配，不自动缩放相机参数");
}
}  // namespace
QJsonObject readCalibrationArtifact(const QByteArray& bytes) {
  require(!bytes.isEmpty() && bytes.size() <= 8 * 1024 * 1024,
          "请加载有效标定文件");
  QJsonParseError error;
  const auto m = QJsonDocument::fromJson(bytes, &error).object();
  require(error.error == QJsonParseError::NoError &&
              m["format"] == "mvkit-studio-calibration" && m["version"] == 1 &&
              m["valid"].toBool(),
          "标定文件格式无效或未求解成功");
  const auto camera = m["camera"].toArray();
  require(camera.size() == 9 && camera[0] == "area_scan_division",
          "当前界面支持 area_scan_division 相机");
  const auto native = tuple(camera);
  mv::MvCamPar validation(native);
  require(m["width"].toInt() > 0 && m["height"].toInt() > 0 &&
              m["width"].toInt() <= 16384 && m["height"].toInt() <= 16384 &&
              camera[7].toDouble() == m["width"].toInt() &&
              camera[8].toDouble() == m["height"].toInt(),
          "标定尺寸无效");
  require(camera[1].toDouble() > 0 && camera[3].toDouble() > 0 &&
              camera[4].toDouble() > 0,
          "标定相机参数无效");
  const auto pose = m["worldPose"].toArray();
  if (!pose.isEmpty()) {
    require(pose.size() == 7 && pose[6].toInt() == 0, "标定平面位姿无效");
    tuple(pose);
  }
  return m;
}
QByteArray loadCalibrationFile(const QString& path) {
  auto bytes = read(path, 8 * 1024 * 1024);
  readCalibrationArtifact(bytes);
  return bytes;
}
void saveCalibrationFile(const QByteArray& bytes, const QString& path) {
  readCalibrationArtifact(bytes);
  QSaveFile f(path);
  require(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() &&
              f.commit(),
          "无法保存标定文件");
}
Result executeCalibrationJob(const Document& d) {
  Result r;
  QElapsedTimer timer;
  timer.start();
  QJsonObject model{
      {"format", "mvkit-studio-calibration"}, {"version", 1}, {"valid", false}};
  QJsonArray views;
  try {
    const auto config = d.calibrationJob;
    const auto files = config["images"].toArray();
    require(files.size() >= 3 && files.size() <= 64,
            "相机标定需要 3–64 张不同姿态的图像");
    mv::MvCalibData data;
    QSize size;
    int valid = 0;
    for (int i = 0; i < files.size(); ++i) {
      const auto file = files[i].toObject();
      QJsonObject view{{"path", file["path"]}, {"index", i}};
      try {
        auto image = loadImage(file["path"].toString());
        require(file["pixelKey"].toString().isEmpty() ||
                    image.pixelKey == file["pixelKey"].toString(),
                "标定输入像素已改变，请重新采集所选图像");
        if (!size.isValid()) {
          size = image.preview.size();
          configure(data, config, size);
        }
        require(image.preview.size() == size, "标定集图像尺寸不同");
        data.FindCalibObject(gray(image.image), 0, 0, i, mv::MvTuple{},
                             mv::MvTuple{});
        view = observation(data, i, image.imagePath);
        view["pixelKey"] = image.pixelKey;
        ++valid;
      } catch (const std::exception& e) {
        view["status"] = "error";
        view["error"] = QString::fromUtf8(e.what());
      }
      views.append(view);
    }
    require(valid >= 3,
            "成功检测少于 3 张，无法标定；请检查描述文件、初始参数和图像");
    const double rms = data.CalibrateCameras();
    require(std::isfinite(rms), "标定求解未返回有限误差");
    for (int i = 0; i < views.size(); ++i) {
      auto view = views[i].toObject();
      if (view["status"] != "detected") continue;
      auto key = mv::MvTuple(0).Append(i);
      view["pose"] = array(data.GetCalibData("calib_obj_pose", key, "pose"));
      const auto errors = data.GetCalibData("calib_obj_pose", key, "errors");
      double sum = 0;
      for (std::size_t j = 0; j < errors.Length(); ++j)
        sum += errors.D(j) * errors.D(j);
      view["rms"] = std::sqrt(sum / std::max(std::size_t(1), errors.Length()));
      view["status"] = "success";
      views[i] = view;
    }
    model["camera"] = array(data.GetCalibData("camera", 0, "params"));
    model["rms"] = rms;
    model["width"] = size.width();
    model["height"] = size.height();
    model["valid"] = true;
    for (const auto& v : views)
      if (v.toObject()["status"] == "success") {
        model["worldPose"] = v.toObject()["pose"];
        model["planeView"] = v.toObject()["index"];
        break;
      }
  } catch (const std::exception& e) {
    r.error = QString::fromUtf8(e.what());
  }
  model["views"] = views;
  r.calibrationResult = QJsonDocument(model).toJson(QJsonDocument::Compact);
  r.pipeline = QStringLiteral("相机标定");
  r.algorithmMs = r.totalMs = timer.nsecsElapsed() / 1e6;
  return r;
}
Result detectCalibrationBoard(const Document& d) {
  rawCoordinates(d);
  QElapsedTimer timer;
  timer.start();
  auto config = d.parameters.calibrationConfig;
  for (auto it = d.parameters.options.begin(); it != d.parameters.options.end();
       ++it)
    config[it.key()] = it.value();
  mv::MvCalibData data;
  configure(data, config, d.preview.size());
  const auto input = d.roi.kind == RoiKind::None
                         ? d.image
                         : d.image.ReduceDomain(makeRegion(d.roi));
  data.FindCalibObject(gray(input), 0, 0, 0, mv::MvTuple{}, mv::MvTuple{});
  const auto view = observation(data, 0, d.imagePath);
  const auto rows = view["rows"].toArray(), cols = view["columns"].toArray(),
             ids = view["ids"].toArray();
  // The current public detector identifies the board using the image raster.
  // Restrict returned mark centers explicitly: a reduced domain alone is not
  // sufficient for this SDK subset. Keep full-board identification separate
  // from the visible/consumable point set.
  const auto domain = input.GetDomain();
  Result r;
  r.outputImage = d.image;
  for (int i = 0; i < rows.size(); ++i) {
    if (!domain.TestRegionPoint(std::int64_t(std::llround(rows[i].toDouble())),
                                std::int64_t(std::llround(cols[i].toDouble()))))
      continue;
    ResultObject point;
    point.type = "Point";
    point.center = {cols[i].toDouble(), rows[i].toDouble()};
    point.measure = ids[i].toDouble();
    point.values = {{"markIndex", point.measure},
                    {"centerX", point.center.x()},
                    {"centerY", point.center.y()}};
    // Dense calibration points are labeled in the result table.
    point.label.clear();
    point.overlay.moveTo(point.center + QPointF(-3, 0));
    point.overlay.lineTo(point.center + QPointF(3, 0));
    point.overlay.moveTo(point.center + QPointF(0, -3));
    point.overlay.lineTo(point.center + QPointF(0, 3));
    r.objects.append(point);
  }
  require(r.objects.size() >= 6, "处理范围内有效标定点少于 6 个");
  r.pipeline = QStringLiteral("标定板检测");
  r.measureName = QStringLiteral("标记索引");
  r.algorithmMs = r.totalMs = timer.nsecsElapsed() / 1e6;
  return r;
}
Result rectifyCalibrationImage(const Document& d) {
  rawCoordinates(d);
  const auto m = readCalibrationArtifact(d.parameters.calibrationArtifact);
  imageSize(m, d.image);
  QElapsedTimer timer;
  timer.start();
  Result r;
  const auto cam = tuple(m["camera"].toArray());
  const auto& o = d.parameters.options;
  auto image = d.image;
  if (d.roi.kind != RoiKind::None)
    image = image.ReduceDomain(makeRegion(d.roi));
  if (o.value("mode").toInt() == 0) {
    mv::MvTuple ideal;
    mv::ChangeRadialDistortionCamPar("fixed", cam, 0., &ideal);
    mv::ChangeRadialDistortionImage(image, image.GetDomain(), &r.outputImage,
                                    cam, ideal);
  } else {
    require(m["worldPose"].toArray().size() == 7, "标定文件未配置世界平面");
    mv::ImageToWorldPlane(
        image, &r.outputImage, cam, tuple(m["worldPose"].toArray()),
        o.value("outWidth").toInt(640), o.value("outHeight").toInt(640),
        o.value("mmPerPixel").toDouble(.1) * .001,
        o.value("interpolation").toInt() == 0 ? "nearest_neighbor"
                                              : "bilinear");
  }
  r.algorithmMs = timer.nsecsElapsed() / 1e6;
  r.preview = makePreview(r.outputImage).image;
  r.pipeline = QStringLiteral("图像校正");
  r.measureName = QStringLiteral("结果图像");
  r.totalMs = timer.nsecsElapsed() / 1e6;
  return r;
}
Result executePlaneMeasure(const Document& d, const NodeOutput* a,
                           const NodeOutput* b) {
  rawCoordinates(d);
  const auto m = readCalibrationArtifact(d.parameters.calibrationArtifact);
  imageSize(m, d.image);
  require(m["worldPose"].toArray().size() == 7, "标定文件未配置世界平面");
  const int ia = d.parameters.options.value("objectA").toInt(1) - 1,
            ib = d.parameters.options.value("objectB").toInt(1) - 1;
  require(a && b && a->state == NodeState::Success &&
              b->state == NodeState::Success && ia >= 0 &&
              ia < a->objects.size() && ib >= 0 && ib < b->objects.size(),
          "请选择有效的平面测量 A/B 对象");
  QElapsedTimer timer;
  timer.start();
  const auto p = a->objects[ia].center, q = b->objects[ib].center;
  mv::MvTuple x, y;
  mv::ImagePointsToWorldPlane(tuple(m["camera"].toArray()),
                              tuple(m["worldPose"].toArray()),
                              mv::MvTuple(p.y()).Append(q.y()),
                              mv::MvTuple(p.x()).Append(q.x()), "mm", &x, &y);
  Result r;
  ResultObject value;
  value.type = "Value";
  value.unit = "mm";
  value.measure = std::hypot(x.D(1) - x.D(0), y.D(1) - y.D(0));
  value.center = (p + q) / 2;
  value.overlay.moveTo(p);
  value.overlay.lineTo(q);
  value.label = QStringLiteral("平面中心距");
  value.values = {{"value", value.measure},
                  {"worldX1", x.D(0)},
                  {"worldY1", y.D(0)},
                  {"worldX2", x.D(1)},
                  {"worldY2", y.D(1)}};
  r.objects = {value};
  r.outputImage = d.image;
  r.pipeline = value.label;
  r.measureName = QStringLiteral("中心距 / mm");
  r.algorithmMs = r.totalMs = timer.nsecsElapsed() / 1e6;
  return r;
}
}  // namespace workbench
