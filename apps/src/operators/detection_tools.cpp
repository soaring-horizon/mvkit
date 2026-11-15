// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
// Region operations use the public MVKit API; length scaling is Euclidean.
#include "operators/detection_tools.hpp"

#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "operators/operator_registry.hpp"
namespace workbench {
namespace {
void require(bool ok, const char* reason) {
  if (!ok) throw std::runtime_error(reason);
}
bool regionTool(Algorithm a) {
  return a >= Algorithm::RegionMorph && a <= Algorithm::RegionBoolean;
}
struct Feature {
  int index;
  double area, width, height, circularity;
  QPointF center;
};
Feature feature(const mv::MvRegion& region, int index) {
  double r, c;
  auto area = region.AreaCenter(&r, &c);
  std::int64_t top, left, bottom, right;
  region.SmallestRectangle1(&top, &left, &bottom, &right);
  return {index,
          double(area),
          double(right - left + 1),
          double(bottom - top + 1),
          region.Circularity().D(),
          {c, r}};
}
ResultObject regionObject(const mv::MvRegion& region, const Feature& f,
                          std::size_t& budget) {
  ResultObject r;
  r.type = "Region";
  r.measure = f.area;
  r.center = f.center;
  r.unit = QStringLiteral("px²");
  r.values = {{"area", f.area},
              {"width", f.width},
              {"height", f.height},
              {"circularity", f.circularity},
              {"sourceObject", double(f.index)}};
  mv::MvTuple rows, first, last;
  region.GetRegionRuns(&rows, &first, &last);
  require((budget += rows.Length() * 5) <= 2000000,
          "区域显示超过 200 万行程，请缩小范围");
  r.overlay.setFillRule(Qt::WindingFill);
  for (std::size_t j = 0; j < rows.Length(); ++j)
    r.overlay.addRect(
        QRectF(first.I(j) - .5, rows.I(j) - .5, last.I(j) - first.I(j) + 1, 1));
  return r;
}
}  // namespace
void validateDetection(const Parameters& p) {
  auto v = [&](const char* key, double d) {
    return p.options.value(key).toDouble(d);
  };
  require(p.calibrationName.size() <= 256, "标定资源名称过长");
  if (p.algorithm == Algorithm::RegionMorph) {
    require(v("operation", 0) >= 0 && v("operation", 0) <= 5 &&
                std::floor(v("operation", 0)) == v("operation", 0),
            "形态学操作无效");
    require(v("shape", 0) == 0 || v("shape", 0) == 1, "结构元素无效");
    require(v("radius", 2) >= .5 && v("radius", 2) <= 100,
            "结构元素半径须为 0.5–100 px");
    for (auto key : {"width", "height"})
      require(v(key, 5) >= 1 && v(key, 5) <= 201 &&
                  v(key, 5) == std::floor(v(key, 5)) && int(v(key, 5)) % 2 == 1,
              "结构元素宽高须为 1–201 的奇数");
  }
  if (p.algorithm == Algorithm::RegionFilter) {
    for (auto pair : QVector<QPair<const char*, const char*>>{
             {"minArea", "maxArea"},
             {"minWidth", "maxWidth"},
             {"minHeight", "maxHeight"},
             {"minCircularity", "maxCircularity"}}) {
      const double hi = QString(pair.second) == "maxCircularity" ? 1 : 1e12;
      require(v(pair.first, 0) >= 0 && v(pair.first, 0) <= v(pair.second, hi) &&
                  v(pair.second, hi) <= hi,
              "区域筛选上下限无效");
    }
  }
  if (p.algorithm == Algorithm::RegionFeatures)
    require(v("mode", 0) >= 0 && v("mode", 0) <= 9, "区域特征无效");
  if (p.algorithm == Algorithm::RegionBoolean)
    require(v("operation", 0) >= 0 && v("operation", 0) <= 4 &&
                v("operation", 0) == std::floor(v("operation", 0)),
            "区域集合操作无效");
  if (p.algorithm == Algorithm::ScaleCalibration)
    require(v("pixelLength", 100) > 0 && v("realLength", 10) > 0 &&
                v("pixelLength", 100) <= 1e9 && v("realLength", 10) <= 1e9,
            "标定参考长度必须大于零且不超过 10^9");
}
QString fieldUnit(const ResultObject& object, const QString& field) {
  if (object.type == "Plane3D" && field.startsWith("plane")) return "mm";
  if (field.startsWith("sphere")) return "mm";
  if (QStringList{"mean", "deviation", "minGray", "maxGray", "grayRange"}.contains(field)) return QStringLiteral("灰度");
  if (field == "angle" || field == "arc") return QStringLiteral("°");
  if (field == "area") return QStringLiteral("px²");
  if (field == "count") return QStringLiteral("个");
  if (field == "qualityTolerance") return "px";
  if (field == "sampleCount" || field == "outsideTolerance") return QStringLiteral("点");
  if (field == "circularity") return {};
  if (field == "value" || field == "lower" || field == "upper") return object.unit;
  if (field == "mmPerPixel") return "mm/px";
  if (field == "minZ" || field == "maxZ" || field=="medianZ") return "mm";
  if (QStringList{"worldX1", "worldY1", "worldX2", "worldY2"}.contains(field))
    return "mm";
  if (QStringList{"diameter", "radius", "length", "rms", "maxResidual",
                  "majorDiameter", "minorDiameter", "majorRadius",
                  "minorRadius", "width", "height", "pixelValue", "centerX",
                  "centerY", "x1", "x2", "y1", "y2"}
          .contains(field))
    return "px";
  return {};
}
QString fieldLabel(const QString& field) {
  static const QMap<QString, QString> names{
      {"value", QStringLiteral("主测量值")}, {"mean", QStringLiteral("灰度均值")},
      {"deviation", QStringLiteral("灰度标准差")}, {"minGray", QStringLiteral("最小灰度")},
      {"maxGray", QStringLiteral("最大灰度")}, {"grayRange", QStringLiteral("灰度极差")},
      {"planeRms", QStringLiteral("平面残差 RMS")}, {"planeMaxAbs", QStringLiteral("平面最大绝对残差")},
      {"planePeakToValley", QStringLiteral("平面残差峰谷差")}, {"planeOffset", QStringLiteral("平面偏移 D")},
      {"normalX", QStringLiteral("单位法向 X")}, {"normalY", QStringLiteral("单位法向 Y")}, {"normalZ", QStringLiteral("单位法向 Z")},
      {"sphereRadius", QStringLiteral("球半径")}, {"sphereDiameter", QStringLiteral("球直径")},
      {"sphereX", QStringLiteral("球心 X")}, {"sphereY", QStringLiteral("球心 Y")}, {"sphereZ", QStringLiteral("球心 Z")},
      {"sphereRms", QStringLiteral("球面残差 RMS")}, {"sphereMaxAbs", QStringLiteral("球面最大绝对残差")},
      {"spherePeakToValley", QStringLiteral("球面残差峰谷差")},
      {"radius", QStringLiteral("半径")}, {"diameter", QStringLiteral("直径")}, {"length", QStringLiteral("长度")},
      {"rms", QStringLiteral("全部点 RMS")}, {"maxResidual", QStringLiteral("最大残差")},
      {"angle", QStringLiteral("角度")}, {"arc", QStringLiteral("圆弧覆盖")},
      {"majorDiameter", QStringLiteral("长轴 / 长边")}, {"minorDiameter", QStringLiteral("短轴 / 短边")},
      {"majorRadius", QStringLiteral("长半轴")}, {"minorRadius", QStringLiteral("短半轴")},
      {"centerX", QStringLiteral("中心列")}, {"centerY", QStringLiteral("中心行")},
      {"count", QStringLiteral("区域数量")}, {"area", QStringLiteral("区域面积")},
      {"width", QStringLiteral("宽度")}, {"height", QStringLiteral("高度")}, {"circularity", QStringLiteral("圆度")},
      {"minZ", QStringLiteral("最小深度")}, {"maxZ", QStringLiteral("最大深度")}, {"medianZ", QStringLiteral("深度中位数")},
      {"sampleCount", QStringLiteral("采样点数")}, {"outsideTolerance", QStringLiteral("超差点数")},
      {"qualityTolerance", QStringLiteral("显示容差")}, {"pixelValue", QStringLiteral("原始像素长度")},
      {"mmPerPixel", QStringLiteral("每像素长度")}};
  return names.value(field, field);
}
ResultObject numericalOutputSchema(const Parameters& p) {
  ResultObject result;
  result.type = "Value";
  const int mode = p.options.value("mode").toInt();
  QStringList fields;
  switch (p.algorithm) {
    case Algorithm::PlaneFit:
      result.type = "Plane3D"; result.unit = "mm";
      fields = QStringList{"value", "normalX", "normalY", "normalZ", "planeOffset", "planeRms", "planeMaxAbs", "planePeakToValley"}; break;
    case Algorithm::SphereFit:
      result.unit = "mm";
      fields = QStringList{"value", "sphereRadius", "sphereDiameter", "sphereX", "sphereY", "sphereZ", "sphereRms", "sphereMaxAbs", "spherePeakToValley"}; break;
    case Algorithm::GrayStats:
      result.unit = QStringLiteral("灰度"); fields = QStringList{"value", "mean", "deviation", "minGray", "maxGray", "grayRange"}; break;
    case Algorithm::CloudMeasure:
      result.unit = "mm"; fields = QStringList{"value"};
      if (mode == 0) fields << "minZ" << "maxZ";
      if (mode == 2) fields << "medianZ";
      break;
    case Algorithm::RegionFeatures:
      if (mode == 0) { result.unit = QStringLiteral("个"); fields = QStringList{"value", "count"}; }
      else {
        fields = QStringList{"value", "area", "width", "height", "circularity", "sourceObject", "centerX", "centerY"};
        if (mode >= 7) fields << "majorDiameter" << "minorDiameter" << "angle";
        const QString selected = QStringList{"count", "area", "width", "height", "circularity", "centerX", "centerY", "majorDiameter", "minorDiameter", "angle"}.value(mode);
        result.unit = fieldUnit(result, selected);
      }
      break;
    case Algorithm::LineFit: case Algorithm::CircleFit: case Algorithm::EllipseFit:
      result.unit = "px";
      fields = QStringList{"value", "sourceObject", "rms", "maxResidual", "sampleCount", "qualityTolerance", "outsideTolerance", "centerX", "centerY"};
      if (p.algorithm == Algorithm::LineFit) fields << "length" << "angle" << "x1" << "y1" << "x2" << "y2";
      else if (p.algorithm == Algorithm::CircleFit) fields << "radius" << "diameter" << "arc";
      else fields << "majorDiameter" << "minorDiameter" << "majorRadius" << "minorRadius" << "angle" << "arc";
      break;
    case Algorithm::Measure:
      result.unit = mode == 4 ? QStringLiteral("°") : "px"; fields = QStringList{"value"}; break;
    case Algorithm::PlaneMeasure:
      result.unit = "mm"; fields = QStringList{"value", "worldX1", "worldY1", "worldX2", "worldY2"}; break;
    case Algorithm::ScaleCalibration:
      result.unit = "mm"; fields = QStringList{"value", "pixelValue", "mmPerPixel"}; break;
    default: break;
  }
  for (const auto& field : fields) result.values[field] = 0;
  return result;
}
Result executeDetection(const Document& stage, const NodeOutput* a,
                        const NodeOutput* b) {
  const auto& p = stage.parameters;
  p.validate();
  auto v = [&](const char* key, double d) {
    return p.options.value(key).toDouble(d);
  };
  Result out;
  out.outputImage = stage.image;
  out.pipeline = operatorDefinition(p.algorithm).name;
  QElapsedTimer timer;
  timer.start();
  if (p.algorithm == Algorithm::ScaleCalibration) {
    require(a && a->state == NodeState::Success,
            "比例标定需要有效像素长度输入");
    for (const auto& source : a->objects) {
      require(source.type == "Value" && source.unit == "px",
              "比例标定只接受像素长度，不能换算面积、角度或已标定数值");
      auto r = source;
      r.type = "Value";
      r.unit = "mm";
      r.label = QStringLiteral("标定长度");
      const double scale = v("realLength", 10) / v("pixelLength", 100);
      r.measure = source.measure * scale;
      require(std::isfinite(r.measure), "标定数值溢出");
      r.values = {{"value", r.measure},
                  {"pixelValue", source.measure},
                  {"mmPerPixel", scale}};
      out.objects.append(r);
    }
    require(!out.objects.isEmpty(), "没有可换算的像素长度");
    out.measureName = QStringLiteral("长度 / mm");
  } else if (regionTool(p.algorithm)) {
    mv::MvRegion regions;
    if (a) {
      require(a->state == NodeState::Success, "区域输入未成功执行");
      regions = a->regions;
    } else {
      require(p.algorithm != Algorithm::RegionBoolean,
              "区域集合需要工作流输入");
      auto input = stage.image.Channels() == 3
                       ? stage.image.FullDomain().Rgb1ToGray().ReduceDomain(
                             stage.image.GetDomain())
                       : stage.image;
      regions = input.Threshold(p.minimum, p.maximum).Connection();
    }
    if (p.algorithm == Algorithm::RegionMorph && regions.CountObj()) {
      const int op = int(v("operation", 0));
      const bool rect = v("shape", 0) == 1;
      const int w = int(v("width", 5)), h = int(v("height", 5));
      const double rad = v("radius", 2);
      if (op == 0)
        regions =
            rect ? regions.OpeningRectangle1(w, h) : regions.OpeningCircle(rad);
      else if (op == 1)
        regions =
            rect ? regions.ClosingRectangle1(w, h) : regions.ClosingCircle(rad);
      else if (op == 2)
        regions = rect ? regions.DilationRectangle1(w, h)
                       : regions.DilationCircle(rad);
      else if (op == 3)
        regions =
            rect ? regions.ErosionRectangle1(w, h) : regions.ErosionCircle(rad);
      else if (op == 4)
        regions = regions.FillUp();
      else
        regions = regionMask(regions).Connection();
    }
    if (p.algorithm == Algorithm::RegionBoolean) {
      auto mask = regionMask(regions);
      const int op = int(v("operation", 0));
      if (op) {
        require(b && b->state == NodeState::Success, "区域集合需要有效输入 B");
        const auto other = regionMask(b->regions);
        mask = op == 1   ? mask.Union2(other)
               : op == 2 ? mask.Intersection(other)
               : op == 3 ? mask.Difference(other)
                         : mask.SymmDifference(other);
      }
      regions = mask.Connection();
    }
    require(regions.CountObj() <= 10000, "区域超过 10000 个，请先减少对象");
    QVector<Feature> features;
    for (int i = 1; i <= regions.CountObj(); ++i) {
      auto selected = regions.SelectObj(i);
      double row, col;
      if (selected.AreaCenter(&row, &col) == 0) continue;
      auto f = feature(selected, i);
      if (p.algorithm == Algorithm::RegionFilter &&
          (f.area < v("minArea", 0) || f.area > v("maxArea", 1e12) ||
           f.width < v("minWidth", 0) || f.width > v("maxWidth", 1e12) ||
           f.height < v("minHeight", 0) || f.height > v("maxHeight", 1e12) ||
           f.circularity < v("minCircularity", 0) ||
           f.circularity > v("maxCircularity", 1)))
        continue;
      features.append(f);
    }
    if (p.algorithm == Algorithm::RegionFilter) {
      int sort = int(v("sort", 0));
      if (sort)
        std::stable_sort(features.begin(), features.end(),
                         [sort](const Feature& x, const Feature& y) {
                           return sort == 1   ? x.area > y.area
                                  : sort == 2 ? x.center.x() < y.center.x()
                                              : x.center.y() < y.center.y();
                         });
      int take = int(v("take", 0));
      if (take > 0 && features.size() > take) features.resize(take);
    }
    std::size_t displayBudget = 0;
    const int mode = int(v("mode", 0));
    if (p.algorithm == Algorithm::RegionFeatures && mode == 0) {
      ResultObject count;
      count.type = "Value";
      count.measure = features.size();
      count.unit = QStringLiteral("个");
      count.label = QStringLiteral("区域数量");
      count.values = {{"value", count.measure}, {"count", count.measure}};
      out.objects.append(count);
      out.measureName = QStringLiteral("数量 / 个");
    } else
      for (const auto& f : features) {
        const auto selected = regions.SelectObj(f.index);
        auto r = regionObject(selected, f, displayBudget);
        if (p.algorithm == Algorithm::RegionFeatures) {
          const QString field = QStringList{"count", "area", "width", "height",
                                            "circularity", "centerX", "centerY", "majorDiameter", "minorDiameter", "angle"}[mode];
          r.values["centerX"] = f.center.x();
          r.values["centerY"] = f.center.y();
          if (mode >= 7) {
            double row, col, phi, halfLength, halfWidth;
            selected.SmallestRectangle2(&row, &col, &phi, &halfLength, &halfWidth);
            r.values["majorDiameter"] = halfLength * 2;
            r.values["minorDiameter"] = halfWidth * 2;
            r.values["angle"] = -phi * 180. / 3.14159265358979323846;
          }
          r.type = "Value";
          r.measure = r.values[field];
          r.unit = fieldUnit(r, field);
          r.values["value"] = r.measure;
          r.label = QStringList{QStringLiteral("数量"), QStringLiteral("面积"),
                                QStringLiteral("宽度"), QStringLiteral("高度"),
                                QStringLiteral("圆度"), QStringLiteral("质心列"), QStringLiteral("质心行"),
                                QStringLiteral("旋转长边"), QStringLiteral("旋转短边"), QStringLiteral("旋转框方向")}[mode];
        } else
          out.regions = out.objects.isEmpty() ? selected
                                              : out.regions.ConcatObj(selected);
        out.objects.append(r);
      }
    if (out.measureName.isEmpty())
      out.measureName = p.algorithm == Algorithm::RegionFeatures
                            ? (out.objects.isEmpty() ? QStringLiteral("区域特征") : out.objects.first().label)
                            : QStringLiteral("面积 / px²");
  }
  out.algorithmMs = out.totalMs = timer.nsecsElapsed() / 1e6;
  out.steps.append({out.pipeline, out.algorithmMs});
  return out;
}
}  // namespace workbench
