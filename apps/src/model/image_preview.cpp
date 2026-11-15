// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "model/image_preview.hpp"
#include <QPainter>

namespace workbench {
QPainterPath imageDomainPath(const mv::MvImage& image) {
  QPainterPath path;
  path.setFillRule(Qt::WindingFill);
  if (!image.CountObj()) return path;
  mv::MvTuple rows, starts, ends;
  image.GetDomain().GetRegionRuns(&rows, &starts, &ends);
  // Consolidate vertical runs so a full image becomes one clip rectangle.
  for (std::size_t i = 0; i < rows.Length();) {
    std::size_t j = i + 1;
    while (j < rows.Length() && rows.I(j) == rows.I(j - 1) + 1 &&
           starts.I(j) == starts.I(i) && ends.I(j) == ends.I(i))
      ++j;
    path.addRect(starts.D(i) - .5, rows.D(i) - .5, ends.D(i) - starts.D(i) + 1,
                 double(j - i));
    i = j;
  }
  return path;
}
QImage displayComposite(const QImage& original, const QImage& processed,
                        const QPainterPath& domain) {
  if (processed.isNull() || processed.size() != original.size())
    return original;
  auto image = original.copy();
  QPainter painter(&image);
  painter.setClipPath(domain.translated(.5, .5));
  painter.drawImage(0, 0, processed);
  return image;
}
}  // namespace workbench
