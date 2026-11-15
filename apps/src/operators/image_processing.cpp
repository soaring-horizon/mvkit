// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
// Public affine image/region APIs use (row,column) pixel corners. The UI uses
// (column,row) pixel centers; the three correspondences below conjugate the
// independently constructed UI transform by this exchange and half-pixel shift.
#include "operators/image_processing.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <cmath>
#include <stdexcept>

#include "operators/calibration_tools.hpp"
#include "operators/operator_registry.hpp"
namespace workbench {
namespace {
void require(bool b, const char* text) {
  if (!b) throw std::runtime_error(text);
}
}  // namespace
bool imageProcessingTool(Algorithm a) {
  return (a >= Algorithm::ImageMorph && a <= Algorithm::ImageRectify) || a == Algorithm::GrayStats || a == Algorithm::Polar;
}
QJsonObject calibrationConfigJson(QJsonObject config, const QString& path,
                                  bool save) {
  // Bundled projects record paths relative to their own model files. Once
  // loaded, preserve the absolute provenance even when saving elsewhere.
  const auto source = config["sourcePath"].toString();
  if (!save && !source.isEmpty() && QDir::isRelativePath(source))
    config["sourcePath"] = QFileInfo(path).absoluteDir().absoluteFilePath(source);
  const auto board = config["boardPath"].toString();
  if (!board.isEmpty())
    config["boardPath"] =
        save ? QFileInfo(path).absoluteDir().relativeFilePath(board)
             : QFileInfo(path).absoluteDir().absoluteFilePath(board);
  return config;
}
void validateProcessing(const Parameters& p) {
  if (p.algorithm < Algorithm::ImageMorph) return;
  auto v = [&](const char* key, double d) {
    return p.options.value(key).toDouble(d);
  };
  auto selection = [&](const char* key, int maximum) {
    double n = v(key, 0);
    require(std::isfinite(n) && n >= 0 && n <= maximum && n == std::floor(n),
            "图像处理选项无效");
  };
  require(p.calibrationConfig.size() <= 16 &&
              p.calibrationArtifact.size() <= 8 * 1024 * 1024,
          "标定配置或资源过大");
  if(calibrationTool(p.algorithm)) {
    for(const auto* key:{"focusMM","secondFocusMM"})require(v(key,12)>=.1&&v(key,12)<=500,"标定初始焦距须为 0.1–500 mm");
    for(const auto* key:{"pitchUM","secondPitchUM"})require(v(key,3.45)>=.1&&v(key,3.45)<=100,"标定像元尺寸须为 0.1–100 µm");
    if(p.algorithm==Algorithm::MonoCalibration)require(v("planeView",1)>=1&&v("planeView",1)<=64&&v("planeView",1)==std::floor(v("planeView",1)),"世界平面姿态编号须为 1–64");
    if(p.algorithm==Algorithm::StructuredCalibration) {
      const auto w=v("projectorWidth",1280),h=v("projectorHeight",720),stripe=v("stripeWidth",16);
      require(w>=8&&h>=8&&w<=8192&&h<=8192&&w*h<=1000000&&w==std::floor(w)&&h==std::floor(h),"投影图案须为整数尺寸，最多 100 万像素");
      require(stripe>=2&&stripe<=256&&stripe==std::floor(stripe),"条纹宽须为 2–256 的整数");
      require(v("minContrast",10)>=0&&v("minContrast",10)<=255&&v("minContrast",10)==std::floor(v("minContrast",10)),"曝光对比度须为 0–255 的整数");
    }
  }
  if (p.algorithm == Algorithm::StereoDisparity) {
    for (auto key : {"minDisparity", "maxDisparity"})
      require(
          v(key, key == QString("minDisparity") ? -64 : 0) >= -512 &&
              v(key, key == QString("minDisparity") ? -64 : 0) <= 512 &&
              v(key, key == QString("minDisparity") ? -64 : 0) ==
                  std::floor(v(key, key == QString("minDisparity") ? -64 : 0)),
          "视差界限必须为整数且在 -512–512");
    require(v("minDisparity", -64) <= v("maxDisparity", 0), "视差上下限反向");
    for (auto key : {"surface", "edge"})
      require(v(key, key == QString("surface") ? 6 : 35) >= 0 &&
                  v(key, key == QString("surface") ? 6 : 35) <= 255,
              "平滑参数无效");
  }
  if (p.algorithm == Algorithm::CloudSelect)
    require(v("minZ", 0) >= 0 && v("maxZ", 100000) <= 1e9 &&
                v("minZ", 0) <= v("maxZ", 100000),
            "毫米深度区间无效");
  if (p.algorithm == Algorithm::DisparityFilter) {
    require(v("maxCost", 32) >= 0 && v("maxCost", 32) <= 1e9,
            "匹配代价上限须为 0–10⁹");
    require(v("minArea", 5) >= 0 && v("minArea", 5) <= 1000000 &&
                v("minArea", 5) == std::floor(v("minArea", 5)),
            "有效连通面积须为 0–1000000 的整数");
  }
  if (p.algorithm == Algorithm::CloudMeasure) {
    selection("mode", 3);
    for (auto key : {"pointA", "pointB"})
      require(v(key, 1) >= 1 && v(key, 1) <= 1000000 &&
                  v(key, 1) == std::floor(v(key, 1)),
              "三维点编号无效");
  }
  if (p.algorithm == Algorithm::ImageMorph) {
    selection("operation", 5);
    for (auto key : {"width", "height"})
      require(v(key, 5) >= 1 && v(key, 5) <= 101 &&
                  v(key, 5) == std::floor(v(key, 5)) && int(v(key, 5)) % 2 == 1,
              "灰度形态学宽高须为 1–101 奇数");
  }
  if (p.algorithm == Algorithm::PlaneFit) {
    selection("mode", 2);
  }
  if (p.algorithm == Algorithm::SphereFit) {
    selection("mode", 2);
    require(v("minRadius", .01) > 0 && v("minRadius", .01) <= v("maxRadius", 10000) &&
            v("maxRadius", 10000) <= 1e6, "球半径范围须满足 0 < 下限 ≤ 上限 ≤ 1000000 mm");
  }
  if (p.algorithm == Algorithm::GrayStats) selection("mode", 4);
  if (p.algorithm == Algorithm::Polar) {
    selection("interpolation", 1);
    for (const auto key : {"outWidth", "outHeight"}) {
      const double n = v(key, 360);
      require(n >= 2 && n <= 4096 && n == std::floor(n), "展开尺寸须为 2–4096 的整数");
    }
    for (const auto key : {"inner", "outer"})
      require(v(key, 0) >= 0 && v(key, 0) <= 100000, "半径超出范围");
    for (const auto key : {"centerX", "centerY"})
      require(v(key, -1) == -1 || (v(key, -1) >= 0 && v(key, -1) <= 100000), "圆心超出范围");
    require(std::abs(v("start",0)) <= 360 && std::abs(v("end",360)) <= 360 && v("start",0) != v("end",360), "展开角度区间无效");
  }
  if (p.algorithm == Algorithm::ImageAffine) {
    selection("inverse", 1);
    selection("interpolation", 1);
    require(
        std::abs(v("scaleX", 1)) >= .01 && std::abs(v("scaleX", 1)) <= 100 &&
            std::abs(v("scaleY", 1)) >= .01 && std::abs(v("scaleY", 1)) <= 100,
        "仿射缩放绝对值须在 0.01–100");
    for (auto key : {"tx", "ty", "centerX", "centerY"})
      require(std::abs(v(key, 0)) <= 1e6, "仿射坐标超过范围");
    require(std::abs(v("rotation", 0)) <= 180 && std::abs(v("shear", 0)) <= 10,
            "仿射旋转或切变无效");
  }
  if (p.algorithm == Algorithm::ImageGradient) {
    selection("method", 4);
    require(v("sigma", 1.2) >= .3 && v("sigma", 1.2) <= 10,
            "梯度平滑系数须为 0.3–10");
  }
  if (p.algorithm == Algorithm::ImageEnhance) {
    selection("operation", 1);
    require(v("gain", 1) >= 0 && v("gain", 1) <= 100 &&
                std::abs(v("offset", 0)) <= 65535 && v("factor", 1) >= 0 &&
                v("factor", 1) <= 10,
            "图像增强参数无效");
  }
  if (p.algorithm == Algorithm::ImageRectify) {
    selection("mode", 1);
    selection("interpolation", 1);
    require(v("mmPerPixel", .1) > 0 && v("mmPerPixel", .1) <= 100,
            "平面像素比例无效");
    for (auto key : {"outWidth", "outHeight"})
      require(v(key, 640) >= 1 && v(key, 640) <= 8192 &&
                  v(key, 640) == std::floor(v(key, 640)),
              "校正图尺寸无效");
    require(v("outWidth", 640) * v("outHeight", 640) <= 16000000,
            "校正图超过 1600 万像素");
  }
}
mv::MvHomMat2D affineMatrix(const Parameters& p) {
  auto v = [&](const char* key, double d) {
    return p.options.value(key).toDouble(d);
  };
  QTransform t;
  t.translate(v("tx", 0) + v("centerX", 0), v("ty", 0) + v("centerY", 0));
  t.rotate(v("rotation", 0));
  t.shear(v("shear", 0), 0);
  t.scale(v("scaleX", 1), v("scaleY", 1));
  t.translate(-v("centerX", 0), -v("centerY", 0));
  mv::MvTuple rr(0.), cc(0.), outR, outC;
  rr.Append(1.).Append(0.);
  cc.Append(0.).Append(1.);
  for (int i = 0; i < 3; ++i) {
    const auto q = t.map(QPointF(cc.D(i) - .5, rr.D(i) - .5));
    outR.Append(q.y() + .5);
    outC.Append(q.x() + .5);
  }
  mv::MvHomMat2D matrix;
  matrix.VectorToHomMat2d(rr, cc, outR, outC);
  return v("inverse", 0) == 1 ? matrix.HomMat2dInvert() : matrix;
}
QString processingFrame(const QString& frame, const Parameters& p) {
  if (p.algorithm != Algorithm::ImageAffine &&
      p.algorithm != Algorithm::ImageRectify && p.algorithm != Algorithm::Polar)
    return frame;
  if (p.algorithm == Algorithm::ImageAffine &&
      p.options.value("tx").toDouble() == 0 &&
      p.options.value("ty").toDouble() == 0 &&
      p.options.value("rotation").toDouble() == 0 &&
      p.options.value("shear").toDouble() == 0 &&
      p.options.value("scaleX").toDouble(1) == 1 &&
      p.options.value("scaleY").toDouble(1) == 1)
    return frame;
  const auto bytes = QJsonDocument(p.options).toJson(QJsonDocument::Compact) +
                     p.calibrationArtifact;
  return frame + "|grid:" + QString::number(int(p.algorithm)) + ":" +
         QString::fromLatin1(
             QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
                 .toHex()
                 .left(16));
}
Result executeProcessing(const Document& d) {
  const auto& p = d.parameters;
  p.validate();
  if (p.algorithm == Algorithm::CalibrationDetect)
    return detectCalibrationBoard(d);
  if (p.algorithm == Algorithm::ImageRectify) return rectifyCalibrationImage(d);
  auto v = [&](const char* key, double value) {
    return p.options.value(key).toDouble(value);
  };
  auto input = d.image;
  if (d.roi.kind != RoiKind::None)
    input = input.ReduceDomain(makeRegion(d.roi));
  require(input.CountObj() == 1, "图像处理需要单张图像");
  Result r;
  QElapsedTimer timer;
  timer.start();
  if (p.algorithm == Algorithm::ImageMorph) {
    int op = int(v("operation", 0)), w = int(v("width", 5)),
        h = int(v("height", 5));
    r.outputImage = op == 0   ? input.GrayErosionRect(h, w)
                    : op == 1 ? input.GrayDilationRect(h, w)
                    : op == 2 ? input.GrayOpeningRect(h, w)
                    : op == 3 ? input.GrayClosingRect(h, w)
                    : op == 4
                        ? input.SubImage(input.GrayOpeningRect(h, w), 1, 0)
                        : input.GrayClosingRect(h, w).SubImage(input, 1, 0);
  } else if (p.algorithm == Algorithm::GrayStats) {
    const auto gray = input.Channels() == 3 ? input.FullDomain().Rgb1ToGray().ReduceDomain(input.GetDomain()) : input;
    const auto region = gray.GetDomain();
    double row, col, deviation, low, high, range;
    require(region.AreaCenter(&row, &col) > 0, "灰度统计范围为空");
    const double mean = gray.Intensity(region, &deviation);
    gray.MinMaxGray(region, 0., &low, &high, &range);
    const QStringList names{QStringLiteral("均值"), QStringLiteral("标准差"), QStringLiteral("最小值"), QStringLiteral("最大值"), QStringLiteral("极差")};
    const double values[]{mean, deviation, low, high, range};
    const int mode = int(v("mode", 0));
    ResultObject item;
    item.type = "Value";
    item.unit = QStringLiteral("灰度");
    item.label = names[mode];
    item.measure = values[mode];
    item.center = {col, row};
    item.values = {{"value", item.measure}, {"mean", mean}, {"deviation", deviation}, {"minGray", low}, {"maxGray", high}, {"grayRange", range}};
    r.objects = {item};
    r.measureName = item.label;
    r.outputImage = input;
  } else if (p.algorithm == Algorithm::Polar) {
    const double x = v("centerX",-1) < 0 ? (input.Width()-1)*.5 : v("centerX",0);
    const double y = v("centerY",-1) < 0 ? (input.Height()-1)*.5 : v("centerY",0);
    const double outer = v("outer",0) == 0 ? std::min({x, y, input.Width()-1-x, input.Height()-1-y}) : v("outer",0);
    require(outer >= 0 && outer != v("inner",0), "展开半径区间无效，请检查圆心和内外半径");
    constexpr double radians = 3.14159265358979323846 / 180.;
    const auto unwrap = [&](const mv::MvImage& image) {
      return image.PolarTransImageExt(y, x, v("start",0)*radians, v("end",360)*radians,
          v("inner",0), outer, int(v("outWidth",360)), int(v("outHeight",128)),
          v("interpolation",0) ? "bilinear" : "nearest_neighbor");
    };
    // PolarTransImageExt samples the full raster. Transform validity separately so
    // an excluded input pixel or out-of-frame sample cannot become a measurement.
    mv::MvImage mask;
    mask.GenImageConst("real", input.Width(), input.Height());
    mask = mask.PaintRegion(input.GetDomain(), 1., "fill");
    r.outputImage = unwrap(input).ReduceDomain(unwrap(mask).Threshold(.999999, 1.000001));
  } else if (p.algorithm == Algorithm::ImageAffine) {
    const auto matrix = affineMatrix(p);
    r.outputImage = input.AffineTransImage(
        matrix, v("interpolation", 0) == 0 ? "nearest_neighbor" : "bilinear",
        "false");
    r.outputImage = r.outputImage.ReduceDomain(
        input.GetDomain().AffineTransRegion(matrix, "nearest_neighbor"));
  } else if (p.algorithm == Algorithm::ImageGradient) {
    auto gray =
        input.Channels() == 3
            ? input.FullDomain().Rgb1ToGray().ReduceDomain(input.GetDomain())
            : input;
    int method = int(v("method", 0));
    r.outputImage = method < 2
                        ? gray.SobelAmp(method == 0 ? "sum_abs" : "sum_sqrt", 3)
                    : method == 2 ? gray.DerivateGauss(v("sigma", 1.2), "x")
                    : method == 3 ? gray.DerivateGauss(v("sigma", 1.2), "y")
                                  : gray.LaplaceOfGauss(v("sigma", 1.2));
  } else if (p.algorithm == Algorithm::ImageEnhance)
    r.outputImage = v("operation", 0) == 0
                        ? input.ScaleImage(v("gain", 1), v("offset", 0))
                        : input.Emphasize(7, 7, v("factor", 1));
  r.algorithmMs = timer.nsecsElapsed() / 1e6;
  r.preview = makePreview(r.outputImage).image;
  r.pipeline = operatorDefinition(p.algorithm).name;
  if (r.measureName.isEmpty()) r.measureName = QStringLiteral("结果图像");
  r.steps.append({r.pipeline, r.algorithmMs});
  r.totalMs = timer.nsecsElapsed() / 1e6;
  return r;
}
}  // namespace workbench
