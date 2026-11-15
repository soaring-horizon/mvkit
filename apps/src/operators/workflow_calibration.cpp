// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
// Studio orchestration of HALCON-corresponding public MVKit operators only.
#include "model/spatial_data.hpp"
#include "operators/calibration_tools.hpp"
#include "operators/operator_registry.hpp"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace workbench {
namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
QJsonArray array(const mv::MvTuple &t) {
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
mv::MvTuple camera(const Parameters &p, QSize size, bool second) {
  const auto o = p.options;
  const char *focus = second ? "secondFocusMM" : "focusMM";
  const char *pitch = second ? "secondPitchUM" : "pitchUM";
  const double f = o.value(focus).toDouble(12) * .001,
               s = o.value(pitch).toDouble(3.45) * 1e-6;
  require(f > 0 && std::isfinite(f) && s > 0 && std::isfinite(s),
          "焦距及像元尺寸必须为正有限数值");
  mv::MvTuple t("area_scan_division");
  return t.Append(f)
      .Append(0.)
      .Append(s)
      .Append(s)
      .Append((size.width() - 1) * .5)
      .Append((size.height() - 1) * .5)
      .Append(size.width())
      .Append(size.height());
}
mv::MvImage gray(const mv::MvImage &i) {
  require(i.CountObj() == 1 && (i.Channels() == 1 || i.Channels() == 3),
          "标定输入须为灰度或 RGB 图像");
  for (std::size_t c = 0; c < i.Channels(); ++c)
    require(i.Type(c) == mv::MvPixelType::UInt8, "标定输入须为 byte 图像");
  return i.Channels() == 3 ? i.Rgb1ToGray() : i;
}
SourceData sample(const QJsonObject &j) {
  QStringList paths, views;
  for (const auto &v : j["files"].toArray())
    paths << v.toString();
  for (const auto &v : j["views"].toArray())
    views << v.toString();
  const auto d = views.isEmpty() ? loadSourceData(paths, j["sequence"].toBool())
                                 : loadMultiViewData(paths, views);
  require(QJsonArray::fromStringList(d.keys) == j["keys"].toArray(),
          "标定图像已改变，请重新加载样本");
  return d;
}
mv::MvStructuredLightModel projectionModel(const Parameters &p) {
  mv::MvStructuredLightModel m;
  m.CreateStructuredLightModel("3d_reconstruction");
  m.SetStructuredLightModelParam(
      "pattern_width", qint64(p.options.value("projectorWidth").toInt(1280)));
  m.SetStructuredLightModelParam(
      "pattern_height", qint64(p.options.value("projectorHeight").toInt(720)));
  m.SetStructuredLightModelParam(
      "min_stripe_width", qint64(p.options.value("stripeWidth").toInt(16)));
  m.SetStructuredLightModelParam("pattern_type", "gray_code_and_phase_shift");
  m.SetStructuredLightModelParam("pattern_orientation", "both");
  m.SetStructuredLightModelParam("normalization", "global");
  m.SetStructuredLightModelParam(
      "min_gray_difference", qint64(p.options.value("minContrast").toInt(10)));
  const auto patterns = m.GenStructuredLightPattern();
  require(patterns.CountObj() <= 64,
          "标定图案超过 64 帧，请检查投影尺寸与条纹宽度");
  return m;
}
double bilinear(const mv::MvImage &image, double row, double col) {
  const int r = int(std::floor(row)), c = int(std::floor(col));
  mv::MvTuple type, width, height;
  const auto address = image.GetImagePointer1(&type, &width, &height);
  require(QString::fromUtf8(type.S().Text()) == "real",
          "投影坐标图必须为 real 类型");
  const int w = int(width.I()), h = int(height.I());
  require(r >= 0 && c >= 0 && r + 1 < h && c + 1 < w, "投影坐标采样越界");
  const auto *pixels =
      reinterpret_cast<const float *>(std::uintptr_t(address.I()));
  auto value = [&](int y, int x) { return pixels[y * w + x]; };
  const double y = row - r, x = col - c;
  return (1 - y) * ((1 - x) * value(r, c) + x * value(r, c + 1)) +
         y * ((1 - x) * value(r + 1, c) + x * value(r + 1, c + 1));
}
// Hash the encoded files without decoding every exposure on each cache hit.
QByteArray exposureDigest(const QJsonArray& samples) {
  QCryptographicHash ordered(QCryptographicHash::Sha256);
  for (const auto& sample : samples) {
    for (const auto& path : sample.toObject()["files"].toArray()) {
      QFile file(path.toString());
      QCryptographicHash contents(QCryptographicHash::Sha256);
      if (!file.open(QIODevice::ReadOnly) || !contents.addData(&file)) return {};
      ordered.addData(contents.result());
    }
  }
  return ordered.result().toHex();
}
// The batch solution has no display image. Rebuild overlays from the observation
// belonging to the current input, including the camera role in a stereo pair.
Result calibrationView(const QJsonObject& solution, const QJsonArray& samples,
                       const SourceData& source, Algorithm tool, const QString& captureId) {
  Result out;
  const bool stereo = tool == Algorithm::StereoCalibration;
  const bool mono = tool == Algorithm::MonoCalibration;
  const int frame = stereo ? source.views.indexOf("left") : 0;
  require(frame >= 0 && frame < source.frames.size() && !source.frames.isEmpty(),
          "当前标定预览缺少原始图像");
  out.calibrationResult = QByteArray::fromBase64(solution["artifact"].toString().toLatin1());
  ResultObject score;
  score.type = "Value";
  score.label = QStringLiteral("标定重投影 RMS");
  score.measure = solution["rms"].toDouble();
  score.unit = "px";
  score.values = {{"value", score.measure}, {"rms", score.measure}, {"views", double(samples.size())}};
  out.objects << score;
  if (!mono) {
    ResultObject baseline;
    baseline.type = "Value";
    baseline.label = stereo ? QStringLiteral("双目基线") : QStringLiteral("相机—投影仪基线");
    baseline.unit = "mm";
    baseline.measure = solution["baselineMm"].toDouble();
    baseline.values["value"] = baseline.measure;
    out.objects << baseline;
  }
  const auto views = solution["views"].toArray();
  const auto observations = solution["observations"].toArray();
  const auto currentKeys = QJsonArray::fromStringList(source.keys);
  int displayView = mono ? 0 : -1;
  if (!mono) {
    const auto roles = QJsonArray::fromStringList(source.views);
    const auto files = QJsonArray::fromStringList(source.paths);
    for (int v = 0; v < samples.size(); ++v) {
      const auto manifest = samples[v].toObject();
      if (manifest["keys"].toArray() != currentKeys || manifest["views"].toArray() != roles) continue;
      if (manifest["sampleId"].toString() == captureId) { displayView = v; break; }
      if (displayView < 0 || manifest["files"].toArray() == files) displayView = v;
    }
  }
  QVector<ResultObject> marks;
  for (int v = 0; v < views.size(); ++v) {
    const auto view = views[v].toObject();
    const auto errors = view["deviceRms"].toArray();
    const auto counts = view["pointCounts"].toArray();
    for (int device = 0; device < errors.size(); ++device) {
      ResultObject quality;
      quality.type = "Value";
      const QString name = stereo ? (device ? QStringLiteral("右相机") : QStringLiteral("左相机"))
                                  : (device ? QStringLiteral("投影仪") : QStringLiteral("相机"));
      quality.label = QStringLiteral("姿态 %1 · %2").arg(v + 1, 2, 10, QChar('0')).arg(name);
      quality.unit = "px";
      quality.measure = errors[device].toDouble();
      quality.values = {{"value", quality.measure}, {"rms", quality.measure}, {"points", counts[device].toDouble()}};
      out.objects << quality;
      if (v != displayView || (device && !stereo)) continue;
      for (const auto& item : observations[v].toArray()[device].toArray()) {
        const auto values = item.toArray();
        ResultObject point;
        point.type = "Point";
        point.center = {values[1].toDouble(), values[2].toDouble()};
        point.measure = values[0].toDouble();
        point.values = {{"markIndex", point.measure}, {"residual", values[3].toDouble()},
                        {"cameraIndex", double(device)}, {"viewIndex", double(v)}};
        point.overlay.moveTo(point.center + QPointF(-3, 0));
        point.overlay.lineTo(point.center + QPointF(3, 0));
        point.overlay.moveTo(point.center + QPointF(0, -3));
        point.overlay.lineTo(point.center + QPointF(0, 3));
        marks << point;
      }
    }
  }
  out.objects += marks;
  out.outputImage = source.frames[frame];
  out.preview = makePreview(out.outputImage).image;
  out.measureName = QStringLiteral("标定结果");
  out.pipeline = operatorDefinition(tool).name;
  return out;
}
} // namespace

QByteArray workflowCalibrationModel(const Document &d, int target,
                                    const QVector<NodeOutput> &outputs) {
  if (!calibrationConsumer(d.workflow[target].algorithm))
    return {};
  const int found = workflowNodeSettings(d, target).calibrationSource;
  if (found == -1)
    return {};
  if (found < 0) {
    int count = 0;
    for (int source : workflowAncestors(workflowConnections(d), target))
      if (source >= 0 && calibrationTool(d.workflow[source].algorithm))
        ++count;
    require(count == 0, "请选择一个明确的标定模型输入");
    return {};
  }
  require(found < d.workflow.size() &&
              workflowAncestors(workflowConnections(d), target).contains(found),
          "标定模型输入已断开，请重新选择");
  const auto producer = d.workflow[found].algorithm,
             consumer = d.workflow[target].algorithm;
  require((producer == Algorithm::MonoCalibration &&
           (consumer == Algorithm::ImageRectify ||
            consumer == Algorithm::PlaneMeasure)) ||
              (producer == Algorithm::StereoCalibration &&
               consumer == Algorithm::StereoRectify) ||
              (producer == Algorithm::StructuredCalibration &&
               consumer == Algorithm::StructuredDecode),
          "标定模型类型与处理节点不匹配");
  require(found < outputs.size() &&
              outputs[found].state == NodeState::Success &&
              !outputs[found].calibrationResult.isEmpty(),
          "标定模型未成功生成");
  return outputs[found].calibrationResult;
}

Result executeWorkflowCalibration(const Document &d, const SourceData &source,
                                  const QString &binding) {
  Result out;
  QElapsedTimer timer;
  timer.start();
  const bool profile = qEnvironmentVariableIsSet("MVKIT_CALIBRATION_PROFILE");
  double lastTick = 0, loadMs = 0, detectionMs = 0, decodeMs = 0,
         projectorMs = 0;
  auto tick = [&] {
    const double now = timer.nsecsElapsed() / 1e6;
    const double delta = now - lastTick;
    lastTick = now;
    return delta;
  };
  const auto &p = d.parameters;
  p.validate();
  const bool stereo = p.algorithm == Algorithm::StereoCalibration;
  const bool structured = p.algorithm == Algorithm::StructuredCalibration;
  const int devices = stereo || structured ? 2 : 1;
  QJsonArray samples;
  if (p.algorithm == Algorithm::MonoCalibration) {
    require(source.sequence && source.views.isEmpty(),
            "单目标定须连接序列输入；一条序列包含同一相机的不同板姿态");
    require(source.frames.size() == source.paths.size() &&
                source.keys.size() == source.paths.size(),
            "标定序列不完整");
    for (int i = 0; i < source.frames.size(); ++i)
      samples.append(
          QJsonObject{{"sampleId", QString("sequence-pose-%1").arg(i)},
                      {"sequence", false},
                      {"views", QJsonArray{}},
                      {"files", QJsonArray{source.paths[i]}},
                      {"keys", QJsonArray{source.keys[i]}}});
  } else
    samples = d.calibrationInputs[binding].toArray();
  require(samples.size() >= 3 && samples.size() <= 64,
          "标定需要 3–64 "
          "个不同姿态；单目使用当前序列的各帧，双目/结构光使用已选完整样本");
  QSet<QString> identities;
  for (const auto &item : samples) {
    const auto id = item.toObject()["sampleId"].toString();
    require(!id.isEmpty() && id.size() <= 128 && !identities.contains(id),
            "标定样本身份缺失或重复");
    identities.insert(id);
  }
  QTemporaryDir temp;
  require(temp.isValid(), "无法创建标定临时目录");
  auto boardPath = p.calibrationConfig["boardPath"].toString();
  QByteArray board = QByteArray::fromBase64(
      p.calibrationConfig["boardData"].toString().toLatin1());
  if (board.isEmpty()) {
    QFile f(boardPath);
    require(f.open(QIODevice::ReadOnly) && f.size() <= 1024 * 1024,
            "请选择有效标定板描述文件");
    board = f.readAll();
  }
  const auto hash = QString::fromLatin1(
      QCryptographicHash::hash(board, QCryptographicHash::Sha256).toHex());
  require(!board.isEmpty() &&
              (p.calibrationConfig["boardSha"].toString().isEmpty() ||
               hash == p.calibrationConfig["boardSha"].toString()),
          "标定板描述校验失败");
  QString cachePath;
  const auto digest = devices == 2 && !d.batchCalibrationDirectory.isEmpty()
      ? exposureDigest(samples) : QByteArray();
  if (!digest.isEmpty()) {
    const QJsonObject identity{{"algorithm", int(p.algorithm)}, {"samples", samples},
                               {"board", hash}, {"boardFormat", QFileInfo(boardPath).suffix()},
                               {"options", p.options}, {"encodedInputs", QString::fromLatin1(digest)}};
    const auto key = QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact),
                                              QCryptographicHash::Sha256).toHex();
    cachePath = QDir(d.batchCalibrationDirectory).filePath(QString::fromLatin1(key) + ".json");
    QFile cache(cachePath);
    if (cache.open(QIODevice::ReadOnly)) {
      const auto solution = QJsonDocument::fromJson(cache.readAll()).object();
      if (!solution["artifact"].toString().isEmpty() && solution["views"].toArray().size() == samples.size()
          && solution["observations"].toArray().size() == samples.size()) {
        out = calibrationView(solution, samples, source, p.algorithm, d.captureId);
        out.algorithmMs = out.totalMs = timer.nsecsElapsed() / 1e6;
        if (profile) out.objects[0].values["profile_batch_reuse"] = 1;
        return out;
      }
    }
  }
  const auto frozen = temp.path() + "/board." + QFileInfo(boardPath).suffix();
  {
    QFile f(frozen);
    require(f.open(QIODevice::WriteOnly) && f.write(board) == board.size(),
            "冻结标定板失败");
  }
  mv::MvCalibData data;
  data.CreateCalibData("calibration_object", devices, 1);
  data.SetCalibDataCalibObject(0, frozen.toUtf8().constData());
  std::array<QSize, 2> sizes;
  QJsonArray views;
  mv::MvStructuredLightModel projector;
  if (structured)
    projector = projectionModel(p);
  const double setupMs = tick();
  for (int view = 0; view < samples.size(); ++view) {
    const auto manifest = samples[view].toObject();
    const auto cell = sample(manifest);
    loadMs += tick();
    require(!cell.frames.isEmpty(), "标定样本为空");
    require(structured ? cell.sequence : !cell.sequence, "标定样本类型不匹配");
    std::array<mv::MvImage, 2> images;
    if (stereo) {
      const int left = cell.views.indexOf("left"),
                right = cell.views.indexOf("right");
      require(left >= 0 && right >= 0 && left != right,
              "双目标定须使用包含 left/right 的完整多视样本");
      images = {cell.frames[left], cell.frames[right]};
    } else {
      require(structured || cell.views.isEmpty(), "单目标定只接受单图输入");
      images[0] = cell.frames.first();
    }
    for (int device = 0; device < (stereo ? 2 : 1); ++device) {
      images[device] = gray(images[device]);
      const QSize size(int(images[device].Width()),
                       int(images[device].Height()));
      if (view == 0) {
        sizes[device] = size;
        data.SetCalibDataCamParam(device, mv::MvTuple{},
                                  mv::MvCamPar(camera(p, size, device == 1)));
        data.SetCalibData("camera", device, "excluded_settings",
                          mv::MvTuple("sx").Append("sy"));
      }
      require(sizes[device] == size, "同一设备的标定图像规格必须一致");
      try {
        data.FindCalibObject(images[device], device, 0, view, mv::MvTuple{},
                             mv::MvTuple{});
      } catch (const std::exception &e) {
        throw std::runtime_error(QString("姿态 %1 · 设备 %2：%3")
                                     .arg(view + 1)
                                     .arg(device)
                                     .arg(QString::fromUtf8(e.what()))
                                     .toUtf8()
                                     .constData());
      }
    }
    detectionMs += tick();
    if (structured) {
      if (view == 0) {
        sizes[1] = QSize(p.options.value("projectorWidth").toInt(1280),
                         p.options.value("projectorHeight").toInt(720));
        data.SetCalibDataCamParam(1, mv::MvTuple{},
                                  mv::MvCamPar(camera(p, sizes[1], true)));
        data.SetCalibData("camera", 1, "excluded_settings",
                          mv::MvTuple("sx").Append("sy"));
      }
      mv::MvImage frames;
      for (const auto &image : cell.frames)
        frames = mv::MvImage(frames.ConcatObj(gray(image)));
      projector.DecodeStructuredLightPattern(frames);
      const auto correspondence =
          projector.GetStructuredLightObject("correspondence_image");
      require(correspondence.CountObj() == 2,
              "结构光标定需要水平和垂直两个方向的完整曝光");
      const mv::MvImage col(correspondence.SelectObj(1)),
          row(correspondence.SelectObj(2));
      const auto domain = col.GetDomain().Intersection(row.GetDomain());
      decodeMs += tick();
      mv::MvTuple rr, cc, ids, approx, pr, pc, indices;
      data.GetCalibDataObservPoints(0, 0, view, &rr, &cc, &ids, &approx);
      for (std::size_t i = 0; i < rr.Length(); ++i) {
        const double r = rr.D(i), c = cc.D(i);
        const int ir = int(std::floor(r)), ic = int(std::floor(c));
        if (ir < 0 || ic < 0 || ir + 1 >= int(row.Height()) ||
            ic + 1 >= int(row.Width()))
          continue;
        if (!domain.TestRegionPoint(ir, ic) ||
            !domain.TestRegionPoint(ir + 1, ic) ||
            !domain.TestRegionPoint(ir, ic + 1) ||
            !domain.TestRegionPoint(ir + 1, ic + 1))
          continue;
        const double y = bilinear(row, r, c), x = bilinear(col, r, c);
        if (!std::isfinite(y) || !std::isfinite(x) || y < 0 || x < 0 ||
            y >= sizes[1].height() || x >= sizes[1].width())
          continue;
        pr.Append(y);
        pc.Append(x);
        indices.Append(ids.I(i));
      }
      require(pr.Length() >= 4,
              "有效投影仪标定点不足，请检查曝光顺序、对比度及遮挡");
      const auto wx = data.GetCalibData("calib_obj", 0, "x"),
                 wy = data.GetCalibData("calib_obj", 0, "y"),
                 wz = data.GetCalibData("calib_obj", 0, "z");
      mv::MvTuple x, y, z;
      for (std::size_t i = 0; i < indices.Length(); ++i) {
        const auto k = std::size_t(indices.I(i));
        x.Append(wx.D(k));
        y.Append(wy.D(k));
        z.Append(wz.D(k));
      }
      mv::MvTuple pose, quality;
      mv::VectorToPose(x, y, z, pr, pc, mv::MvCamPar(camera(p, sizes[1], true)),
                       "iterative", "error", &pose, &quality);
      data.SetCalibDataObservPoints(1, 0, view, pr, pc, indices, pose);
      projectorMs += tick();
    }
    views.append(QJsonObject{{"index", view},
                             {"sampleId", manifest["sampleId"]},
                             {"status", "success"}});
  }
  const double rms = data.CalibrateCameras();
  const double solveMs = tick();
  require(std::isfinite(rms), "标定误差无效");
  const mv::MvCameraSetupModel setup(
      data.GetCalibData("model", "general", "camera_setup_model").H());
  QJsonArray observations;
  const auto worldX = data.GetCalibData("calib_obj", 0, "x"),
             worldY = data.GetCalibData("calib_obj", 0, "y"),
             worldZ = data.GetCalibData("calib_obj", 0, "z");
  for (int view = 0; view < samples.size(); ++view) {
    const auto boardPose = data.GetCalibData(
        "calib_obj_pose", mv::MvTuple(0).Append(view), "pose");
    const auto boardTransform = mv::MvPose(boardPose).PoseToHomMat3d();
    auto summary = views[view].toObject();
    summary["pose"] = array(boardPose);
    QJsonArray errors, counts, deviceObservations;
    for (int device = 0; device < devices; ++device) {
      mv::MvTuple rr, cc, ids, initial, x, y, z, projectedR, projectedC;
      data.GetCalibDataObservPoints(device, 0, view, &rr, &cc, &ids, &initial);
      for (std::size_t i = 0; i < ids.Length(); ++i) {
        const auto k = std::size_t(ids.I(i));
        x.Append(worldX.D(k));
        y.Append(worldY.D(k));
        z.Append(worldZ.D(k));
      }
      const auto transform =
          mv::MvPose(data.GetCalibData("camera", device, "pose"))
              .PoseToHomMat3d()
              .HomMat3dInvert()
              .HomMat3dCompose(boardTransform);
      mv::MvTuple cy, cz;
      const auto cx = transform.AffineTransPoint3d(x, y, z, &cy, &cz);
      mv::MvCamPar(data.GetCalibData("camera", device, "params"))
          .Project3dPoint(cx, cy, cz, &projectedR, &projectedC);
      QJsonArray observed;
      double squared = 0;
      for (std::size_t i = 0; i < ids.Length(); ++i) {
        const double dr = rr.D(i) - projectedR.D(i),
                     dc = cc.D(i) - projectedC.D(i);
        squared += dr * dr + dc * dc;
        if (device == 0 || stereo)
          observed.append(QJsonArray{ids.D(i), cc.D(i), rr.D(i), std::hypot(dr, dc)});
      }
      deviceObservations.append(observed);
      const double error = std::sqrt(squared / double(ids.Length()));
      errors.append(error);
      counts.append(double(ids.Length()));
    }
    observations.append(deviceObservations);
    summary["rms"] = errors[0];
    summary["deviceRms"] = errors;
    summary["pointCounts"] = counts;
    views[view] = summary;
  }
  QJsonObject model;
  if (stereo) {
    model = {{"format", "mvkit-stereo-calibration"},
             {"version", 1},
             {"lengthUnit", "m"},
             {"leftCamera", array(data.GetCalibData("camera", 0, "params"))},
             {"rightCamera", array(data.GetCalibData("camera", 1, "params"))},
             {"rightInLeft", array(data.GetCalibData("camera", 1, "pose"))},
             {"rms", rms},
             {"views", views}};
    out.calibrationResult = QJsonDocument(model).toJson(QJsonDocument::Compact);
    stereoCalibration(out.calibrationResult);
  } else if (structured) {
    projector.SetStructuredLightModelParam("camera_setup_model",
                                           mv::MvTuple(setup));
    const auto path = temp.path() + "/calibrated.mvslm";
    projector.WriteStructuredLightModel(path.toUtf8().constData());
    QFile f(path);
    require(f.open(QIODevice::ReadOnly), "结构光模型写入失败");
    out.calibrationResult = f.readAll();
    structuredModelInfo(out.calibrationResult);
  } else {
    const int plane = p.options.value("planeView").toInt(1) - 1;
    require(plane >= 0 && plane < samples.size(),
            "世界平面姿态编号超出标定样本范围");
    model = {{"format", "mvkit-studio-calibration"},
             {"version", 1},
             {"valid", true},
             {"camera", array(data.GetCalibData("camera", 0, "params"))},
             {"worldPose",
              array(data.GetCalibData("calib_obj_pose",
                                      mv::MvTuple(0).Append(plane), "pose"))},
             {"planeView", plane},
             {"width", sizes[0].width()},
             {"height", sizes[0].height()},
             {"rms", rms},
             {"views", views}};
    out.calibrationResult = QJsonDocument(model).toJson(QJsonDocument::Compact);
    readCalibrationArtifact(out.calibrationResult);
  }
  QJsonObject solution{{"artifact", QString::fromLatin1(out.calibrationResult.toBase64())},
                       {"views", views}, {"observations", observations}, {"rms", rms}};
  if (devices == 2) {
    const auto pose = data.GetCalibData("camera", 1, "pose");
    solution["baselineMm"] = 1000 * std::sqrt(pose.D(0) * pose.D(0) + pose.D(1) * pose.D(1) + pose.D(2) * pose.D(2));
  }
  out = calibrationView(solution, samples, source, p.algorithm, d.captureId);
  // Publish only if the files stayed unchanged while their decoded pixel hashes
  // were verified and the model was solved. A changed file takes the full path.
  if (!cachePath.isEmpty() && exposureDigest(samples) == digest) {
    QSaveFile file(cachePath);
    const auto bytes = QJsonDocument(solution).toJson(QJsonDocument::Compact);
    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size()) file.commit();
  }
  out.algorithmMs = out.totalMs = timer.nsecsElapsed() / 1e6;
  if (profile) {
    auto &values = out.objects[0].values;
    values["profile_setup_ms"] = setupMs;
    values["profile_load_ms"] = loadMs;
    values["profile_detection_ms"] = detectionMs;
    values["profile_decode_ms"] = decodeMs;
    values["profile_projector_points_ms"] = projectorMs;
    values["profile_solve_ms"] = solveMs;
    values["profile_results_ms"] = tick();
    values["profile_total_ms"] = out.algorithmMs;
  }
  return out;
}
} // namespace workbench
