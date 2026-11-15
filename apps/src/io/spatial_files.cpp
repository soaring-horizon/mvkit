// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#include "model/spatial_data.hpp"
#include <QLocale>
#include <QSaveFile>
#include <QTextStream>
#include <cmath>
#include <stdexcept>

namespace workbench {
namespace {
void require(bool valid, const char* message) {
  if (!valid) throw std::runtime_error(message);
}
}  // namespace
void savePointCloud(const SpatialData& data, const QString& path) {
  require(!data.points.isEmpty() && data.points.size() <= 1000000 && !data.frame.isEmpty(),
          "没有当前有效点云可导出");
  QSaveFile file(path);
  require(file.open(QIODevice::WriteOnly), "无法创建点云文件");
  QTextStream out(&file);
  out.setLocale(QLocale::c());
  out.setRealNumberPrecision(17);
  out << "ply\nformat ascii 1.0\ncomment MV Studio\ncomment length_unit mm\n"
      << "comment frame_base64 " << data.frame.toUtf8().toBase64() << '\n'
      << "comment capture_base64 " << data.group.toUtf8().toBase64() << '\n'
      << "element vertex " << data.points.size()
      << "\nproperty double x\nproperty double y\nproperty double z\n"
         "property int source_row\nproperty int source_column\nend_header\n";
  for (const auto& p : data.points) {
    require(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
                p.z > 0 && p.row >= 0 && p.column >= 0, "点云包含无效坐标");
    out << p.x << ' ' << p.y << ' ' << p.z << ' ' << p.row << ' ' << p.column << '\n';
  }
  out.flush();
  require(out.status() == QTextStream::Ok && file.commit(), "点云文件写入失败");
}
} // namespace workbench
