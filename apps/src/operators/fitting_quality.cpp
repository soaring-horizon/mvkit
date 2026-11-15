// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
// Residuals are independently derived Euclidean distances. For ellipses,
// minimize squared distance on the first quadrant: symmetry handles signs.
#include "operators/fitting_quality.hpp"

#include <QTransform>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace workbench {
namespace {
double norm(QPointF p) { return std::hypot(p.x(), p.y()); }
double ellipseDistance(QPointF p, double a, double b) {
  const double x = std::abs(p.x()), y = std::abs(p.y());
  auto f = [&](double t) {
    return std::pow(a * std::cos(t) - x, 2) + std::pow(b * std::sin(t) - y, 2);
  };
  constexpr double halfPi = 1.57079632679489661923;
  int best = 0;
  double minimum = f(0);
  for (int i = 1; i <= 32; ++i) {
    double value = f(i * halfPi / 32);
    if (value < minimum) {
      minimum = value;
      best = i;
    }
  }
  double lo = std::max(0, best - 1) * halfPi / 32,
         hi = std::min(32, best + 1) * halfPi / 32;
  for (int i = 0; i < 48; ++i) {
    const double left = lo + (hi - lo) * .381966011250105,
                 right = lo + (hi - lo) * .618033988749895;
    if (f(left) < f(right))
      hi = right;
    else
      lo = left;
  }
  return std::sqrt(std::min({minimum, f(lo), f(hi)}));
}
}  // namespace
void fittingQuality(ResultObject& object, const mv::MvTuple& rows,
                    const mv::MvTuple& columns, double tolerance) {
  double sum = 0, maximum = 0;
  int outside = 0;
  const auto v = object.second - object.first;
  const double denom = v.x() * v.x() + v.y() * v.y();
  QTransform inverse;
  inverse.rotate(-object.angle);
  for (std::size_t i = 0; i < rows.Length(); ++i) {
    const QPointF point(columns.D(i), rows.D(i));
    double residual;
    if (object.type == "Line") {
      auto d = point - object.first;
      auto projection =
          object.first + v * ((d.x() * v.x() + d.y() * v.y()) / denom);
      residual = norm(point - projection);
    } else if (object.type == "Circle")
      residual = std::abs(norm(point - object.center) - object.radius);
    else
      residual = ellipseDistance(inverse.map(point - object.center),
                                 object.radius, object.radius2);
    if (!std::isfinite(residual))
      throw std::runtime_error("拟合残差无效，几何可能退化");
    object.samples.append(point);
    object.residuals.append(residual);
    sum += residual * residual;
    maximum = std::max(maximum, residual);
    outside += residual > tolerance;
  }
  object.values["rms"] =
      std::sqrt(sum / std::max<std::size_t>(1, rows.Length()));
  object.values["maxResidual"] = maximum;
  object.values["sampleCount"] = double(rows.Length());
  object.values["qualityTolerance"] = tolerance;
  object.values["outsideTolerance"] = outside;
}
}  // namespace workbench
