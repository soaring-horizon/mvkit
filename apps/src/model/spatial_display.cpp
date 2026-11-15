// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "model/spatial_data.hpp"
#include "model/image_preview.hpp"
#include <QColor>
#include <algorithm>
#include <cmath>
#include <limits>

namespace workbench {
namespace {
struct Plane {
  const char* pixels;
  size_t stride;
  mv::MvPixelType type;
};
double sample(const Plane& plane, int row, int col) {
  const auto* p = plane.pixels + row * plane.stride;
  switch (plane.type) {
    case mv::MvPixelType::UInt8: return reinterpret_cast<const unsigned char*>(p)[col];
    case mv::MvPixelType::UInt16: return reinterpret_cast<const unsigned short*>(p)[col];
    case mv::MvPixelType::Int16: return reinterpret_cast<const short*>(p)[col];
    case mv::MvPixelType::Int32: return reinterpret_cast<const int*>(p)[col];
    case mv::MvPixelType::Float32: return reinterpret_cast<const float*>(p)[col];
  }
  return std::numeric_limits<double>::quiet_NaN();
}
QColor ramp(double t) {
  // Project-owned blue -> teal -> amber palette. Display only, no pixel edits.
  const QColor a = t <= .5 ? QColor("#5865e9") : QColor("#21b6b0");
  const QColor b = t <= .5 ? QColor("#21b6b0") : QColor("#f3b656");
  t = t <= .5 ? t * 2 : (t - .5) * 2;
  return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t,
                         a.greenF() * (1 - t) + b.greenF() * t,
                         a.blueF() * (1 - t) + b.blueF() * t);
}
QColor reasonColor(int mask) {
  if (!mask) return QColor("#21b6b0");
  if (mask & 256) return QColor("#db5268");
  if (mask & 16) return QColor("#e27e48");
  if (mask & 8) return QColor("#e8b146");
  return QColor("#9a6cdb");
}
}  // namespace
ScalarRange spatialRange(const SpatialData& data, const QString& key) {
  const auto it = data.rasters.constFind(key);
  if (it == data.rasters.cend() || it->image.CountObj() != 1 || it->image.Channels() != 1) return {};
  const auto& image = it->image;
  const Plane plane{static_cast<const char*>(image.Data()), image.Stride(), image.Type()};
  auto domain = image.GetDomain();
  if (it->semantic == "correspondence" && data.rasters.contains("decodeValid"))
    domain = domain.Intersection(regionMask(data.rasters["decodeValid"].image.Threshold(1, 255)));
  mv::MvTuple rows, first, last;
  domain.GetRegionRuns(&rows, &first, &last);
  ScalarRange result{INFINITY, -INFINITY, false};
  for (size_t run = 0; run < rows.Length(); ++run) {
    const int row = int(rows.I(run)), begin = int(first.I(run)), end = int(last.I(run));
    for (int col = begin; col <= end; ++col) {
      const auto value = sample(plane, row, col);
      if (std::isfinite(value)) {
        result.lower = std::min(result.lower, value); result.upper = std::max(result.upper, value); result.available = true;
      }
    }
  }
  return result;
}
SpatialPreview spatialPreview(const SpatialData& data, const QString& key, bool color, ScalarRange scale) {
  SpatialPreview result;
  const auto it = data.rasters.constFind(key);
  if (it == data.rasters.cend()) return result;
  const auto& raster = it.value();
  const auto& image = raster.image;
  const Plane plane{static_cast<const char*>(image.Data()), image.Stride(), image.Type()};
  if (raster.semantic == "intensity" || image.Channels() != 1) {
    result.image = makePreview(image).image;
    result.domain = imageDomainPath(image);
    return result;
  }
  auto domain = image.GetDomain();
  if (raster.semantic == "correspondence" && data.rasters.contains("decodeValid"))
    domain = domain.Intersection(regionMask(data.rasters["decodeValid"].image.Threshold(1, 255)));
  result.domain = imageDomainPath(image.ReduceDomain(domain));
  mv::MvTuple rows, first, last;
  domain.GetRegionRuns(&rows, &first, &last);
  const auto actual = spatialRange(data, key);
  result.image = QImage(int(image.Width()), int(image.Height()), QImage::Format_ARGB32);
  result.image.fill(Qt::transparent);
  if (!actual.available) { result.legend = QStringLiteral("无有效数值"); return result; }
  const bool fixed = raster.semantic == "depth" && raster.unit == "m" && scale.available &&
      std::isfinite(scale.lower) && std::isfinite(scale.upper) && scale.lower <= scale.upper;
  const bool residual = raster.semantic == "residual";
  const double extent = std::max(std::abs(actual.lower), std::abs(actual.upper));
  const double lo = residual ? -extent : fixed ? scale.lower : actual.lower;
  const double hi = residual ? extent : fixed ? scale.upper : actual.upper;
  const bool reason = raster.semantic == "reason" && key == "decodeReason";
  const bool validity = raster.semantic == "validity";
  const double span = hi - lo;
  for (size_t run = 0; run < rows.Length(); ++run) {
    const int row = int(rows.I(run));
    const int begin = int(first.I(run)), end = int(last.I(run));
    auto* target = reinterpret_cast<QRgb*>(result.image.scanLine(row));
    for (int col = begin; col <= end; ++col) {
      const double value = sample(plane, row, col);
      if (!std::isfinite(value)) continue;
      const double t = span > 1e-12 * std::max(1., std::abs(hi)) ? std::clamp((value - lo) / span, 0., 1.) : .5;
      if (color) target[col] = (reason ? reasonColor(int(value)) : validity ? (value > 0 ? QColor("#21b6b0") : QColor("#db5268")) : ramp(t)).rgba();
      else { const int g = qRound(t * 255); target[col] = qRgb(g, g, g); }
    }
  }
  if (residual) result.legend = QStringLiteral("±%1 mm · %2 · %3")
      .arg(extent, 0, 'g', 5)
      .arg(color ? QStringLiteral("蓝：负 · 青：零 · 黄：正") : QStringLiteral("灰度：负→正"))
      .arg(raster.label.startsWith(QStringLiteral("平面")) ? QStringLiteral("符号沿拟合单位法向") : QStringLiteral("球外为正，球内为负"));
  else if (color && reason) result.legend = QStringLiteral("绿：有效 · 黄：低对比 · 橙：弱相移 · 红：饱和 · 紫：其他");
  else if (color && validity) result.legend = QStringLiteral("绿：有效 · 红：无效");
  else {
    const double scale = raster.unit == "m" ? 1000. : 1.;
    const auto unit = raster.unit == "m" ? QStringLiteral("mm") : raster.unit;
    result.legend = QStringLiteral("%1–%2 %3 · %4")
        .arg(lo * scale, 0, 'g', 6).arg(hi * scale, 0, 'g', 6).arg(unit)
        .arg(color ? QStringLiteral("蓝→青→黄：低→高") : QStringLiteral("灰度：低→高"));
    if (span <= 1e-12 * std::max(1., std::abs(hi))) result.legend += QStringLiteral(" · 恒定值");
    if (fixed) result.legend += QStringLiteral(" · 有效值 %1–%2 mm · 范围外颜色截断，数值保留")
        .arg(actual.lower * 1000, 0, 'g', 6).arg(actual.upper * 1000, 0, 'g', 6);
  }
  return result;
}
}  // namespace workbench
