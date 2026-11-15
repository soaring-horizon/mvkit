// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
// Analytic distances and residuals use Euclidean geometry, independently
// derived here; fitting uses MVKit's existing public XLD API.
#include <QElapsedTimer>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "operators/calibration_tools.hpp"
#include "runtime/execution.hpp"
#include "operators/fitting_quality.hpp"
#include "operators/operator_registry.hpp"
#include "model/spatial_data.hpp"
namespace workbench {
namespace {
constexpr double pi = 3.14159265358979323846;
[[noreturn]] void fail(const char *message) {
  throw std::runtime_error(message);
}
double length(QPointF p) { return std::hypot(p.x(), p.y()); }
double dot(QPointF a, QPointF b) { return a.x() * b.x() + a.y() * b.y(); }
QPointF projection(QPointF p, const ResultObject &line) {
  const auto v = line.second - line.first;
  if (dot(v, v) < 1e-16) fail("直线退化，无法计算距离");
  return line.first + v * (dot(p - line.first, v) / dot(v, v));
}
const ResultObject &object(const NodeOutput *input, int index) {
  if (!input || input->state != NodeState::Success)
    fail("未连接有效的类型化输入，请连接上游并选择对象");
  if (index < 1 || index > input->objects.size())
    fail("所选对象编号不存在，请筛选轮廓或调整对象编号");
  return input->objects[index - 1];
}
}  // namespace
bool acceptsData(Algorithm tool, const QString &type, bool second) {
  if (calibrationTool(tool)) return !second && type==(tool==Algorithm::StereoCalibration?"MultiView":"ImageSequence");
  if (tool == Algorithm::StereoRectify) return type == "Image" || !second && type == "MultiView";
  if (tool == Algorithm::StructuredDecode) return !second && type == "ImageSequence";
  if (!dataTool(tool)) return false;
  if (tool == Algorithm::StructuredReconstruct)
    return !second && type == "Correspondence";
  if (tool == Algorithm::StructuredImage)
    return !second && (type == "Correspondence" || type == "PointCloud");
  if (tool == Algorithm::StereoDisparity)
    return !second && type == "StereoPair";
  if (tool == Algorithm::StereoReconstruct)
    return !second && type == "Disparity";
  if (tool == Algorithm::DisparityFilter)
    return !second && type == "Disparity";
  if (tool == Algorithm::StereoImage)
    return !second && (type == "StereoPair" || type == "Disparity" || type == "PointCloud");
  if (tool == Algorithm::CloudSelect)
    return second ? type == "Region" : type == "PointCloud";
  if (tool == Algorithm::CloudMeasure) return type == "PointCloud" || (second && type == "Plane3D");
  if (tool == Algorithm::SphereFit || tool == Algorithm::PlaneFit) return !second && type == "PointCloud";
  if (tool == Algorithm::ContourFilter || tool == Algorithm::LineFit ||
      tool == Algorithm::CircleFit || tool == Algorithm::EllipseFit ||
      tool == Algorithm::ContourProcess)
    return !second && type == "XLD";
  if (tool == Algorithm::RegionMorph || tool == Algorithm::RegionFilter ||
      tool == Algorithm::RegionFeatures || tool == Algorithm::RegionBoolean)
    return type == "Region" && (!second || tool == Algorithm::RegionBoolean);
  if (tool == Algorithm::PlaneMeasure)
    return type == "Point" || type == "Circle" || type == "Ellipse" ||
           type == "Line" || type == "Match";
  if (tool == Algorithm::ScaleCalibration) return !second && type == "Value";
  if (tool == Algorithm::Judge)
    return !second && (type == "Value" || type == "Plane3D" || type == "Circle" || type == "Line" ||
                       type == "Ellipse");
  return type == "Circle" || type == "Line" || type == "Match" ||
         type == "Ellipse" || type == "Point";
}
Result executeGeometry(const Document &stage, const NodeOutput *a,
                       const NodeOutput *b) {
  if (spatialTool(stage.parameters.algorithm)) return executeSpatial(stage, a, b);
  if (stage.parameters.algorithm == Algorithm::PlaneMeasure)
    return executePlaneMeasure(stage, a, b);
  if (int(stage.parameters.algorithm) >= int(Algorithm::RegionMorph))
    return executeDetection(stage, a, b);
  Result out;
  QElapsedTimer timer;
  timer.start();
  const auto &p = stage.parameters;
  p.validate();
  const auto &o = p.options;
  auto v = [&](const char *key, double def) {
    return o.value(key).toDouble(def);
  };
  out.outputImage = stage.image;  // Raster is only a carrier; output remains
                                  // typed geometry, not Image.
  out.pipeline = operatorDefinition(p.algorithm).name;
  if (p.algorithm == Algorithm::ContourFilter ||
      p.algorithm == Algorithm::LineFit ||
      p.algorithm == Algorithm::CircleFit ||
      p.algorithm == Algorithm::EllipseFit ||
      p.algorithm == Algorithm::ContourProcess) {
    mv::MvXLDCont contours;
    if (a)
      contours = a->contours;
    else {
      // Explicit independent test source, shown in the parameter panel.
      auto image = stage.image.Channels() == 3
                       ? stage.image.FullDomain().Rgb1ToGray().ReduceDomain(
                             stage.image.GetDomain())
                       : stage.image;
      contours = image.EdgesSubPix("canny", p.sigma, p.edgeLow, p.edgeHigh);
    }
    if (p.algorithm == Algorithm::ContourProcess) {
      const int operation = int(v("operation", 0));
      if (operation == 0)
        contours = contours.SmoothContoursXld(int(v("smooth", 5)));
      else if (operation == 1)
        contours =
            contours.SegmentContoursXld("lines_circles", int(v("smooth", 5)),
                                        v("distance", 2), v("distance", 2) * 2);
      else if (operation == 2)
        contours = contours.UnionAdjacentContoursXld(
            v("distance", 2), v("relativeGap", .1), "attr_keep");
      else if (operation == 3)
        contours = contours.UnionCollinearContoursXld(
            v("distance", 2), v("relativeGap", .1), v("shift", 2),
            v("angle", 10) * pi / 180, "attr_keep");
      else
        contours = contours.UnionCocircularContoursXld(
            v("angle", 10) * pi / 180, v("angle", 10) * pi / 180,
            v("angle", 10) * pi / 180, v("distance", 2), v("shift", 2),
            v("shift", 2), "false", 5);
    }
    QVector<int> sourceObjects;
    if (p.algorithm == Algorithm::ContourFilter && v("borderMargin", 0) > 0) {
      const auto domain = a ? a->effectiveDomain : stage.image.GetDomain();
      if (!domain.CountObj()) fail("缺少有效定义域，无法排除范围边缘");
      const auto interior = domain.ErosionCircle(v("borderMargin", 0));
      mv::MvXLDCont clipped;
      int pieces = 0;
      std::size_t processed = 0;
      for (std::int64_t i = 1; i <= contours.CountObj(); ++i) {
        mv::MvTuple rows, cols, rr, cc;
        contours.SelectObj(i).GetContourXld(&rows, &cols);
        if ((processed += rows.Length()) > 2000000) fail("轮廓点超过预算");
        auto flush = [&] {
          if (rr.Length() > 1) {
            mv::MvXLDCont piece;
            piece.GenContourPolygonXld(rr, cc);
            clipped = pieces++ ? clipped.ConcatObj(piece) : piece;
            sourceObjects.append(int(i));
          }
          rr = {};
          cc = {};
        };
        for (std::size_t j = 0; j < rows.Length(); ++j) {
          if (interior.TestRegionPoint(std::int64_t(std::llround(rows.D(j))),
                                       std::int64_t(std::llround(cols.D(j))))) {
            rr.Append(rows.D(j));
            cc.Append(cols.D(j));
          } else
            flush();
        }
        flush();
      }
      contours = clipped;
    }
    const int count = int(contours.CountObj());
    if (count > 10000) fail("轮廓超过 10000 个，请先缩小范围");
    struct Item {
      int index;
      double length;
      QPointF center;
    };
    QVector<Item> items;
    std::size_t budget = 0;
    for (int i = 1; i <= count; ++i) {
      const auto contour = contours.SelectObj(i);
      mv::MvTuple rows, cols;
      contour.GetContourXld(&rows, &cols);
      if ((budget += rows.Length()) > 2000000)
        fail("轮廓点超过 200 万，请缩小范围");
      const double len = contour.LengthXld().D();
      QRectF bounds;
      for (std::size_t j = 0; j < rows.Length(); ++j) {
        const QPointF pt(cols.D(j), rows.D(j));
        if (j == 0)
          bounds = QRectF(pt, QSizeF(.00001, .00001));
        else
          bounds = bounds.united(QRectF(pt, QSizeF(.00001, .00001)));
      }
      const bool closed =
          rows.Length() > 2 &&
          length(QPointF(cols.D(0) - cols.D(cols.Length() - 1),
                         rows.D(0) - rows.D(rows.Length() - 1))) <= 2.;
      if (p.algorithm == Algorithm::ContourFilter &&
          (len < v("minLength", 0) || len > v("maxLength", 1e9) ||
           (v("closed", 0) == 1 && !closed) || (v("closed", 0) == 2 && closed)))
        continue;
      items.append({i, len, bounds.center()});
    }
    const int sort = int(v("sort", 0));
    if (p.algorithm == Algorithm::ContourFilter && sort)
      std::stable_sort(items.begin(), items.end(),
                       [sort](const Item &x, const Item &y) {
                         return sort == 1   ? x.length > y.length
                                : sort == 2 ? x.center.x() < y.center.x()
                                            : x.center.y() < y.center.y();
                       });
    const int take = int(v("take", 0));
    if (p.algorithm == Algorithm::ContourFilter && take > 0 &&
        items.size() > take)
      items.resize(take);
    for (const auto &item : items) {
      const auto contour = contours.SelectObj(item.index);
      mv::MvTuple rows, cols;
      contour.GetContourXld(&rows, &cols);
      ResultObject r;
      r.values["sourceObject"] =
          sourceObjects.value(item.index - 1, item.index);
      r.center = item.center;
      if (p.algorithm == Algorithm::ContourFilter ||
          p.algorithm == Algorithm::ContourProcess) {
        if (p.algorithm == Algorithm::ContourProcess) {
          r.values.remove("sourceObject");
          r.values["outputContour"] = item.index;
        }
        r.type = "XLD";
        r.measure = item.length;
        r.values["length"] = item.length;
        r.unit = "px";
        for (std::size_t j = 0; j < rows.Length(); ++j) {
          QPointF pt(cols.D(j), rows.D(j));
          if (!j)
            r.overlay.moveTo(pt);
          else
            r.overlay.lineTo(pt);
        }
        out.contours =
            out.objects.isEmpty() ? contour : out.contours.ConcatObj(contour);
      } else {
        if (rows.Length() < (p.algorithm == Algorithm::LineFit ? 2 : 6))
          fail("轮廓采样点不足，无法拟合；请先筛选轮廓");
        const int method = int(v("method", 0));
        if (p.algorithm == Algorithm::LineFit) {
          double r1, c1, r2, c2, nr, nc, d;
          contour.FitLineContourXld(method == 0   ? "regression"
                                    : method == 1 ? "huber"
                                                  : "tukey",
                                    -1, 0, 5, 2., &r1, &c1, &r2, &c2, &nr, &nc,
                                    &d);
          // Public fit endpoints project original first/last samples; a closed
          // contour may coincide there. Display the full projected sample
          // extent.
          const QPointF base(c1, r1), tangent(nr, -nc);
          double lo = 1e300, hi = -1e300;
          for (std::size_t j = 0; j < rows.Length(); ++j) {
            const double t = dot(QPointF(cols.D(j), rows.D(j)) - base, tangent);
            lo = std::min(lo, t);
            hi = std::max(hi, t);
          }
          r.type = "Line";
          r.label = QStringLiteral("直线长度");
          r.first = base + tangent * lo;
          r.second = base + tangent * hi;
          r.center = (r.first + r.second) / 2.;
          r.measure = length(r.second - r.first);
          r.unit = "px";
          if (r.measure < 1e-8) fail("拟合直线退化");
          r.angle = std::atan2(r.second.y() - r.first.y(),
                               r.second.x() - r.first.x()) *
                    180 / pi;
          r.values["length"] = r.measure;
          r.values["angle"] = r.angle;
          r.values["x1"] = r.first.x();
          r.values["y1"] = r.first.y();
          r.values["x2"] = r.second.x();
          r.values["y2"] = r.second.y();
          r.overlay.moveTo(r.first);
          r.overlay.lineTo(r.second);
        } else if (p.algorithm == Algorithm::EllipseFit) {
          if (rows.Length() > 200000)
            fail("椭圆拟合超过 20 万采样点，请先精简轮廓");
          double row, col, phi, major, minor, start, end;
          mv::MvString order;
          contour.FitEllipseContourXld(method == 0   ? "fitzgibbon"
                                       : method == 1 ? "geohuber"
                                                     : "geotukey",
                                       -1, 2., 0, 200, 10, 2., &row, &col, &phi,
                                       &major, &minor, &start, &end, &order);
          if (!std::isfinite(major) || !std::isfinite(minor) || major <= 0 ||
              minor <= 0)
            fail("椭圆拟合退化");
          r.type = "Ellipse";
          r.label = QStringLiteral("椭圆长轴");
          r.center = {col, row};
          r.radius = major;
          r.radius2 = minor;
          r.angle = -phi * 180 / pi;
          r.measure = 2 * major;
          r.unit = "px";
          r.values["majorDiameter"] = 2 * major;
          r.values["minorDiameter"] = 2 * minor;
          r.values["majorRadius"] = major;
          r.values["minorRadius"] = minor;
          r.values["angle"] = r.angle;
          r.values["arc"] = std::abs(end - start) * 180 / pi;
          if (r.values["arc"] < v("minArc", 60))
            out.error = QStringLiteral("椭圆弧覆盖不足，保留拟合证据供检查");
          QPainterPath local;
          local.addEllipse(QPointF(0, 0), major, minor);
          local.moveTo(-major, 0);
          local.lineTo(major, 0);
          local.moveTo(0, -minor);
          local.lineTo(0, minor);
          QTransform t;
          t.translate(col, row);
          t.rotate(r.angle);
          r.overlay = t.map(local);
        } else {
          double row, col, radius, start, end;
          mv::MvString order;
          contour.FitCircleContourXld(method == 0   ? "geometric"
                                      : method == 1 ? "geohuber"
                                                    : "geotukey",
                                      -1, 2., 0, 5, 2., &row, &col, &radius,
                                      &start, &end, &order);
          r.type = "Circle";
          r.label = QStringLiteral("圆直径");
          r.center = {col, row};
          r.first = r.center;
          r.radius = radius;
          r.measure = 2 * radius;
          r.unit = "px";
          const double arc = std::abs(end - start) * 180 / pi;
          if (!std::isfinite(radius) || radius <= 0)
            fail("圆拟合无效，请检查轮廓");
          if (arc < v("minArc", 60))
            out.error = QStringLiteral("圆弧覆盖不足，保留拟合证据供检查");
          r.values["radius"] = radius;
          r.values["diameter"] = 2 * radius;
          r.values["arc"] = arc;
          r.overlay.addEllipse(r.center, radius, radius);
        }
        fittingQuality(r, rows, cols, v("qualityTolerance", 1));
        // This is the residual of ALL input samples, including robust-fit
        // outliers.
        if (r.values["rms"] > v("maxRms", 3))
          out.error = QStringLiteral(
              "拟合残差超过限制，已保留采样质量供检查；下游不执行");
        r.values["centerX"] = r.center.x();
        r.values["centerY"] = r.center.y();
        r.values["value"] = r.measure;
      }
      out.objects.append(r);
    }
    if (p.algorithm != Algorithm::ContourFilter &&
        p.algorithm != Algorithm::ContourProcess && out.objects.isEmpty())
      fail("没有可拟合轮廓，请检查边缘和筛选条件");
    out.measureName = p.algorithm == Algorithm::CircleFit
                          ? QStringLiteral("直径 / px")
                          : QStringLiteral("长度 / px");
  } else {
    const auto &x = object(a, int(v("objectA", 1)));
    ResultObject r;
    r.center = x.center;
    r.overlay = x.overlay;
    r.unit = "px";
    if (p.algorithm == Algorithm::Judge) {
      const auto field = o.value("field").toString("value");
      if (!x.values.contains(field))
        fail("所选命名数值不存在，请选择上游的有效输出");
      r.type = "Verdict";
      r.measure = x.values[field];
      r.unit = fieldUnit(x, field);
      r.verdict =
          r.measure >= v("lower", 0) && r.measure <= v("upper", 100) ? 1 : 0;
      r.label = r.verdict ? "OK" : "NG";
      r.values["value"] = r.measure;
      r.values["lower"] = v("lower", 0);
      r.values["upper"] = v("upper", 100);
      out.measureName = QStringLiteral("判定数值 / %1").arg(r.unit);
    } else {
      const int mode = int(v("mode", 0));
      r.type = "Value";
      QPointF start = x.center, end = x.center;
      if (mode == 0) {
        if (x.type != "Circle") fail("直径测量的对象 A 必须为圆");
        r.measure = 2 * x.radius;
        start = x.center - QPointF(x.radius, 0);
        end = x.center + QPointF(x.radius, 0);
        r.label = QStringLiteral("直径");
      } else {
        const auto &y = object(b, int(v("objectB", 1)));
        r.overlay.addPath(y.overlay);
        if (mode == 1 || mode == 2) {
          if (x.type != "Circle" || y.type != "Line")
            fail("对象 A 须为圆，对象 B 须为无限直线");
          end = projection(x.center, y);
          r.measure = length(end - x.center);
          if (mode == 2) {
            r.measure = std::max(0., r.measure - x.radius);
            if (length(end - start) > 1e-10)
              start += (end - start) * (x.radius / length(end - start));
            else
              start = end;
          }
          r.label = mode == 1 ? QStringLiteral("圆心到直线")
                              : QStringLiteral("圆周到直线");
        } else if (mode == 3) {
          end = y.center;
          r.measure = length(end - start);
          r.label = QStringLiteral("中心距");
        } else if (mode == 4) {
          if (x.type != "Line" || y.type != "Line")
            fail("夹角测量需要两条直线");
          const auto u = x.second - x.first, w = y.second - y.first;
          if (length(u) < 1e-8 || length(w) < 1e-8) fail("直线退化");
          r.measure =
              std::acos(std::clamp(
                  std::abs(dot(u, w) / (length(u) * length(w))), 0., 1.)) *
              180 / pi;
          r.unit = QStringLiteral("°");
          r.label = QStringLiteral("锐夹角");
          end = y.center;
        } else {
          if (x.type != "Circle" || y.type != "Circle")
            fail("圆周间隙需要两个圆");
          const double d = length(y.center - x.center);
          r.measure = std::max(
              {0., d - x.radius - y.radius, std::abs(x.radius - y.radius) - d});
          end = y.center;
          r.label = QStringLiteral("圆周最短间隙");
        }
      }
      r.values["value"] = r.measure;
      r.overlay.moveTo(start);
      r.overlay.lineTo(end);
      r.center = (start + end) / 2.;
      out.measureName = r.label + QStringLiteral(" / ") + r.unit;
    }
    if (!std::isfinite(r.measure)) fail("测量结果不是有限数值");
    out.objects.append(r);
  }
  out.algorithmMs = out.totalMs = timer.nsecsElapsed() / 1e6;
  out.steps.append({out.pipeline, out.algorithmMs});
  return out;
}
}  // namespace workbench
